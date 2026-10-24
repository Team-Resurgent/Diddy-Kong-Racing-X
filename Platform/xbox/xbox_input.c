/* xbox_input.c -- the Xbox controllers as the N64's four.
 *
 * The pad in Xbox port N is the N64's controller N, so four pads give four
 * players as on the N64. Player 1 is the first pad plugged in, wherever it
 * is: a lone pad in port 3 plays player 1, as a lone N64 controller has to be
 * in port 1 for the game to see it.
 *
 *   Left stick ........... the N64 stick (steer; menus)
 *   D-pad ................ the N64 D-pad, and the stick when it is idle
 *   A .................... A    accelerate, select
 *   B .................... B    brake, reverse, back
 *   X / Left trigger ..... Z    fire the weapon
 *   Right trigger ........ R    hop, powerslide
 *   Y .................... C-up    the camera
 *   Black ................ C-down  course directions, the multiplayer HUD
 *   White ................ C-right the speedometer
 *   Right stick .......... the C buttons
 *   Back ................. L
 *   Start ................ Start
 *
 * Back + Start + both triggers is the dashboard.
 *
 * Rumble: the game drives a rumble pak's motor on and off, pulsed at the frame
 * rate for the gentler effects. The pad's motors run at the share of recent
 * frames the motor was on: half the frames, half speed. */
#include "xbox_xtl.h"

#include <string.h>

#include "dkr_host.h"
#include "xbox_port.h"

#define PADS 4
#define PRESSED 30        /* an analog button counts past this */
#define STICK_MAX 80      /* the N64 stick's full deflection */
#define DEADZONE 7000     /* of the Xbox stick's 32767, round the centre */
#define CSTICK 16000      /* a right-stick direction counts as a C button past this */
#define STICK_IDLE 8      /* an N64 stick axis this near the centre lets the D-pad in */
#define RUMBLE_BIG 0xC000 /* the motors' speeds while the rumble pak runs flat out */
#define RUMBLE_SMALL 0x8000
#define RUMBLE_FULL 256   /* a pad's rumble level, 0 to this */
#define RUMBLE_STEP 32    /* levels are sent in steps of this, to keep requests few */

typedef struct {
    HANDLE h;
    HostPad pad;
    int quit;
    /* rumble: the game's motor, the level that follows it, the level sent,
     * and whether a request is in flight */
    int on, level, sent, busy;
    XINPUT_FEEDBACK fb;
} Pad;

static Pad sPads[PADS];
static int sReady;

void xb_input_init(void)
{
    XDEVICE_PREALLOC_TYPE prealloc[] = {
        { XDEVICE_TYPE_GAMEPAD, PADS },
    };

    /* enumeration is asynchronous: started first thing in main, the pads
     * are known by the time the game first looks */
    XInitDevices(sizeof(prealloc) / sizeof(prealloc[0]), prealloc);
    sReady = 1;
}

static void refresh(void)
{
    DWORD ins = 0, rem = 0;
    int i;

    if (!XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &ins, &rem))
        return;
    for (i = 0; i < PADS; i++)
    {
        Pad *p = &sPads[i];

        if ((rem & (1u << i)) && p->h != NULL)
        {
            XInputClose(p->h);
            p->h = NULL;
            p->busy = 0;
            p->on = p->level = p->sent = 0;
        }
        if ((ins & (1u << i)) && p->h == NULL)
            p->h = XInputOpen(XDEVICE_TYPE_GAMEPAD, (DWORD)i, XDEVICE_NO_SLOT, NULL);
    }
}

/* Before the game starts: the pads already plugged in, found. Enumeration is
 * asynchronous and can take most of a second, and the game looks for START
 * held at power-on (its Controller Pak screen) only in its first eight
 * frames. Up to a second for a pad, then a moment for its first report. */
void xb_input_wait(void)
{
    DWORD start = GetTickCount();
    int i;

    for (;;)
    {
        refresh();
        for (i = 0; i < PADS; i++)
            if (sPads[i].h != NULL)
            {
                Sleep(50);
                return;
            }
        if (GetTickCount() - start > 1000)
            return;
        Sleep(10);
    }
}

/* -32768..32767 with a dead zone, to the N64's -80..80 */
static int axis(int v)
{
    int s = v < 0 ? -1 : 1;
    int m = v < 0 ? -v : v;

    if (m <= DEADZONE)
        return 0;
    m = (m - DEADZONE) * STICK_MAX / (32767 - DEADZONE);
    return s * (m > STICK_MAX ? STICK_MAX : m);
}

static void read_pad(Pad *p)
{
    XINPUT_STATE s;
    const XINPUT_GAMEPAD *g;
    uint16_t b = 0;
    int x, y, lt, rt;

    memset(&p->pad, 0, sizeof(p->pad));
    p->quit = 0;
    if (p->h == NULL || XInputGetState(p->h, &s) != ERROR_SUCCESS)
        return;
    g = &s.Gamepad;
    p->pad.connected = 1;
    lt = g->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] > PRESSED;
    rt = g->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > PRESSED;

    if (g->bAnalogButtons[XINPUT_GAMEPAD_A] > PRESSED)
        b |= HOST_BTN_A;
    if (g->bAnalogButtons[XINPUT_GAMEPAD_B] > PRESSED)
        b |= HOST_BTN_B;
    if (g->bAnalogButtons[XINPUT_GAMEPAD_X] > PRESSED || lt)
        b |= HOST_BTN_Z;
    if (rt)
        b |= HOST_BTN_R;
    if (g->bAnalogButtons[XINPUT_GAMEPAD_Y] > PRESSED || g->sThumbRY > CSTICK)
        b |= HOST_BTN_CUP;
    if (g->bAnalogButtons[XINPUT_GAMEPAD_BLACK] > PRESSED || g->sThumbRY < -CSTICK)
        b |= HOST_BTN_CDOWN;
    if (g->bAnalogButtons[XINPUT_GAMEPAD_WHITE] > PRESSED || g->sThumbRX > CSTICK)
        b |= HOST_BTN_CRIGHT;
    if (g->sThumbRX < -CSTICK)
        b |= HOST_BTN_CLEFT;
    if (g->wButtons & XINPUT_GAMEPAD_BACK)
        b |= HOST_BTN_L;
    if (g->wButtons & XINPUT_GAMEPAD_START)
        b |= HOST_BTN_START;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_UP)
        b |= HOST_BTN_UP;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
        b |= HOST_BTN_DOWN;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
        b |= HOST_BTN_LEFT;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
        b |= HOST_BTN_RIGHT;

    x = axis(g->sThumbLX);
    y = axis(g->sThumbLY);
    /* the menus and the steering read the stick, so the D-pad drives it too
     * on an axis the stick leaves idle (the Dreamcast port does the same) */
    if (x > -STICK_IDLE && x < STICK_IDLE)
    {
        if (b & HOST_BTN_LEFT)
            x = -STICK_MAX;
        if (b & HOST_BTN_RIGHT)
            x = STICK_MAX;
    }
    if (y > -STICK_IDLE && y < STICK_IDLE)
    {
        if (b & HOST_BTN_UP)
            y = STICK_MAX;
        if (b & HOST_BTN_DOWN)
            y = -STICK_MAX;
    }
    p->pad.buttons = b;
    p->pad.stickX = (int8_t)x;
    p->pad.stickY = (int8_t)y;
    p->quit = (g->wButtons & XINPUT_GAMEPAD_BACK) && (g->wButtons & XINPUT_GAMEPAD_START) && lt && rt;
}

/* The N64's ports from the Xbox's: the pads that are in, in port order, from
 * port 1 on. */
void host_pads_read(HostPad pads[HOST_PADS])
{
    int i, n = 0;

    memset(pads, 0, sizeof(HostPad) * HOST_PADS);
    if (!sReady)
        return;
    refresh();
    for (i = 0; i < PADS; i++)
    {
        read_pad(&sPads[i]);
        if (sPads[i].pad.connected)
            pads[n++] = sPads[i].pad;
    }
}

/* the Xbox pad playing N64 port `port` */
static Pad *pad_of(int port)
{
    int i, n = 0;

    for (i = 0; i < PADS; i++)
        if (sPads[i].h != NULL && n++ == port)
            return &sPads[i];
    return NULL;
}

/* XInputSetState is asynchronous: its XINPUT_FEEDBACK must stay put until the
 * driver has taken it (Header.dwStatus leaves ERROR_IO_PENDING). One request
 * in flight per pad; the latest level goes when it is done. */
static void rumble_send(Pad *p)
{
    int level;

    if (p->h == NULL)
        return;
    if (p->busy)
    {
        if (p->fb.Header.dwStatus == ERROR_IO_PENDING)
            return;
        p->busy = 0;
    }
    /* in steps, and off once it has all but died away */
    level = (p->level < RUMBLE_STEP / 2) ? 0 : ((p->level + RUMBLE_STEP / 2) / RUMBLE_STEP) * RUMBLE_STEP;
    if (level > RUMBLE_FULL)
        level = RUMBLE_FULL;
    if (level == p->sent)
        return;
    memset(&p->fb, 0, sizeof(p->fb));
    p->fb.Rumble.wLeftMotorSpeed = (WORD)(RUMBLE_BIG * level / RUMBLE_FULL);
    p->fb.Rumble.wRightMotorSpeed = (WORD)(RUMBLE_SMALL * level / RUMBLE_FULL);
    if (XInputSetState(p->h, &p->fb) == ERROR_IO_PENDING)
        p->busy = 1;
    p->sent = level;
}

void host_rumble(int port, int on)
{
    Pad *p = pad_of(port);

    if (p == NULL)
        return;
    p->on = on != 0;
    /* a motor started from rest kicks in at once */
    if (p->on && p->level < RUMBLE_FULL / 2)
        p->level = RUMBLE_FULL / 2;
}

int xb_input_frame(void)
{
    int i, quit = 0;

    for (i = 0; i < PADS; i++)
    {
        Pad *p = &sPads[i];

        /* the share of the last few frames the game had the motor on */
        p->level += ((p->on ? RUMBLE_FULL : 0) - p->level) / 3;
        rumble_send(p);
        quit |= p->quit;
    }
    return quit;
}
