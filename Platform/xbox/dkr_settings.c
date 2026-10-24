// dkr_settings.c -- the port's own settings (dkr_settings.h): their names for
// the menu, and their stored form, which the platform keeps on the hard disk
// (host_settings_load, host_settings_changed in dkr_host.h).

#include <string.h>

#include "dkr_host.h"
#include "dkr_settings.h"

#define SETTINGS_MAGIC "DKRS"
#define SETTINGS_VERSION 1
#define SETTINGS_SLOTS 32 // room for settings to come, in the same file

typedef struct {
    char magic[4];
    unsigned char version;
    unsigned char count;
    unsigned char pad[2];
    unsigned char values[SETTINGS_SLOTS];
} SettingsImage;

typedef struct {
    const char *name;
    int count;
    const char *values[4];
} SettingInfo;

static const SettingInfo sInfo[DKR_SET_COUNT] = {
    [DKR_SET_CAMERA_WALLS] = { "Camera at walls", 2, { "Keep out", "Original" } },
    [DKR_SET_PLANE_STICK] = { "Plane up/down", 2, { "Normal", "Inverted" } },
    [DKR_SET_RUMBLE] = { "Rumble", 4, { "High", "Medium", "Low", "Off" } },
    [DKR_SET_FRAME_RATE] = { "Frame rate", 2, { "60 FPS", "30 FPS" } },
    [DKR_SET_TEXTURES] = { "Textures", 2, { "Smooth", "Sharp" } },
};

static SettingsImage sImage;
static int sReady;

static void settings_init(void) {
    int i;

    if (sReady) {
        return;
    }
    sReady = 1;
    if (!host_settings_load((uint8_t *) &sImage, sizeof(sImage)) ||
        memcmp(sImage.magic, SETTINGS_MAGIC, sizeof(sImage.magic)) != 0 || sImage.version != SETTINGS_VERSION) {
        memset(&sImage, 0, sizeof(sImage));
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

const unsigned char *dkr_settings_image(void) {
    settings_init();
    return (const unsigned char *) &sImage;
}

int dkr_settings_size(void) {
    return (int) sizeof(sImage);
}
