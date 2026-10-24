// dkr_host.h -- what the shared host layer (dkr_host.c, dkr_audio.c) asks of the
// platform under it: the Xbox (xbox_*.c) or the PC test harness
// (tools/xbox/hosttest). Plain C types only: the files that include this cannot
// include the N64 headers, whose u8/s32 and libc stand-ins clash with the C
// library's.
#ifndef DKR_HOST_H
#define DKR_HOST_H

#include <stdint.h>

#define HOST_PADS 4

// The N64 controller's button bits (include/PR/os_cont.h).
#define HOST_BTN_A 0x8000
#define HOST_BTN_B 0x4000
#define HOST_BTN_Z 0x2000
#define HOST_BTN_START 0x1000
#define HOST_BTN_UP 0x0800
#define HOST_BTN_DOWN 0x0400
#define HOST_BTN_LEFT 0x0200
#define HOST_BTN_RIGHT 0x0100
#define HOST_BTN_L 0x0020
#define HOST_BTN_R 0x0010
#define HOST_BTN_CUP 0x0008
#define HOST_BTN_CDOWN 0x0004
#define HOST_BTN_CLEFT 0x0002
#define HOST_BTN_CRIGHT 0x0001

// One N64 controller as the game sees it.
typedef struct {
    int connected;
    uint16_t buttons;
    int8_t stickX, stickY; // -80..80, up positive
} HostPad;

// --- the platform provides ---------------------------------------------------

void host_log(const char *line);

// A monotonic clock.
uint64_t host_time_ns(void);

// A file from the game's data folder (assets.bin, assets.lut.bin), whole, in
// memory from malloc; NULL when it cannot be read.
uint8_t *host_load_file(const char *name, uint32_t *size);

// The four N64 controller ports, this instant.
void host_pads_read(HostPad pads[HOST_PADS]);

// The rumble pak motor in an N64 port, on or off.
void host_rumble(int port, int on);

// The EEPROM image kept from the last run: nonzero when `buf` was filled.
int host_eeprom_load(uint8_t *buf, int size);
// The game wrote to the EEPROM; the platform stores dkr_eeprom_image() when it
// is ready to (the game writes a save as a burst of eight-byte blocks).
void host_eeprom_changed(void);

// The Controller Pak in an N64 port (time-trial ghosts): the image of
// dkr_cpak_size() bytes kept from the last run, as for the EEPROM. Nonzero when
// `buf` was filled; an empty pak is made otherwise.
int host_cpak_load(int port, uint8_t *buf, int size);
// The game changed a pak; the platform stores dkr_cpak_image(port) when ready.
void host_cpak_changed(int port);

// A recursive lock fencing the game's sound state against an audio thread
// (osSetIntMask, see dkr_host.c). The audio manager runs on the game's thread
// on both platforms now (dkr_audio.c says why), so both make these do nothing.
void host_audio_lock(void);
void host_audio_unlock(void);
int host_audio_lock_held(void); // by the calling thread

// --- the shared layer provides -------------------------------------------------

void dkr_printf(const char *fmt, ...);

// The EEPROM's 512 bytes, as the game last wrote them.
const uint8_t *dkr_eeprom_image(void);
int dkr_eeprom_size(void);

// A Controller Pak's image, as the game last left it.
const uint8_t *dkr_cpak_image(int port);
int dkr_cpak_size(void);

// Audio (dkr_audio.c). The output rate the game's mixer runs at.
#define DKR_AUDIO_RATE 22050
// On the game's thread, after each frame: has the game's audio manager make
// sound until `frames` stereo frames are waiting.
void dkr_audio_fill(int frames);
// The stereo frames made and waiting for the output.
int dkr_audio_waiting(void);
// On the output's thread: `frames` stereo frames (interleaved s16) of what is
// waiting, silence for any that are not. Returns how many were sound.
int dkr_audio_render(int16_t *out, int frames);
// The platform calls this once the game has drawn a frame: its audio set-up is
// done by then, and the manager may be ticked.
void dkr_audio_go(void);

#endif
