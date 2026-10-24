/* xbox_port.h -- the Xbox side of Diddy Kong Racing.
 *
 *   xbox_main.c    the entry point, the frame pacer, the clock, the log
 *   gfx_xbox.c     Direct3D: what the display-list interpreter draws
 *   xbox_audio.c   DirectSound: the game's mixer, streamed
 *   xbox_input.c   the controllers, as the N64's four, with rumble
 *   xbox_save.c    the EEPROM and Controller Paks, as a save game on the hard disk
 *
 * Under them, shared with the PC test harness: Platform/xbox/dkr_*.c (the N64
 * OS, the audio interface, the display-list interpreter); dkr_host.h is what
 * they ask of these files. */
#ifndef DKRX_XBOX_PORT_H
#define DKRX_XBOX_PORT_H

#include <stdint.h>

void xb_log(const char *fmt, ...);

/* gfx_xbox.c: 0 when the device is up; the display's rate (60, or 50 on a
 * PAL console without PAL60) */
int xb_gfx_init(void);
int xb_gfx_refresh(void);
void xb_gfx_solid(unsigned long argb);
/* gfx_xbox.c calls this after each frame it presents */
void xb_frame_done(void);
/* textures the GPU may still read: released two frames on */
void xb_gfx_release_pending(void);

int xb_audio_init(void);

void xb_input_init(void);
/* before the game starts: waits (up to a second) for the pads plugged in */
void xb_input_wait(void);
/* once a frame: the pads plugged in or out, rumble requests sent; nonzero
 * when the player asks for the dashboard (Back + Start + both triggers) */
int xb_input_frame(void);

/* Where the game and audio threads are, for xbox_main.c's watchdog: it logs
 * them when no frame has been shown for a few seconds. */
enum {
    XB_AT_GAME,         /* the game's own code */
    XB_AT_DRAW,         /* interpreting and drawing a display list */
    XB_AT_PRESENT,      /* in Direct3D's Present */
    XB_AT_VBLANK,       /* the pacer, waiting for a field */
    XB_AT_AUDIO_LOCK,   /* waiting for the audio lock */
    XB_AT_AUDIO_WAIT,   /* the audio thread, waiting for a packet to finish */
    XB_AT_AUDIO_RENDER, /* the audio thread, handing a packet over */
};
extern volatile LONG xb_at_game, xb_at_audio;
extern volatile LONG xb_audio_packets;
/* times the sound ran out mid-play and the stream took silence */
extern volatile LONG xb_audio_gaps;
void xb_watchdog_start(void);

void xb_save_init(void);
/* once a frame: writes the EEPROM and the paks out once the game has finished
 * writing them */
void xb_save_frame(void);

#endif /* DKRX_XBOX_PORT_H */
