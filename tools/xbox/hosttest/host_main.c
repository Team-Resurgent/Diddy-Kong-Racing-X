/* host_main.c -- the PC test harness: the Xbox build's game and host layer
 * (Platform/xbox/dkr_*.c) run on Windows, drawn by gfx_soft.c, as fast as
 * the PC goes, with no window. How the port's gameplay is checked without a
 * console.
 *
 *   hosttest.exe [--assets DIR] [--frames N] [--shots F,F,...] [--every N]
 *                [--script FILE] [--out DIR] [--eeprom FILE] [--wav FILE]
 *                [--time] [--threaded]
 *
 * --script holds lines "FRAME PAD BUTTONS [X Y]": from frame FRAME on, pad
 * PAD (0-3) holds BUTTONS (letters, '-' for none) and its stick at X, Y
 * (-80..80). Letters: A B Z S(tart) L R u d l r (D-pad) U D W E (C up,
 * down, left, right). Pads named in a script are plugged in; pad 0 always is.
 * FRAME may instead be "mMENU+K": K frames after the game next opens menu
 * MENU (src/menu.h's MENU_ID: 0 title, 3 character select, 15 track select,
 * 19 game select ...), counted from the menu the line before it waited for.
 * The game's menu and mode changes are logged as STATE lines.
 * Screenshots are OUT/frame_NNNNN.png; the sound, 22050 Hz stereo, goes to
 * --wav. The EEPROM is read from and written to --eeprom. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dkr_host.h"
#include "dkr_settings.h"

#define MAX_EVENTS 4096
#define MAX_SHOTS 256

typedef struct {
    int frame, pad;
    int menu, offset; /* menu >= 0: frame is set when that menu opens */
    uint16_t buttons;
    int8_t x, y;
} Event;

static const char *sAssets = "assets/";
static const char *sOut = ".";
static const char *sEeprom;
static FILE *sWav;
static uint32_t sWavBytes;
static int sFrames = 600;
static int sEvery;
static int sShots[MAX_SHOTS], sShotCount;
static Event sEvents[MAX_EVENTS];
static int sEventCount;
static int sFrame;
static HostPad sPads[HOST_PADS];
static int sPadUsed[HOST_PADS] = { 1 };
static int sEepromDirty;
static int sCPakDirty[HOST_PADS];

const unsigned int *soft_framebuffer(int *w, int *h);
void soft_set_wide(void);
void gfx_init(void);

/* ----------------------------------------------------------- platform */

void host_log(const char *line)
{
    fputs(line, stdout);
    fflush(stdout);
}

uint64_t host_time_ns(void)
{
    /* the game's clock runs at the frame rate it is shown, as on a console
     * that keeps up: 60 frames a second, whatever the PC does */
    return (uint64_t)sFrame * 1000000000ull / 60;
}

uint8_t *host_load_file(const char *name, uint32_t *size)
{
    char path[512];
    uint8_t *buf;
    long n;
    FILE *f;

    snprintf(path, sizeof(path), "%s%s", sAssets, name);
    if ((f = fopen(path, "rb")) == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n > 0 ? n : 1);
    if (buf != NULL && fread(buf, 1, n, f) != (size_t)n)
    {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

void host_pads_read(HostPad pads[HOST_PADS])
{
    memcpy(pads, sPads, sizeof(sPads));
}

void host_rumble(int port, int on)
{
    static int sOn[HOST_PADS];

    if (port >= 0 && port < HOST_PADS && sOn[port] != on)
    {
        sOn[port] = on;
        printf("RUMBLE: pad %d %s at frame %d\n", port, on ? "on" : "off", sFrame);
    }
}

int host_eeprom_load(uint8_t *buf, int size)
{
    FILE *f;
    int ok;

    if (sEeprom == NULL || (f = fopen(sEeprom, "rb")) == NULL)
        return 0;
    ok = fread(buf, 1, size, f) == (size_t)size;
    fclose(f);
    return ok;
}

void host_eeprom_changed(void)
{
    sEepromDirty = 1;
}

/* the Controller Pak in each port: <eeprom>.cpak1 .. .cpak4 beside the EEPROM */
static void cpak_path(char *out, size_t size, int port)
{
    snprintf(out, size, "%s.cpak%d", sEeprom, port + 1);
}

int host_cpak_load(int port, uint8_t *buf, int size)
{
    char path[1024];
    FILE *f;
    int ok;

    if (sEeprom == NULL || port < 0 || port >= HOST_PADS)
        return 0;
    cpak_path(path, sizeof(path), port);
    if ((f = fopen(path, "rb")) == NULL)
        return 0;
    ok = fread(buf, 1, size, f) == (size_t)size;
    fclose(f);
    if (ok)
        printf("HOST: Controller Pak %d read from %s\n", port + 1, path);
    return ok;
}

void host_cpak_changed(int port)
{
    if (port >= 0 && port < HOST_PADS)
        sCPakDirty[port] = 1;
}

/* the port's settings: <eeprom>.settings beside the EEPROM */
static int sSettingsDirty;

int host_settings_load(uint8_t *buf, int size)
{
    char path[1024];
    FILE *f;
    int ok;

    if (sEeprom == NULL)
        return 0;
    snprintf(path, sizeof(path), "%s.settings", sEeprom);
    if ((f = fopen(path, "rb")) == NULL)
        return 0;
    ok = fread(buf, 1, size, f) == (size_t)size;
    fclose(f);
    return ok;
}

void host_settings_changed(void)
{
    sSettingsDirty = 1;
}

/* The audio manager runs on this thread (host_frame_done, as on the Xbox):
 * nothing to fence. With --threaded the sound is drained by a thread of its
 * own, as xbox_audio.c's is. */
static int sThreaded;
void rt_thread_start(void (*fn)(void));
void rt_sleep_ms(unsigned ms);
void rt_fault_report(void);

void host_audio_lock(void)
{
}

/* (no output queue: the harness's sound is what dkr_audio.c's ring holds) */
int host_audio_queued(void)
{
    return 0;
}

void host_audio_unlock(void)
{
}

int host_audio_lock_held(void)
{
    return 0;
}

/* xbox_audio.c's thread, roughly: a 512-frame packet every 23 ms, for ever */
static void audio_thread(void)
{
    static int16_t pcm[512 * 2];

    for (;;)
    {
        dkr_audio_render(pcm, 512);
        rt_sleep_ms(23);
    }
}

/* -------------------------------------------------------------- pacing */

/* src/video.c: the retrace periods this frame covers, which is the game's
 * logic step. The build's rate (DKR_LOGIC_RATE: 1 = 60 frames a second, 2 =
 * the N64's 30), every frame, as on a console that keeps up. */
#ifndef DKR_LOGIC_RATE
#define DKR_LOGIC_RATE 1
#endif

int32_t pc_retrace_wait(void)
{
    return DKR_LOGIC_RATE;
}

/* dkr_scene.c: a level has loaded (the harness's clock never ran ahead) */
void pc_retrace_reset(void)
{
}

/* ----------------------------------------------------------------- PNG */

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;

    if (crc_table[1] == 0)
    {
        uint32_t k, j, v;

        for (k = 0; k < 256; k++)
        {
            for (v = k, j = 0; j < 8; j++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    }
    c ^= 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8], t[4];
    uint32_t c;

    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, len, f);
    c = crc(crc(0, (const uint8_t *)type, 4), data, len);
    be32(t, c);
    fwrite(t, 1, 4, f);
}

/* RGB, deflate's stored blocks: big, but no zlib */
static void write_png(const char *path, const unsigned int *px, int w, int h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t raw = (size_t)(w * 3 + 1) * h, blocks = (raw + 65534) / 65535;
    uint8_t *img = (uint8_t *)malloc(raw), *z = (uint8_t *)malloc(raw + blocks * 5 + 6);
    uint8_t ihdr[13];
    uint32_t a = 1, b = 0;
    size_t i, o = 0, left;
    int x, y;
    FILE *f;

    for (y = 0; y < h; y++)
    {
        uint8_t *row = img + (size_t)y * (w * 3 + 1);

        row[0] = 0;
        for (x = 0; x < w; x++)
        {
            unsigned int p = px[y * w + x];

            row[1 + x * 3] = (uint8_t)p;
            row[2 + x * 3] = (uint8_t)(p >> 8);
            row[3 + x * 3] = (uint8_t)(p >> 16);
        }
    }
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (i = 0, left = raw; left > 0;)
    {
        size_t n = left > 65535 ? 65535 : left;

        z[o++] = (uint8_t)(left == n);
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, img + i, n);
        o += n;
        i += n;
        left -= n;
    }
    for (i = 0; i < raw; i++)
    {
        a = (a + img[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z + o, (b << 16) | a);
    o += 4;

    f = fopen(path, "wb");
    if (f != NULL)
    {
        fwrite(sig, 1, 8, f);
        be32(ihdr, (uint32_t)w);
        be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8;
        ihdr[9] = 2;
        ihdr[10] = ihdr[11] = ihdr[12] = 0;
        chunk(f, "IHDR", ihdr, 13);
        chunk(f, "IDAT", z, (uint32_t)o);
        chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(img);
    free(z);
}

/* ---------------------------------------------------------------- WAV */

static void wav_header(FILE *f, uint32_t bytes)
{
    uint8_t h[44];

    memcpy(h, "RIFF", 4);
    *(uint32_t *)(h + 4) = 36 + bytes;
    memcpy(h + 8, "WAVEfmt ", 8);
    *(uint32_t *)(h + 16) = 16;
    *(uint16_t *)(h + 20) = 1;
    *(uint16_t *)(h + 22) = 2;
    *(uint32_t *)(h + 24) = DKR_AUDIO_RATE;
    *(uint32_t *)(h + 28) = DKR_AUDIO_RATE * 4;
    *(uint16_t *)(h + 32) = 4;
    *(uint16_t *)(h + 34) = 16;
    memcpy(h + 36, "data", 4);
    *(uint32_t *)(h + 40) = bytes;
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

/* --------------------------------------------------------- per frame */

/* the game's menu and mode (src/menu.c, src/thread3_main.c) */
extern int32_t gCurrentMenuId;
extern int32_t gGameMode;
static int sArmed; /* events before this one have their frames */

static void watch_state(void)
{
    static int32_t sMenu = -1, sMode = -1;
    int i;

    if (gCurrentMenuId == sMenu && gGameMode == sMode)
        return;
    printf("STATE frame %d: menu %d, mode %d\n", sFrame, (int)gCurrentMenuId, (int)gGameMode);
    /* (not at boot: the menu variable starts at 0, the title's, before any
     * menu has opened) */
    if (gCurrentMenuId != sMenu && sMenu != -1)
    {
        /* the next menu-waiting events in script order that are this menu's
         * (groups before them, for menus that never opened, are dropped) */
        int j = sArmed;

        while (j < sEventCount && sEvents[j].menu != gCurrentMenuId)
            j++;
        if (j < sEventCount)
        {
            int menu = sEvents[j].menu;

            for (i = j; i < sEventCount && (sEvents[i].menu == menu || sEvents[i].menu < 0); i++)
                if (sEvents[i].menu == menu)
                    sEvents[i].frame = sFrame + sEvents[i].offset;
            sArmed = i;
        }
    }
    sMenu = gCurrentMenuId;
    sMode = gGameMode;
}

static void apply_script(void)
{
    int i;

    watch_state();
    for (i = 0; i < sEventCount; i++)
    {
        const Event *e = &sEvents[i];

        if (e->frame == sFrame)
        {
            sPads[e->pad].buttons = e->buttons;
            sPads[e->pad].stickX = e->x;
            sPads[e->pad].stickY = e->y;
        }
    }
}

static void finish(void)
{
    if (sEepromDirty && sEeprom != NULL)
    {
        FILE *f = fopen(sEeprom, "wb");

        if (f != NULL)
        {
            fwrite(dkr_eeprom_image(), 1, dkr_eeprom_size(), f);
            fclose(f);
            printf("HOST: EEPROM written to %s\n", sEeprom);
        }
    }
    for (int port = 0; port < HOST_PADS; port++)
    {
        char path[1024];
        FILE *f;

        if (!sCPakDirty[port] || sEeprom == NULL)
            continue;
        cpak_path(path, sizeof(path), port);
        if ((f = fopen(path, "wb")) != NULL)
        {
            fwrite(dkr_cpak_image(port), 1, dkr_cpak_size(), f);
            fclose(f);
            printf("HOST: Controller Pak %d written to %s\n", port + 1, path);
        }
    }
    if (sSettingsDirty && sEeprom != NULL)
    {
        char path[1024];
        FILE *f;

        snprintf(path, sizeof(path), "%s.settings", sEeprom);
        if ((f = fopen(path, "wb")) != NULL)
        {
            fwrite(dkr_settings_image(), 1, dkr_settings_size(), f);
            fclose(f);
            printf("HOST: settings written to %s\n", path);
        }
    }
    if (sWav != NULL)
    {
        wav_header(sWav, sWavBytes);
        fclose(sWav);
    }
    printf("HOST: %d frames\n", sFrame);
    fflush(stdout);
    exit(0);
}

/* --time: what the game's own work costs a frame, the rasteriser left out
 * (what the Xbox's CPU does; its GPU draws), every 120 frames */
static int sTime;
static double sAudioMs;
extern int gSoftNoRaster;
extern unsigned long gSoftTris;
extern double gSoftDlMs;

static double now_ms(void)
{
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void time_frame(void)
{
    static double sLast, sSum, sMax, sAudio, sDl;
    static unsigned long sTris;
    static int sN;
    double t = now_ms(), d;

    if (sLast != 0.0)
    {
        d = t - sLast - (sAudioMs - sAudio);
        sSum += d;
        if (d > sMax)
            sMax = d;
        sN++;
    }
    sAudio = sAudioMs;
    sLast = t;
    if (sN == 120)
    {
        printf("TIME frames %d-%d: game %.2f ms a frame (longest %.2f; display list %.2f), "
               "audio %.2f ms a frame, %lu triangles a frame\n", sFrame - 119, sFrame, sSum / sN, sMax,
               (gSoftDlMs - sDl) / sN, sAudioMs / (sFrame + 1), (gSoftTris - sTris) / sN);
        sDl = gSoftDlMs;
        sTris = gSoftTris;
        sSum = sMax = 0.0;
        sN = 0;
    }
}

/* gfx_soft.c's gfx_frame_end: the frame is drawn */
void music_play(unsigned char seqID); /* src/audio.c */
unsigned char music_sequence_count(void);
void music_jingle_play(unsigned char seqID);
unsigned int music_jingle_playing(void);
static int sMusic = -1;
static int sJingle = -1, sJingleFrame;
void sound_play(unsigned short soundID, void *handlePtr);
static int sSound = -1, sSoundFrame;

void host_frame_done(void)
{
    int i, shot = sEvery > 0 && sFrame > 0 && sFrame % sEvery == 0;

    for (i = 0; i < sShotCount; i++)
        shot |= sShots[i] == sFrame;
    if (shot)
    {
        char path[512];
        int w, h;
        const unsigned int *fb = soft_framebuffer(&w, &h);

        snprintf(path, sizeof(path), "%s/frame_%05d.png", sOut, sFrame);
        write_png(path, fb, w, h);
        printf("HOST: %s\n", path);
    }

    /* --music: the sequence the audio options' music test would play, from
     * frame 300 (the title screen, its own music started) */
    if (sMusic >= 0 && sFrame == 300)
    {
        music_play((unsigned char)sMusic);
        printf("HOST: music %d of %d\n", sMusic, music_sequence_count());
    }

    /* --jingle N F: the jingle a pickup plays (src/object_functions.c), at
     * frame F, and whether the jingle player says it is playing after */
    if (sJingle >= 0 && sFrame == sJingleFrame)
        music_jingle_play((unsigned char)sJingle);
    /* --sound N F: sound effect N (the table's index, src/audio.c sound_play) at frame F */
    if (sSound >= 0 && sFrame == sSoundFrame)
        sound_play((unsigned short)sSound, NULL);

    /* a sixtieth of a second of sound a frame (367.5 samples: 367 and 368 in
     * turn) */
    dkr_audio_go();
    dkr_audio_fill(1024); /* as xbox_main.c's xb_frame_done */
    if (!sThreaded)
    {
        static int16_t pcm[400 * 2];
        int n = (sFrame & 1) ? 368 : 367;
        double t0 = now_ms();

        dkr_audio_render(pcm, n);
        sAudioMs += now_ms() - t0;
        if (sWav != NULL)
        {
            fwrite(pcm, 4, n, sWav);
            sWavBytes += n * 4;
        }
    }
    if (sTime)
        time_frame();

    sFrame++;
    if (sFrame >= sFrames)
        finish();
    apply_script();
}

/* ------------------------------------------------------------- main */

static uint16_t parse_buttons(const char *s)
{
    uint16_t b = 0;

    for (; *s; s++)
    {
        switch (*s)
        {
            case 'A': b |= HOST_BTN_A; break;
            case 'B': b |= HOST_BTN_B; break;
            case 'Z': b |= HOST_BTN_Z; break;
            case 'S': b |= HOST_BTN_START; break;
            case 'L': b |= HOST_BTN_L; break;
            case 'R': b |= HOST_BTN_R; break;
            case 'u': b |= HOST_BTN_UP; break;
            case 'd': b |= HOST_BTN_DOWN; break;
            case 'l': b |= HOST_BTN_LEFT; break;
            case 'r': b |= HOST_BTN_RIGHT; break;
            case 'U': b |= HOST_BTN_CUP; break;
            case 'D': b |= HOST_BTN_CDOWN; break;
            case 'W': b |= HOST_BTN_CLEFT; break;
            case 'E': b |= HOST_BTN_CRIGHT; break;
            case 'M': b |= HOST_BTN_MENU; break; /* the settings menu (the right stick's click) */
            default: break;
        }
    }
    return b;
}

static void load_script(const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        fprintf(stderr, "hosttest: cannot read %s\n", path);
        exit(1);
    }
    while (fgets(line, sizeof(line), f) && sEventCount < MAX_EVENTS)
    {
        Event *e = &sEvents[sEventCount];
        char buttons[64];
        int x = 0, y = 0, n;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        e->menu = -1;
        if (line[0] == 'm')
        {
            n = sscanf(line, "m%d+%d %d %63s %d %d", &e->menu, &e->offset, &e->pad, buttons, &x, &y);
            e->frame = -1;
            n -= 1;
        }
        else
        {
            n = sscanf(line, "%d %d %63s %d %d", &e->frame, &e->pad, buttons, &x, &y);
        }
        if (n < 3 || e->pad < 0 || e->pad >= HOST_PADS)
            continue;
        e->buttons = parse_buttons(buttons);
        e->x = (int8_t)x;
        e->y = (int8_t)y;
        sPadUsed[e->pad] = 1;
        sEventCount++;
    }
    fclose(f);
}

void dkr_boot(void);

/* clang's *-windows-gnu targets call MinGW's __main (its constructors) from
 * main; this links as an MSVC program, whose runtime has run them already */
void __main(void)
{
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        const char *v = (i + 1 < argc) ? argv[i + 1] : "";

        if (!strcmp(argv[i], "--assets")) { sAssets = v; i++; }
        else if (!strcmp(argv[i], "--frames")) { sFrames = atoi(v); i++; }
        else if (!strcmp(argv[i], "--every")) { sEvery = atoi(v); i++; }
        else if (!strcmp(argv[i], "--out")) { sOut = v; i++; }
        else if (!strcmp(argv[i], "--time")) { sTime = 1; gSoftNoRaster = 1; }
        else if (!strcmp(argv[i], "--wide")) { soft_set_wide(); } /* a 16:9 display, for widescreen */
        else if (!strcmp(argv[i], "--threaded")) { sThreaded = 1; }
        else if (!strcmp(argv[i], "--music")) { sMusic = atoi(v); i++; } /* a music test number */
        else if (!strcmp(argv[i], "--jingle"))
        {
            /* a jingle's sequence number and the frame to play it at */
            sJingle = atoi(v);
            sJingleFrame = (i + 2 < argc) ? atoi(argv[i + 2]) : 2500;
            i += 2;
        }
        else if (!strcmp(argv[i], "--sound"))
        {
            /* a sound effect's number and the frame to play it at */
            sSound = atoi(v);
            sSoundFrame = (i + 2 < argc) ? atoi(argv[i + 2]) : 2500;
            i += 2;
        }
        else if (!strcmp(argv[i], "--eeprom")) { sEeprom = v; i++; }
        else if (!strcmp(argv[i], "--script")) { load_script(v); i++; }
        else if (!strcmp(argv[i], "--wav"))
        {
            sWav = fopen(v, "wb");
            if (sWav != NULL)
                wav_header(sWav, 0);
            i++;
        }
        else if (!strcmp(argv[i], "--shots"))
        {
            const char *p = v;

            while (*p && sShotCount < MAX_SHOTS)
            {
                sShots[sShotCount++] = atoi(p);
                while (*p && *p != ',')
                    p++;
                if (*p == ',')
                    p++;
            }
            i++;
        }
    }
    for (i = 0; i < HOST_PADS; i++)
        sPads[i].connected = sPadUsed[i];
    while (sArmed < sEventCount && sEvents[sArmed].menu < 0)
        sArmed++;
    apply_script();

    rt_fault_report();
    gfx_init();
    if (sThreaded)
        rt_thread_start(audio_thread);
    dkr_boot();
    return 0;
}
