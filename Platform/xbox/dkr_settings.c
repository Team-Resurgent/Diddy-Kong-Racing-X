// dkr_settings.c -- the port's own settings (dkr_settings.h): their names for
// the menu, and their stored form, which the platform keeps on the hard disk
// (host_settings_load, host_settings_changed in dkr_host.h).

#include <string.h>

#include "dkr_gfx.h"
#include "dkr_host.h"
#include "dkr_settings.h"

#define SETTINGS_MAGIC "DKRS"
#define SETTINGS_VERSION 3 // 2: widescreen, first of all; 3: the controls
#define SETTINGS_SLOTS 32 // room for settings to come, in the same file
#define CONTROL_SLOTS 16  // and for the controls
#define SETTINGS_V2_SIZE 40 // the file before the controls (versions 1 and 2)

typedef struct {
    char magic[4];
    unsigned char version;
    unsigned char count;
    unsigned char pad[2];
    unsigned char values[SETTINGS_SLOTS];
    unsigned int controls[CONTROL_SLOTS]; // the inputs on each N64 button (dkr_control)
} SettingsImage;

typedef struct {
    const char *name;
    int count;
    const char *values[4];
} SettingInfo;

static const SettingInfo sInfo[DKR_SET_COUNT] = {
    [DKR_SET_WIDESCREEN] = { "Widescreen", 2, { "Off", "On" } },
    [DKR_SET_CAMERA_WALLS] = { "Camera at walls", 2, { "Keep out", "Original" } },
    [DKR_SET_PLANE_STICK] = { "Plane up/down", 2, { "Normal", "Inverted" } },
    [DKR_SET_RUMBLE] = { "Rumble", 4, { "High", "Medium", "Low", "Off" } },
    [DKR_SET_FRAME_RATE] = { "Frame rate", 2, { "60 FPS", "30 FPS" } },
    [DKR_SET_TEXTURES] = { "Textures", 2, { "Smooth", "Sharp" } },
};

// The N64 buttons as the controls page lists them, with the HOST_BTN_* bit
// each is and the Xbox inputs it starts on (xbox_input.c's table).
static const struct {
    const char *name;
    unsigned short bit;
    unsigned int inputs;
} sPads[DKR_PAD_COUNT] = {
    [DKR_PAD_A] = { "A", HOST_BTN_A, DKR_IN_BIT(DKR_IN_A) },
    [DKR_PAD_B] = { "B", HOST_BTN_B, DKR_IN_BIT(DKR_IN_B) },
    [DKR_PAD_Z] = { "Z", HOST_BTN_Z, DKR_IN_BIT(DKR_IN_X) | DKR_IN_BIT(DKR_IN_LT) },
    [DKR_PAD_R] = { "R", HOST_BTN_R, DKR_IN_BIT(DKR_IN_RT) },
    [DKR_PAD_L] = { "L", HOST_BTN_L, DKR_IN_BIT(DKR_IN_LCLICK) }, // (the game never reads it)
    [DKR_PAD_START] = { "Start", HOST_BTN_START, DKR_IN_BIT(DKR_IN_START) },
    [DKR_PAD_CUP] = { "C-Up", HOST_BTN_CUP, DKR_IN_BIT(DKR_IN_Y) | DKR_IN_BIT(DKR_IN_RUP) },
    [DKR_PAD_CDOWN] = { "C-Down", HOST_BTN_CDOWN, DKR_IN_BIT(DKR_IN_BLACK) | DKR_IN_BIT(DKR_IN_RDOWN) },
    [DKR_PAD_CLEFT] = { "C-Left", HOST_BTN_CLEFT, DKR_IN_BIT(DKR_IN_RLEFT) },
    [DKR_PAD_CRIGHT] = { "C-Right", HOST_BTN_CRIGHT, DKR_IN_BIT(DKR_IN_WHITE) | DKR_IN_BIT(DKR_IN_RRIGHT) },
    [DKR_PAD_UP] = { "D-Pad Up", HOST_BTN_UP, DKR_IN_BIT(DKR_IN_UP) },
    [DKR_PAD_DOWN] = { "D-Pad Down", HOST_BTN_DOWN, DKR_IN_BIT(DKR_IN_DOWN) },
    [DKR_PAD_LEFT] = { "D-Pad Left", HOST_BTN_LEFT, DKR_IN_BIT(DKR_IN_LEFT) },
    [DKR_PAD_RIGHT] = { "D-Pad Right", HOST_BTN_RIGHT, DKR_IN_BIT(DKR_IN_RIGHT) },
};

static const char *const sInputNames[DKR_IN_COUNT] = {
    [DKR_IN_A] = "A",
    [DKR_IN_B] = "B",
    [DKR_IN_X] = "X",
    [DKR_IN_Y] = "Y",
    [DKR_IN_BLACK] = "Black",
    [DKR_IN_WHITE] = "White",
    [DKR_IN_LT] = "Left trigger",
    [DKR_IN_RT] = "Right trigger",
    [DKR_IN_START] = "Start",
    [DKR_IN_BACK] = "Back",
    [DKR_IN_LCLICK] = "Left click",
    [DKR_IN_UP] = "D-Up",
    [DKR_IN_DOWN] = "D-Down",
    [DKR_IN_LEFT] = "D-Left",
    [DKR_IN_RIGHT] = "D-Right",
    [DKR_IN_RUP] = "Stick Up",
    [DKR_IN_RDOWN] = "Stick Down",
    [DKR_IN_RLEFT] = "Stick Left",
    [DKR_IN_RRIGHT] = "Stick Right",
    [DKR_IN_RCLICK] = "Right click",
};

static SettingsImage sImage;
static int sReady;

static void controls_default(void) {
    int i;

    for (i = 0; i < DKR_PAD_COUNT; i++) {
        sImage.controls[i] = sPads[i].inputs;
    }
}

static void settings_init(void) {
    int i;

    if (sReady) {
        return;
    }
    sReady = 1;
    // a file from before the controls is the shorter one: its settings, and
    // the controls as they were
    if (!host_settings_load((uint8_t *) &sImage, sizeof(sImage))) {
        memset(&sImage, 0, sizeof(sImage));
        if (host_settings_load((uint8_t *) &sImage, SETTINGS_V2_SIZE) && sImage.version >= 3) {
            memset(&sImage, 0, sizeof(sImage));
        }
        controls_default();
    }
    if (memcmp(sImage.magic, SETTINGS_MAGIC, sizeof(sImage.magic)) != 0) {
        memset(&sImage, 0, sizeof(sImage));
        controls_default();
    } else if (sImage.version == 1) {
        // version 1 had no widescreen: its settings move one on, widescreen off
        memmove(&sImage.values[1], &sImage.values[0], SETTINGS_SLOTS - 1);
        sImage.values[0] = DKR_WIDESCREEN_OFF;
    } else if (sImage.version != 2 && sImage.version != SETTINGS_VERSION) {
        memset(&sImage, 0, sizeof(sImage));
        controls_default();
    }
    memcpy(sImage.magic, SETTINGS_MAGIC, sizeof(sImage.magic));
    sImage.version = SETTINGS_VERSION;
    sImage.count = DKR_SET_COUNT;
    // anything out of range (a damaged file) is the setting's first value
    for (i = 0; i < DKR_SET_COUNT; i++) {
        if (sImage.values[i] >= sInfo[i].count) {
            sImage.values[i] = 0;
        }
    }
    for (i = 0; i < DKR_PAD_COUNT; i++) {
        sImage.controls[i] &= DKR_IN_ALL;
    }
}

int dkr_setting(int id) {
    settings_init();
    return (id >= 0 && id < DKR_SET_COUNT) ? sImage.values[id] : 0;
}

void dkr_setting_set(int id, int value) {
    settings_init();
    if (id < 0 || id >= DKR_SET_COUNT || value < 0 || value >= sInfo[id].count || sImage.values[id] == value) {
        return;
    }
    sImage.values[id] = (unsigned char) value;
    host_settings_changed();
}

int dkr_widescreen(void) {
    return dkr_setting(DKR_SET_WIDESCREEN) == DKR_WIDESCREEN_ON && gfx_display_wide();
}

static int sFramed;

void dkr_widescreen_framed(int framed) {
    sFramed = framed;
}

int dkr_widescreen_world(void) {
    return dkr_widescreen() && !sFramed;
}

int dkr_setting_values(int id) {
    return (id >= 0 && id < DKR_SET_COUNT) ? sInfo[id].count : 1;
}

const char *dkr_setting_name(int id) {
    return (id >= 0 && id < DKR_SET_COUNT) ? sInfo[id].name : "";
}

const char *dkr_setting_value_name(int id, int value) {
    if (id < 0 || id >= DKR_SET_COUNT || value < 0 || value >= sInfo[id].count) {
        return "";
    }
    return sInfo[id].values[value];
}

unsigned int dkr_control(int pad) {
    settings_init();
    return (pad >= 0 && pad < DKR_PAD_COUNT) ? sImage.controls[pad] : 0;
}

void dkr_control_set(int pad, unsigned int inputs) {
    settings_init();
    inputs &= DKR_IN_ALL;
    if (pad < 0 || pad >= DKR_PAD_COUNT || sImage.controls[pad] == inputs) {
        return;
    }
    sImage.controls[pad] = inputs;
    host_settings_changed();
}

void dkr_controls_reset(void) {
    int i;

    for (i = 0; i < DKR_PAD_COUNT; i++) {
        dkr_control_set(i, sPads[i].inputs);
    }
}

unsigned int dkr_controls_buttons(unsigned int inputs) {
    unsigned int b = 0;
    int i;

    settings_init();
    for (i = 0; i < DKR_PAD_COUNT; i++) {
        if (sImage.controls[i] & inputs) {
            b |= sPads[i].bit;
        }
    }
    return b;
}

const char *dkr_control_name(int pad) {
    return (pad >= 0 && pad < DKR_PAD_COUNT) ? sPads[pad].name : "";
}

const char *dkr_input_name(int in) {
    return (in >= 0 && in < DKR_IN_COUNT) ? sInputNames[in] : "";
}

const unsigned char *dkr_settings_image(void) {
    settings_init();
    return (const unsigned char *) &sImage;
}

int dkr_settings_size(void) {
    return (int) sizeof(sImage);
}
