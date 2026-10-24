// dkr_audio.c -- the N64's audio interface (AI), and the game's audio manager
// kept a little ahead of the platform's sound output.
//
// On the N64 the audio thread wakes every retrace, has the synthesizer build a
// command list for the next chunk of sound, runs it on the RSP and queues the
// result on the AI. Here the game's own thread does that once a frame
// (dkr_audio_fill, after the frame is shown), as the Linux port does:
// am_audio_frame_pc (src/audiomgr.c) is that retrace with the RSP replaced by
// the command-list interpreter in linux/audio_hle.c, and what it makes arrives
// through osAiSetNextBuffer into a ring. The platform's output (a thread of its
// own on the Xbox) only drains the ring (dkr_audio_render).
//
// Why not tick the manager from the output's thread, as the Dreamcast port
// does? The game's sound code was written for the N64, where the audio thread
// can interrupt the game but the game can never interrupt it. It fences some
// of its changes with osSetIntMask, but not all: run truly in parallel, the
// music player's event queue was emptied under it now and then, and the
// player then spun for ever holding the lock (a hang on loading, most often
// when the menus were hurried through). On one thread there is nothing to race.
// The ring and the output's own buffers carry the sound through the game's
// frames, and through all but the longest level loads.

#include <stdint.h>
#include <string.h>

#include "dkr_host.h"

// src/audiomgr.c: one retrace of the audio manager, and the samples a tick
// aims for (outputRate * 2 / 60 = 735: the N64's audio thread runs once per
// game frame of two video fields).
extern void am_audio_frame_pc(void);
extern unsigned int frameSize;

#define BYTES_PER_FRAME 4  // stereo s16
#define RING_FRAMES 16384  // a power of two; 0.74 s
#define MAX_TICKS 12       // per fill: never spin if the manager makes nothing

static int16_t sRing[RING_FRAMES * 2];
static volatile uint32_t sHead, sTail; // free-running frame counts
static volatile int sStarted;          // osAiSetFrequency has been called
static volatile int sLive;             // and the game has finished setting up

static uint32_t ring_frames(void) {
    return sHead - sTail;
}

// Called once from amCreateAudioMgr with OUTPUT_RATE (and again by a piracy
// check with nonsense, which is ignored). The manager derives its whole frame
// schedule from what this returns.
int32_t osAiSetFrequency(uint32_t frequency) {
    (void) frequency;
    sStarted = 1;
    return DKR_AUDIO_RATE;
}

// The PCM a tick of the manager made (interleaved stereo).
void osAiSetNextBuffer(void *buf, uint32_t size) {
    const int16_t *src = buf;
    uint32_t frames = size / BYTES_PER_FRAME;
    uint32_t head = sHead;
    uint32_t i;

    if (buf == NULL || frames == 0) {
        return;
    }
    if (frames > RING_FRAMES - ring_frames()) {
        return; // the output has stopped draining: drop rather than overrun
    }
    for (i = 0; i < frames; i++) {
        uint32_t at = ((head + i) & (RING_FRAMES - 1)) * 2;

        sRing[at] = src[i * 2];
        sRing[at + 1] = src[i * 2 + 1];
    }
    __asm__ volatile("" ::: "memory"); // the samples land before the count
    sHead = head + frames;
}

// Bytes still to play: the feedback __amHandleFrameMsg sizes the next tick from,
//
//     frameSamples = (16 + (frameSize - osAiGetLength()/4 + 96)) & ~0xf
//
// On the N64 it is what is left of the AI's playing buffer, never more than a
// frame; a deeper backlog would drive frameSamples negative, which the game's
// unsigned clamp then waves through (linux/audio.c has the history). So it is
// saturated at one frame, as the hardware is.
uint32_t osAiGetLength(void) {
    uint32_t used = ring_frames() * BYTES_PER_FRAME;
    uint32_t oneFrame = frameSize * BYTES_PER_FRAME;

    if (frameSize == 0) {
        return used;
    }
    return (used > oneFrame) ? oneFrame : used;
}

// The platform's frame loop says when the game is past its set-up (the first
// frame it draws): before that the audio manager may be half made.
void dkr_audio_go(void) {
    if (sStarted) {
        sLive = 1;
    }
}

void dkr_audio_fill(int frames) {
    int ticks = 0;

    if (!sLive || frameSize == 0) {
        return;
    }
    while (ring_frames() < (uint32_t) frames && ticks < MAX_TICKS) {
        am_audio_frame_pc();
        ticks++;
    }
}

int dkr_audio_waiting(void) {
    return (int) ring_frames();
}

int dkr_audio_render(int16_t *out, int frames) {
    uint32_t tail = sTail;
    uint32_t have = ring_frames();
    int i;

    if (have > (uint32_t) frames) {
        have = frames;
    }
    for (i = 0; i < (int) have; i++) {
        uint32_t at = ((tail + i) & (RING_FRAMES - 1)) * 2;

        out[i * 2] = sRing[at];
        out[i * 2 + 1] = sRing[at + 1];
    }
    // short (the game is starting, or held up): silence
    memset(out + have * 2, 0, (frames - have) * BYTES_PER_FRAME);
    __asm__ volatile("" ::: "memory"); // read before the count frees them
    sTail = tail + have;
    return (int) have;
}
