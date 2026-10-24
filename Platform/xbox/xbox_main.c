/* xbox_main.c -- the Xbox entry point: Diddy Kong Racing (N64, 1997) from the
 * decompilation, drawn by Direct3D, at 60 frames a second.
 *
 * The game's data is D:\data\assets.bin and assets.lut.bin, which setup.py
 * builds from your ROM and puts on the disc. Without them the screen goes red
 * for a few seconds and the console returns to the dashboard (the debug
 * channel says why).
 *
 * Pacing. The game asks, once a frame, how many video fields went by since
 * the last (src/video.c, pc_retrace_wait below): one at 60 frames a second,
 * more when a frame ran long. Its physics steps two fields at a time, as on
 * the N64; the frames in between are drawn part-way between two steps
 * (dreamcast/frame_interp.c), so motion is drawn at 60. */
#include "xbox_xtl.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dkr_host.h"
#include "xbox_port.h"

#define DATA_DIR "D:\\data\\"

/* setup.py writes the time of each build here; a build made without it (Visual
 * Studio after --skip-build) gives the time this file was compiled */
#if __has_include("gen/build_stamp.h")
#include "gen/build_stamp.h"
#else
#define DKRX_BUILD_STAMP __DATE__ " " __TIME__
#endif

#ifndef DKR_LOGIC_RATE
#define DKR_LOGIC_RATE 1 /* fields a frame: 1 for 60 frames a second */
#endif
#define MAX_UPDATE_RATE 6 /* more than this is a stall, not game time */
/* stereo frames of sound kept made ahead of the output: a packet
 * (xbox_audio.c), on top of the stream's own queue, which carries the sound
 * through a long frame */
#define AUDIO_AHEAD 512

void dkr_boot(void);

/* ---------------------------------------------------------------- log */

void xb_log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n] = '\n';
    line[n + 1] = 0;
    OutputDebugStringA(line);
}

void host_log(const char *line)
{
    OutputDebugStringA(line);
}

/* -------------------------------------------------------------- time */

uint64_t host_time_ns(void)
{
    static LARGE_INTEGER sFreq;
    LARGE_INTEGER t;

    if (sFreq.QuadPart == 0)
        QueryPerformanceFrequency(&sFreq);
    QueryPerformanceCounter(&t);
    return (uint64_t)((double)t.QuadPart * 1e9 / (double)sFreq.QuadPart);
}

/* The video fields so far, as the GPU counts them. */
static DWORD vblanks(void)
{
    D3DFIELD_STATUS fs;

    D3DDevice_GetDisplayFieldStatus(&fs);
    return fs.VBlankCount;
}

static DWORD sLastVbl;
static int sPacerStarted, sPacerReset;
static int sFieldAcc; /* sixtieths of a field not yet counted (50 Hz) */

/* src/video.c: the fields this frame covers (the game's step), waiting for
 * the next field when the frame was quicker than one. The game's fields are
 * NTSC's, sixty a second: on a 50 Hz display each field counts as 1.2 of
 * them, so the game keeps its speed. */
int32_t pc_retrace_wait(void)
{
    DWORD now = vblanks();
    int hz = xb_gfx_refresh();
    int periods;

    if (!sPacerStarted || sPacerReset)
    {
        sPacerStarted = 1;
        sPacerReset = 0;
        sLastVbl = now;
        sFieldAcc = 0;
        return DKR_LOGIC_RATE;
    }
    xb_at_game = XB_AT_VBLANK;
    while (now - sLastVbl < (DWORD)DKR_LOGIC_RATE)
    {
        D3DDevice_BlockUntilVerticalBlank();
        now = vblanks();
    }
    xb_at_game = XB_AT_GAME;
    if (now - sLastVbl > MAX_UPDATE_RATE)
    {
        /* a stall (a level loading outside the scene hooks): not the game's
         * time; count afresh from now */
        sLastVbl = now;
        sFieldAcc = 0;
        return DKR_LOGIC_RATE;
    }
    sFieldAcc += (int)(now - sLastVbl) * 60;
    sLastVbl = now;
    periods = sFieldAcc / hz;
    sFieldAcc -= periods * hz;
    return periods > 0 ? (int32_t)periods : 1;
}

/* dkr_scene.c: a level has loaded; the time it took is not game time */
void pc_retrace_reset(void)
{
    sPacerReset = 1;
}

/* ------------------------------------------------------------- data */

uint8_t *host_load_file(const char *name, uint32_t *size)
{
    char path[MAX_PATH];
    HANDLE h;
    DWORD n, got = 0;
    uint8_t *d;

    snprintf(path, sizeof(path), "%s%s", DATA_DIR, name);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    n = GetFileSize(h, NULL);
    d = (n != INVALID_FILE_SIZE && n > 0) ? (uint8_t *)malloc(n) : NULL;
    if (d && (!ReadFile(h, d, n, &got, NULL) || got != n))
    {
        free(d);
        d = NULL;
    }
    CloseHandle(h);
    *size = d ? n : 0;
    return d;
}

/* ------------------------------------------------------- per frame */

void dkr_audio_go(void);

/* ---------------------------------------------------------- watchdog */

volatile LONG xb_at_game, xb_at_audio, xb_audio_packets, xb_audio_gaps;
static volatile LONG sFramesShown;

static const char *where(LONG at)
{
    switch (at)
    {
        case XB_AT_GAME: return "in the game's code";
        case XB_AT_DRAW: return "drawing a frame";
        case XB_AT_PRESENT: return "in Present (the GPU)";
        case XB_AT_VBLANK: return "waiting for a vblank";
        case XB_AT_AUDIO_LOCK: return "waiting for the audio lock";
        case XB_AT_AUDIO_WAIT: return "waiting for a packet";
        case XB_AT_AUDIO_RENDER: return "handing a packet over";
        default: return "?";
    }
}

/* When no frame has been shown for three seconds, say where the two threads
 * are (and again every ten while it lasts): a hang's first clue. */
static DWORD WINAPI watchdog(LPVOID arg)
{
    LONG lastFrames = -1, lastPackets = 0;
    int still = 0;

    (void)arg;
    for (;;)
    {
        Sleep(500);
        if (sFramesShown != lastFrames)
        {
            lastFrames = sFramesShown;
            lastPackets = xb_audio_packets;
            still = 0;
            continue;
        }
        still++;
        if (still == 6 || (still > 6 && still % 20 == 6))
            xb_log("dkrx: HANG? no frame for %d s, frame %ld; game thread %s; audio thread %s, "
                   "%ld packets since (%s)", still / 2, (long)lastFrames, where(xb_at_game),
                   where(xb_at_audio), (long)(xb_audio_packets - lastPackets),
                   (xb_audio_packets - lastPackets) > 0 ? "sound still running" : "sound stopped too");
    }
    return 0;
}

void xb_watchdog_start(void)
{
    HANDLE th = CreateThread(NULL, 16 * 1024, watchdog, NULL, 0, NULL);

    if (th != NULL)
        CloseHandle(th);
}

/* gfx_xbox.c, after each frame it presents */
void xb_frame_done(void)
{
    static uint64_t last, longest;
    static LONG lastGaps;
    uint64_t now = host_time_ns();

    /* every ten seconds the sound ran short in: how often, and the longest
     * frame, its likely cause */
    if (last != 0 && now - last > longest)
        longest = now - last;
    last = now;
    if (sFramesShown % 600 == 599)
    {
        if (xb_audio_gaps != lastGaps)
            xb_log("dkrx: sound ran short %ld times in 10 s; longest frame %u ms", (long)(xb_audio_gaps - lastGaps),
                   (unsigned)(longest / 1000000));
        lastGaps = xb_audio_gaps;
        longest = 0;
    }
    sFramesShown++;
    dkr_audio_go();
    /* the game's sound, a little ahead of the output (xbox_audio.c) */
    dkr_audio_fill(AUDIO_AHEAD);
    xb_save_frame();
    if (xb_input_frame())
    {
        xb_log("dkrx: to the dashboard");
        XLaunchNewImage(NULL, NULL);
    }
}

/* ----------------------------------------------------------- start */

/* The game's single-precision maths with SSE's flush-to-zero, as the N64's
 * FPU (and the Dreamcast port's) flushes denormals; they cost a Pentium III
 * dearly. Every thread that runs game code calls this. */
void xbox_fpu_setup(void)
{
    unsigned int csr;

    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8000; /* FTZ */
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
}

/* A fault's address, to the debug channel, before the console restarts, and
 * the program addresses on the stack after it: most of them are the calls
 * that led there. imagebld moves the sections a little from where the .exe
 * has them; tools/xbox/xbe_symbolize.py allows for that and names them all
 * from out/<Config>/Diddy-Kong-Racing-X.exe's line tables:
 *
 *   python tools/xbox/xbe_symbolize.py < the debug output */
#define XBE_BASE 0x10000u
#define FAULT_STACK_WORDS 2048 /* 8 KB of stack above the fault */
#define FAULT_LINE_ADDRS 12

static LONG WINAPI fault_report(struct _EXCEPTION_POINTERS *ep)
{
    const CONTEXT *c = ep->ContextRecord;
    DWORD imageEnd = XBE_BASE + *(const DWORD *)(uintptr_t)(XBE_BASE + 0x10C); /* the XBE's SizeOfImage */
    const DWORD *sp = (const DWORD *)(uintptr_t)c->Esp;
    DWORD stackTop;
    char line[256];
    int i, n = 0, shown = 0;

    xb_log("dkrx: FAULT %08lx at %08lx (info %08lx %08lx) esp %08lx ebp %08lx",
           (unsigned long)ep->ExceptionRecord->ExceptionCode,
           (unsigned long)(uintptr_t)ep->ExceptionRecord->ExceptionAddress,
           (unsigned long)(ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0]
                                                                     : 0),
           (unsigned long)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1]
                                                                     : 0),
           (unsigned long)c->Esp, (unsigned long)c->Ebp);
    /* the thread's stack ends at its TIB's StackBase: read no further */
    __asm__ volatile("movl %%fs:4, %0" : "=r"(stackTop));
    for (i = 0; i < FAULT_STACK_WORDS && (DWORD)(uintptr_t)&sp[i] + 4 <= stackTop; i++)
    {
        DWORD v = sp[i];

        if (v < XBE_BASE + 0x1000 || v >= imageEnd)
            continue;
        if (shown == 0)
            n = snprintf(line, sizeof(line), "dkrx: FAULT stack:");
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %08lx", (unsigned long)v);
        if (++shown == FAULT_LINE_ADDRS)
        {
            xb_log("%s", line);
            shown = 0;
        }
    }
    if (shown)
        xb_log("%s", line);
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}

/* no game data: a red screen for a while, then the dashboard */
static void give_up(const char *why)
{
    int i;

    xb_log("dkrx: %s", why);
    for (i = 0; i < 60 * 6; i++)
    {
        xb_gfx_solid(0xFF600000u);
        D3DDevice_BlockUntilVerticalBlank();
    }
    XLaunchNewImage(NULL, NULL);
}

void __cdecl main(void)
{
    SetUnhandledExceptionFilter(fault_report);
    /* which build is running: a crash report is read against its symbols */
#ifdef DKRX_DEBUG_BUILD
    xb_log("dkrx: Diddy Kong Racing X, Debug build of %s", DKRX_BUILD_STAMP);
#else
    xb_log("dkrx: Diddy Kong Racing X, Release build of %s", DKRX_BUILD_STAMP);
#endif
    xbox_fpu_setup();
    xb_input_init(); /* enumeration is asynchronous: start it first */
    if (xb_gfx_init() != 0)
    {
        xb_log("dkrx: no Direct3D device");
        XLaunchNewImage(NULL, NULL);
    }
    if (GetFileAttributesA(DATA_DIR "assets.bin") == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(DATA_DIR "assets.lut.bin") == INVALID_FILE_ATTRIBUTES)
        give_up("no " DATA_DIR "assets.bin -- run setup.py, which builds it from your ROM onto the disc");
    xb_save_init();
    if (xb_audio_init() != 0)
        xb_log("dkrx: no sound");
    xb_watchdog_start();
    /* the pads found before the game first reads them: START held at
     * power-on opens its Controller Pak screen */
    xb_input_wait();

    /* the game's main loop: it does not return */
    dkr_boot();
    XLaunchNewImage(NULL, NULL);
}
