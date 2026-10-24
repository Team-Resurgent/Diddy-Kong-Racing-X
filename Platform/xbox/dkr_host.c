// dkr_host.c -- the N64 OS underneath the game, for the Xbox and the PC test
// harness: the asset "ROM", the controllers, the EEPROM, rumble paks, timers and
// the rest of libultra's hardware layer.
//
// The Linux port's linux/reimpl.c, with what it left as constructive lies made
// real: four controllers, an EEPROM that is kept between runs, and in every
// controller a pak that is both a Controller Pak (kept between runs too: the
// time-trial ghosts) and a Rumble Pak (driving the pad's motors). What the
// platform supplies is in dkr_host.h.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dkr_host.h"

typedef signed char s8;
typedef signed int s32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

void dkr_printf(const char *fmt, ...) {
    char line[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    host_log(line);
}

// ---------------------------------------------------------------------------
// RSP microcode blobs (task-submission code references these; the display list
// and the audio command list are interpreted on the host instead)
// ---------------------------------------------------------------------------
u64 rspbootTextStart[1] = { 0 };
u64 rspbootTextEnd[1] = { 0 };
u64 aspMainTextStart[1] = { 0 };
u64 aspMainDataStart[1] = { 0 };
u64 rspF3DDKRXbusStart[1] = { 0 };
u64 rspF3DDKRDataXbusStart[1] = { 0 };
u64 rspF3DDKRFifoStart[1] = { 0 };
u64 rspF3DDKRDataFifoStart[1] = { 0 };

// ---------------------------------------------------------------------------
// Linker-script symbols (placed by the N64 linker script; static here)
// ---------------------------------------------------------------------------
u8 __ASSETS_LUT_START[1] = { 0 };
u8 __ASSETS_LUT_END[1] = { 0 };
u8 *__ROM_END; // cheat-menu checksum bound
u8 *main_BSS_START[1] = { 0 };

// ---------------------------------------------------------------------------
// The asset "ROM". On the N64 the asset LUT and the asset data sit in cart ROM
// after the code, bracketed by __ASSETS_LUT_START/__ASSETS_LUT_END. Here they
// are assets.lut.bin and assets.bin, the two files setup.py builds from the ROM
// (they are its bytes), read whole at boot; a dmacopy address is turned back
// into an offset into them relative to the two stand-in symbols above.
// The LUT is big-endian u32s, swapped once at load. Asset contents stay
// big-endian; the game swaps them where it parses them (include/pc_swap.h).
// ---------------------------------------------------------------------------
static u8 *sAssetLut = NULL;
static u32 sAssetLutSize = 0;
static u8 *sAssetsBin = NULL;
static u32 sAssetsBinSize = 0;

static u8 *load_file(const char *name, u32 *sizeOut) {
    u8 *buf = host_load_file(name, sizeOut);

    if (buf == NULL) {
        dkr_printf("ASSETS: cannot read %s (setup.py builds it from your ROM)\n", name);
        abort();
    }
    return buf;
}

static void assets_init(void) {
    u32 i;

    if (sAssetLut != NULL) {
        return;
    }
    sAssetLut = load_file("assets.lut.bin", &sAssetLutSize);
    sAssetsBin = load_file("assets.bin", &sAssetsBinSize);

    // LUT: entry count followed by offsets, all big-endian u32 -- swap in place.
    for (i = 0; i + 3 < sAssetLutSize; i += 4) {
        u8 *p = &sAssetLut[i];
        u8 t0 = p[0], t1 = p[1];
        p[0] = p[3];
        p[1] = p[2];
        p[2] = t1;
        p[3] = t0;
    }
    dkr_printf("ASSETS: lut %u bytes, data %u bytes\n", sAssetLutSize, sAssetsBinSize);
}

u32 pc_asset_lut_size(void) {
    assets_init();
    return sAssetLutSize;
}

// Byteswap a buffer of big-endian u32s in place (asset offset tables etc.).
void pc_swap32_buf(void *buf, u32 numBytes) {
    u8 *p = buf;
    u32 i;

    for (i = 0; i + 3 < numBytes; i += 4) {
        u8 t0 = p[i + 0], t1 = p[i + 1];
        p[i + 0] = p[i + 3];
        p[i + 1] = p[i + 2];
        p[i + 2] = t1;
        p[i + 3] = t0;
    }
}

// Same, for buffers of big-endian u16s.
void pc_swap16_buf(void *buf, u32 numBytes) {
    u8 *p = buf;
    u32 i;

    for (i = 0; i + 1 < numBytes; i += 2) {
        u8 t = p[i];
        p[i] = p[i + 1];
        p[i + 1] = t;
    }
}

// Byteswap a region of the loaded asset image in place, addressed the way the
// game addresses ROM. Audio's RAW16 sample data is big-endian s16 and is DMA'd
// straight out of this image on demand by __amDMA, so the swap has to happen
// once, here, at the source. (ADPCM waves are a byte stream and must NOT come
// through this. See bnkf.c.)
void pc_asset_swap16_region(u32 romOffset, u32 numBytes) {
    u32 offset;

    assets_init();

    if (romOffset < (u32) (uintptr_t) __ASSETS_LUT_END) {
        dkr_printf("AUDIO: raw16 swap outside the asset image (0x%X) -- skipped\n", romOffset);
        return;
    }
    offset = romOffset - (u32) (uintptr_t) __ASSETS_LUT_END;
    if (offset + numBytes > sAssetsBinSize) {
        dkr_printf("AUDIO: raw16 swap past end of assets (0x%X + 0x%X) -- skipped\n", offset, numBytes);
        return;
    }
    pc_swap16_buf(sAssetsBin + offset, numBytes);
}

void pc_dmacopy(u32 romOffset, u32 ramAddress, s32 numBytes) {
    assets_init();

    if (romOffset == (u32) (uintptr_t) __ASSETS_LUT_START) {
        if ((u32) numBytes > sAssetLutSize) {
            numBytes = sAssetLutSize;
        }
        memcpy((void *) (uintptr_t) ramAddress, sAssetLut, numBytes);
        return;
    }

    if (romOffset >= (u32) (uintptr_t) __ASSETS_LUT_END) {
        u32 offset = romOffset - (u32) (uintptr_t) __ASSETS_LUT_END;
        if (offset < sAssetsBinSize) {
            if (offset + numBytes > sAssetsBinSize) {
                dkr_printf("ASSETS: read past end (offset 0x%X + 0x%X > 0x%X), clamped\n", offset, numBytes,
                           sAssetsBinSize);
                numBytes = sAssetsBinSize - offset;
            }
            memcpy((void *) (uintptr_t) ramAddress, sAssetsBin + offset, numBytes);
            return;
        }
    }

    dkr_printf("ASSETS: dmacopy from unknown ROM address 0x%X (%d bytes) -- zero-filled\n", romOffset, numBytes);
    memset((void *) (uintptr_t) ramAddress, 0, numBytes);
}

// ---------------------------------------------------------------------------
// Boot globals (normally set up by the PIF/boot code)
// ---------------------------------------------------------------------------
void *osRomBase = (void *) 0xB0000000;
s32 osTvType = 1; // 0 = PAL, 1 = NTSC, 2 = MPAL
s32 osResetType = 0; // cold boot
s32 osAppNMIBuffer[16] = { 0 };

// Anti-piracy check: camera.c reads this raw cart-domain address and expects
// the low half to be 0x8965; anything else triggers the piracy path.
s32 D_B0000578 = 0x8965;

// ---------------------------------------------------------------------------
// Math library data (was libultra/src/gu/libm_vals.s)
// ---------------------------------------------------------------------------
float __libm_qnan_f = __builtin_nanf("");

// ---------------------------------------------------------------------------
// SI bus -- controllers, the EEPROM, controller paks, rumble.
// The N64 talks to all of these through PIF-RAM DMA over the Serial Interface
// (the libultra cont*/pfs*/eeprom*/motor files, left out of this build); here
// each call is answered directly.
// ---------------------------------------------------------------------------
#define PC_CONT_TYPE_NORMAL 0x0005
#define PC_CONT_NO_RESPONSE 0x8
#define PC_PFS_ERR_NOPACK 1
#define PC_PFS_ERR_ID_FATAL 10

typedef struct {
    u16 type;
    u8 status;
    u8 error;
} PCContStatus;

typedef struct {
    u16 button;
    s8 stick_x;
    s8 stick_y;
    u8 error;
} PCContPad;

// The front of libultra's OSPfs (include/PR/os_pfs.h): osMotorInit records the
// port in it, as the real one does, for osMotorStart/Stop.
typedef struct {
    int status;
    void *queue;
    int channel;
} PCPfsHead;

extern s32 osSendMesg(void *mq, void *msg, s32 flags);

// Which N64 ports have a controller. Port 0 always does: the game counts it as
// its controller, and a pad plugged in later then simply starts working.
static u8 connected_pattern(const HostPad pads[HOST_PADS]) {
    u8 pattern = 1;
    s32 i;

    for (i = 0; i < HOST_PADS; i++) {
        if (pads[i].connected) {
            pattern |= 1 << i;
        }
    }
    return pattern;
}

s32 osContInit(void *mq, u8 *bitpattern, PCContStatus *status) {
    HostPad pads[HOST_PADS];
    s32 i;

    host_pads_read(pads);
    *bitpattern = connected_pattern(pads);
    for (i = 0; i < HOST_PADS; i++) {
        s32 present = (*bitpattern >> i) & 1;

        status[i].type = present ? PC_CONT_TYPE_NORMAL : 0;
        status[i].status = 0;
        status[i].error = present ? 0 : PC_CONT_NO_RESPONSE;
    }
    return 0;
}

s32 osContStartReadData(void *mq) {
    // Poll "completes" instantly: post the done-message the game waits for.
    osSendMesg(mq, NULL, 0 /* OS_MESG_NOBLOCK */);
    return 0;
}

void osContGetReadData(PCContPad *pads) {
    HostPad host[HOST_PADS];
    u8 pattern;
    s32 i;

    host_pads_read(host);
    pattern = connected_pattern(host);
    for (i = 0; i < HOST_PADS; i++) {
        pads[i].button = host[i].connected ? host[i].buttons : 0;
        pads[i].stick_x = host[i].connected ? host[i].stickX : 0;
        pads[i].stick_y = host[i].connected ? host[i].stickY : 0;
        pads[i].error = ((pattern >> i) & 1) ? 0 : PC_CONT_NO_RESPONSE;
    }
}

// EEPROM_TYPE_4K is 512 bytes / 64 eight-byte blocks: the whole save (both
// adventure slots, times and options). Loaded from the platform's copy at
// first use, and handed back to it after every write. Built with
// -DEEPROM_PRESET_100 (setup.py --unlock-all), a console with no save yet starts
// from the 100%-completion image in include/eeprom_preset.h instead of erased.
#define PC_EEPROM_BLOCKS 64
#define PC_EEPROM_BYTES (PC_EEPROM_BLOCKS * 8)

#ifdef EEPROM_PRESET_100
#include "eeprom_preset.h"
#endif

static u8 gEeprom[PC_EEPROM_BYTES];
static s32 gEepromReady = 0;

static void eeprom_init(void) {
    if (gEepromReady) {
        return;
    }
    gEepromReady = 1;
    if (host_eeprom_load(gEeprom, PC_EEPROM_BYTES)) {
        dkr_printf("EEPROM: the save was loaded\n");
        return;
    }
#ifdef EEPROM_PRESET_100
    memcpy(gEeprom, gEepromPreset, PC_EEPROM_BYTES);
    dkr_printf("EEPROM: no save yet; starting from the 100%% image\n");
#else
    memset(gEeprom, 0xFF, PC_EEPROM_BYTES); // erased
    dkr_printf("EEPROM: no save yet\n");
#endif
}

const uint8_t *dkr_eeprom_image(void) {
    eeprom_init();
    return gEeprom;
}

int dkr_eeprom_size(void) {
    return PC_EEPROM_BYTES;
}

s32 osEepromProbe(void *mq) {
    eeprom_init();
    return 1; // EEPROM_TYPE_4K -- present
}

s32 osEepromRead(void *mq, u8 address, u8 *buffer) {
    eeprom_init();
    if (address >= PC_EEPROM_BLOCKS) {
        memset(buffer, 0xFF, 8);
        return 0;
    }
    memcpy(buffer, &gEeprom[address * 8], 8);
    return 0;
}

s32 osEepromWrite(void *mq, u8 address, u8 *buffer) {
    eeprom_init();
    if (address < PC_EEPROM_BLOCKS && memcmp(&gEeprom[address * 8], buffer, 8) != 0) {
        memcpy(&gEeprom[address * 8], buffer, 8);
        host_eeprom_changed();
    }
    return 0;
}

// Paks. On the N64 a controller holds a Controller Pak or a Rumble Pak, not
// both. Here every connected controller holds one that is both: a Controller
// Pak (a file system, where the game keeps time-trial ghosts) whose motor
// also turns (osMotorInit succeeds). src/save_data.c is told not to take the
// motor for proof that there is no file system (get_si_device_status).
//
// The pak is the file system as the game sees it through libultra's osPfs*
// calls -- sixteen notes in 123 pages of 256 bytes, as a real one holds --
// kept as a plain image (CPak) rather than the real one's inode tables: notes
// lie packed in `data` in the order they were made, closed up when one is
// deleted. The platform keeps each port's image between runs.
#define PC_PFS_ERR_INVALID 5
#define PC_PFS_DATA_FULL 7
#define PC_PFS_DIR_FULL 8
#define PC_PFS_ERR_EXIST 9
#define PC_PFS_INITIALIZED 0x1
#define PC_PFS_MOTOR_INITIALIZED 0x8

#define CPAK_NOTES 16
#define CPAK_PAGE 256
#define CPAK_PAGES 123
#define CPAK_MAGIC "DKRXCPAK"
#define CPAK_VERSION 1

typedef struct {
    u32 used;
    u32 gameCode;
    u16 companyCode;
    u8 extName[4];
    u8 gameName[16];
    u8 pad[2];
    u32 size;   // bytes: whole pages
    u32 offset; // into data
} CPakNote;

typedef struct {
    char magic[8];
    u32 version;
    u32 pad;
    CPakNote notes[CPAK_NOTES];
    u8 data[CPAK_PAGES * CPAK_PAGE];
} CPak;

// libultra's OSPfsState (include/PR/os_pfs.h), as the game reads it.
typedef struct {
    u32 file_size;
    u32 game_code;
    u16 company_code;
    char ext_name[4];
    char game_name[16];
} PCPfsState;

static CPak gCPak[HOST_PADS];
static s32 gCPakReady[HOST_PADS];

static s32 pak_present(s32 channel);

static void cpak_format(CPak *pak) {
    memset(pak, 0, sizeof(*pak));
    memcpy(pak->magic, CPAK_MAGIC, sizeof(pak->magic));
    pak->version = CPAK_VERSION;
}

// A port's pak, loaded the first time the game asks for it.
static CPak *cpak_of(s32 channel) {
    CPak *pak;

    if (channel < 0 || channel >= HOST_PADS) {
        return NULL;
    }
    pak = &gCPak[channel];
    if (!gCPakReady[channel]) {
        gCPakReady[channel] = 1;
        if (!host_cpak_load(channel, (uint8_t *) pak, sizeof(*pak)) ||
            memcmp(pak->magic, CPAK_MAGIC, sizeof(pak->magic)) != 0 || pak->version != CPAK_VERSION) {
            cpak_format(pak);
        }
    }
    return pak;
}

// The pak an osPfs call is about, once osPfsInit has opened it.
static s32 cpak_open(void *pfs, CPak **pak) {
    PCPfsHead *head = pfs;

    if (!pak_present(head->channel)) {
        return PC_PFS_ERR_NOPACK;
    }
    if (!(head->status & PC_PFS_INITIALIZED)) {
        return PC_PFS_ERR_INVALID;
    }
    *pak = cpak_of(head->channel);
    return 0;
}

static u32 cpak_bytes_used(const CPak *pak) {
    u32 used = 0;
    s32 i;

    for (i = 0; i < CPAK_NOTES; i++) {
        if (pak->notes[i].used) {
            used += pak->notes[i].size;
        }
    }
    return used;
}

static s32 cpak_find(const CPak *pak, u16 companyCode, u32 gameCode, const u8 *gameName, const u8 *extName) {
    s32 i;

    for (i = 0; i < CPAK_NOTES; i++) {
        const CPakNote *note = &pak->notes[i];

        if (note->used && note->companyCode == companyCode && note->gameCode == gameCode &&
            memcmp(note->gameName, gameName, sizeof(note->gameName)) == 0 &&
            memcmp(note->extName, extName, sizeof(note->extName)) == 0) {
            return i;
        }
    }
    return -1;
}

static void cpak_changed(const void *pfs) {
    host_cpak_changed(((const PCPfsHead *) pfs)->channel);
}

const uint8_t *dkr_cpak_image(int port) {
    CPak *pak = cpak_of(port);

    return (const uint8_t *) (pak != NULL ? pak : &gCPak[0]);
}

int dkr_cpak_size(void) {
    return (int) sizeof(CPak);
}

s32 osPfsIsPlug(void *mq, u8 *pattern) {
    HostPad pads[HOST_PADS];
    s32 i;

    host_pads_read(pads);
    *pattern = 0;
    for (i = 0; i < HOST_PADS; i++) {
        if (pads[i].connected) {
            *pattern |= 1 << i;
        }
    }
    return 0;
}

static s32 pak_present(s32 channel) {
    HostPad pads[HOST_PADS];

    if (channel < 0 || channel >= HOST_PADS) {
        return 0;
    }
    host_pads_read(pads);
    return pads[channel].connected;
}

s32 osPfsInit(void *mq, void *pfs, s32 channel) {
    PCPfsHead *head = pfs;

    if (!pak_present(channel)) {
        return PC_PFS_ERR_NOPACK;
    }
    cpak_of(channel);
    head->status = PC_PFS_INITIALIZED;
    head->queue = mq;
    head->channel = channel;
    return 0;
}

s32 osPfsFileState(void *pfs, s32 fileNo, void *state) {
    PCPfsState *out = state;
    CPak *pak;
    CPakNote *note;
    s32 ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    if (fileNo < 0 || fileNo >= CPAK_NOTES || !pak->notes[fileNo].used) {
        return PC_PFS_ERR_INVALID;
    }
    note = &pak->notes[fileNo];
    out->file_size = note->size;
    out->game_code = note->gameCode;
    out->company_code = note->companyCode;
    memcpy(out->ext_name, note->extName, sizeof(out->ext_name));
    memcpy(out->game_name, note->gameName, sizeof(out->game_name));
    return 0;
}

s32 osPfsReadWriteFile(void *pfs, s32 fileNo, u8 flag, s32 offset, s32 size, u8 *data) {
    CPak *pak;
    CPakNote *note;
    s32 ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    if (fileNo < 0 || fileNo >= CPAK_NOTES || !pak->notes[fileNo].used) {
        return PC_PFS_ERR_INVALID;
    }
    note = &pak->notes[fileNo];
    if (offset < 0 || size < 0 || (u32) offset + (u32) size > note->size) {
        return PC_PFS_ERR_INVALID;
    }
    if (flag == 0) { // PFS_READ
        memcpy(data, &pak->data[note->offset + offset], size);
    } else if (memcmp(&pak->data[note->offset + offset], data, size) != 0) {
        memcpy(&pak->data[note->offset + offset], data, size);
        cpak_changed(pfs);
    }
    return 0;
}

s32 osPfsNumFiles(void *pfs, s32 *maxFiles, s32 *filesUsed) {
    CPak *pak;
    s32 i, ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    *maxFiles = CPAK_NOTES;
    *filesUsed = 0;
    for (i = 0; i < CPAK_NOTES; i++) {
        *filesUsed += pak->notes[i].used != 0;
    }
    return 0;
}

s32 osPfsFreeBlocks(void *pfs, s32 *bytesNotUsed) {
    CPak *pak;
    s32 ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    *bytesNotUsed = (s32) (sizeof(pak->data) - cpak_bytes_used(pak));
    return 0;
}

s32 osPfsReFormat(void *pfs, void *mq, s32 channel) {
    PCPfsHead *head = pfs;
    CPak *pak;

    if (!pak_present(channel) || (pak = cpak_of(channel)) == NULL) {
        return PC_PFS_ERR_NOPACK;
    }
    cpak_format(pak);
    head->status = PC_PFS_INITIALIZED;
    head->queue = mq;
    head->channel = channel;
    host_cpak_changed(channel);
    return 0;
}

s32 osPfsFindFile(void *pfs, u16 companyCode, u32 gameCode, u8 *gameName, u8 *extName, s32 *fileNo) {
    CPak *pak;
    s32 i, ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    i = cpak_find(pak, companyCode, gameCode, gameName, extName);
    if (i < 0) {
        return PC_PFS_ERR_INVALID;
    }
    *fileNo = i;
    return 0;
}

s32 osPfsDeleteFile(void *pfs, u16 companyCode, u32 gameCode, u8 *gameName, u8 *extName) {
    CPak *pak;
    CPakNote gone;
    s32 i, ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    i = cpak_find(pak, companyCode, gameCode, gameName, extName);
    if (i < 0) {
        return PC_PFS_ERR_INVALID;
    }
    gone = pak->notes[i];
    memset(&pak->notes[i], 0, sizeof(pak->notes[i]));
    // the notes made after it close up the gap
    memmove(&pak->data[gone.offset], &pak->data[gone.offset + gone.size],
            sizeof(pak->data) - (gone.offset + gone.size));
    memset(&pak->data[sizeof(pak->data) - gone.size], 0, gone.size);
    for (i = 0; i < CPAK_NOTES; i++) {
        if (pak->notes[i].used && pak->notes[i].offset > gone.offset) {
            pak->notes[i].offset -= gone.size;
        }
    }
    cpak_changed(pfs);
    return 0;
}

s32 osPfsChecker(void *pfs) {
    CPak *pak;

    return cpak_open(pfs, &pak);
}

s32 osPfsAllocateFile(void *pfs, u16 companyCode, u32 gameCode, u8 *gameName, u8 *extName, s32 size, s32 *fileNo) {
    CPak *pak;
    CPakNote *note;
    u32 bytes, used;
    s32 i, ret = cpak_open(pfs, &pak);

    if (ret != 0) {
        return ret;
    }
    if (size <= 0) {
        return PC_PFS_ERR_INVALID;
    }
    if (cpak_find(pak, companyCode, gameCode, gameName, extName) >= 0) {
        return PC_PFS_ERR_EXIST;
    }
    for (i = 0; i < CPAK_NOTES && pak->notes[i].used; i++) {}
    if (i == CPAK_NOTES) {
        return PC_PFS_DIR_FULL;
    }
    bytes = ((u32) size + CPAK_PAGE - 1) / CPAK_PAGE * CPAK_PAGE;
    used = cpak_bytes_used(pak);
    if (used + bytes > sizeof(pak->data)) {
        return PC_PFS_DATA_FULL;
    }
    note = &pak->notes[i];
    note->used = 1;
    note->gameCode = gameCode;
    note->companyCode = companyCode;
    memcpy(note->extName, extName, sizeof(note->extName));
    memcpy(note->gameName, gameName, sizeof(note->gameName));
    note->size = bytes;
    note->offset = used; // the notes lie packed, so the free pages are the end
    memset(&pak->data[used], 0, bytes);
    *fileNo = i;
    cpak_changed(pfs);
    return 0;
}

s32 osMotorInit(void *mq, void *pfs, s32 channel) {
    PCPfsHead *head = pfs;

    if (!pak_present(channel)) {
        return PC_PFS_ERR_NOPACK;
    }
    // the same OSPfs serves the pak's files: an opened file system stays open
    if (head->channel != channel) {
        head->status = 0;
    }
    head->status |= PC_PFS_MOTOR_INITIALIZED;
    head->queue = mq;
    head->channel = channel;
    return 0;
}

// The game drives the motor as the N64's rumble pak wants it: on and off,
// pulsed at the frame rate for the weaker effects (src/save_data.c).
s32 osMotorStart(void *pfs) {
    host_rumble(((PCPfsHead *) pfs)->channel, 1);
    return 0;
}

s32 osMotorStop(void *pfs) {
    host_rumble(((PCPfsHead *) pfs)->channel, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Debug printing (was src/isv_print.c writing to the IS-Viewer at 0xB3FF0000)
// ---------------------------------------------------------------------------
void isv_printf(const char *fmt, ...) {
    char line[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    host_log(line);
}

// ---------------------------------------------------------------------------
// Cache management (nothing to manage on the host)
// ---------------------------------------------------------------------------
void osWritebackDCache(void *vaddr, s32 size) {}
void osWritebackDCacheAll(void) {}
void osInvalDCache(void *vaddr, s32 size) {}
void osInvalICache(void *vaddr, s32 size) {}

// ---------------------------------------------------------------------------
// Interrupts.
//
// __osDisableInt/__osRestoreInt guard libultra's message queues; those stay
// no-ops, the queues the two threads use are their own.
//
// osSetIntMask is different. On the N64 the game uses it to fence its sound
// state against the audio thread (src/audiosfx.c, and libultra's audio event
// queue): OS_IM_NONE enters, the restoring call leaves. The audio manager runs
// on a thread of its own here too (dkr_audio.c), so this is the platform's
// recursive lock, which that thread holds while it runs the synthesizer -- the
// Dreamcast port's arrangement (dreamcast/reimpl.c).
// ---------------------------------------------------------------------------
#define PC_OS_IM_NONE 0x00000001u

s32 __osDisableInt(void) {
    return 0;
}
void __osRestoreInt(s32 mask) {}

u32 osSetIntMask(u32 mask) {
    // The restoring call hands back what this returned (0). Only leave when this
    // thread holds the lock, so a stray restore can't underflow it.
    if (mask == PC_OS_IM_NONE) {
        host_audio_lock();
    } else if (host_audio_lock_held()) {
        host_audio_unlock();
    }
    return 0;
}

void pc_audio_lock(void) {
    host_audio_lock();
}
void pc_audio_unlock(void) {
    host_audio_unlock();
}

// ---------------------------------------------------------------------------
// Timer / CP0 / FPU registers
// ---------------------------------------------------------------------------
// The N64 Count register ticks at 46.875 MHz (CPU clock / 2): exactly 3/64 ticks
// per nanosecond.
u32 osGetCount(void) {
    return (u32) (host_time_ns() * 3 / 64);
}
void __osSetCompare(u32 value) {}
u32 __osGetSR(void) {
    return 0;
}
void __osSetSR(u32 value) {}
u32 __osSetFpcCsr(u32 value) {
    return 0;
}

// ---------------------------------------------------------------------------
// TLB (virtual == physical as far as the game cares)
// ---------------------------------------------------------------------------
u32 __osProbeTLB(void *vaddr) {
    return 0;
}
void osMapTLBRdb(void) {}

// ---------------------------------------------------------------------------
// Exception / hardware-interrupt plumbing (was exceptasm.s data)
// ---------------------------------------------------------------------------
typedef struct {
    unsigned int inst1;
    unsigned int inst2;
    unsigned int inst3;
    unsigned int inst4;
} __osExceptionVector;
__osExceptionVector __osExceptionPreamble[1] = { { 0, 0, 0, 0 } };

// Pre-2.0J layout (VERSION_G): a plain array of handlers, with no stack frame.
s32 (*__osHwIntTable[8])(void) = { 0 };

// ---------------------------------------------------------------------------
// Thread context switching (was exceptasm.s code). The game's own threads are
// not run: its main thread is called directly (src/thread3_main.c), and the
// audio thread's work is dkr_audio.c's.
// ---------------------------------------------------------------------------
void __osEnqueueThread(void **queue, void *thread) {}
void __osEnqueueAndYield(void **queue) {}
void *__osPopThread(void **queue) {
    return 0;
}
void __osDispatchThread(void) {}
void __osCleanupThread(void) {}
