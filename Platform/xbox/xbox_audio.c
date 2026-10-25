/* xbox_audio.c -- the game's sound through a DirectSound stream.
 *
 * A thread of its own keeps five packets of 22050 Hz stereo queued on the
 * stream (116 ms), each refilled with what the game's N64 audio manager has
 * made (dkr_audio_render), which the game thread keeps a packet ahead of it
 * after every frame (xbox_main.c, dkr_audio_fill). This thread runs none of
 * the game's code. A packet is refilled only once a whole packet of sound is
 * waiting: when a frame runs long the queue plays down rather than taking
 * silence, and the game catches it up after. Only with one packet left to
 * play (the game is loading, or has not started its sound) does the thread
 * make up the rest with silence. */
#include "xbox_xtl.h"
#include <dsound.h>

#include <string.h>

#include "dkr_host.h"
#include "xbox_port.h"

#define PACKET 512 /* frames a packet plays: 23 ms */
#define PACKETS 5

static LPDIRECTSOUND sDS;
static volatile LONG sQueued = PACKETS * PACKET; /* frames on the stream, for host_audio_queued */
static LPDIRECTSOUNDSTREAM sStream;
static int16_t *sPcm[PACKETS];
static DWORD sStatus[PACKETS];
static HANDLE sDone[PACKETS];

/* ------------------------------------------------------------- the lock */

/* The game's audio manager runs on the game's own thread (dkr_audio.c), so
 * there is nothing for osSetIntMask to fence. */
void host_audio_lock(void)
{
}

void host_audio_unlock(void)
{
}

int host_audio_lock_held(void)
{
    return 0;
}

/* what the stream still has to play: the game thread keeps this and the ring
 * together a packet ahead (dkr_audio_fill) */
int host_audio_queued(void)
{
    return (int)sQueued;
}

/* ----------------------------------------------------------- the stream */

void xbox_fpu_setup(void);

static void submit(int k)
{
    XMEDIAPACKET xp;

    memset(&xp, 0, sizeof(xp));
    xp.pvBuffer = sPcm[k];
    xp.dwMaxSize = PACKET * 4;
    xp.pdwStatus = &sStatus[k];
    xp.hCompletionEvent = sDone[k];
    sStatus[k] = XMEDIAPACKET_STATUS_PENDING;
    IDirectSoundStream_Process(sStream, &xp, NULL);
}

static DWORD WINAPI audio_thread(LPVOID arg)
{
    int head = 0, queued = PACKETS, lastFull = 0;

    (void)arg;
    xbox_fpu_setup();
    for (;;)
    {
        /* packets complete in order: wait a little for the oldest */
        xb_at_audio = XB_AT_AUDIO_WAIT;
        WaitForSingleObject(sDone[head], 4);
        xb_at_audio = XB_AT_AUDIO_RENDER;
        DirectSoundDoWork();
        while (queued > 0 && sStatus[head] != XMEDIAPACKET_STATUS_PENDING)
        {
            head = (head + 1) % PACKETS;
            queued--;
        }
        sQueued = queued * PACKET;
        /* refill with a packet of the game's sound, or, when the stream is
         * down to its last packet, with what there is and silence */
        while (queued < PACKETS && (dkr_audio_waiting() >= PACKET || queued <= 1))
        {
            int k = (head + queued) % PACKETS;
            int have = dkr_audio_render(sPcm[k], PACKET);

            if (have < PACKET && lastFull)
                InterlockedIncrement(&xb_audio_gaps);
            lastFull = have == PACKET;
            submit(k);
            queued++;
            xb_audio_packets++;
        }
        sQueued = queued * PACKET;
    }
    return 0;
}

int xb_audio_init(void)
{
    WAVEFORMATEX wfx;
    DSSTREAMDESC desc;
    HANDLE th;
    int k;

    if (FAILED(DirectSoundCreate(NULL, &sDS, NULL)))
        return -1;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = DKR_AUDIO_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = DKR_AUDIO_RATE * 4;
    memset(&desc, 0, sizeof(desc));
    desc.dwMaxAttachedPackets = PACKETS;
    desc.lpwfxFormat = &wfx;
    if (FAILED(DirectSoundCreateStream(&desc, &sStream)))
    {
        sStream = NULL;
        return -1;
    }
    for (k = 0; k < PACKETS; k++)
    {
        sPcm[k] = (int16_t *)XPhysicalAlloc(PACKET * 4, MAXULONG_PTR, 0, PAGE_READWRITE);
        sDone[k] = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (sPcm[k] == NULL || sDone[k] == NULL)
        {
            sStream = NULL;
            return -1;
        }
        memset(sPcm[k], 0, PACKET * 4);
    }
    for (k = 0; k < PACKETS; k++)
        submit(k);
    th = CreateThread(NULL, 256 * 1024, audio_thread, NULL, 0, NULL);
    if (th == NULL)
    {
        sStream = NULL;
        return -1;
    }
    SetThreadPriority(th, THREAD_PRIORITY_TIME_CRITICAL);
    CloseHandle(th);
    xb_log("audio: %d Hz stereo, %d ms of stream", DKR_AUDIO_RATE, PACKETS * PACKET * 1000 / DKR_AUDIO_RATE);
    return 0;
}
