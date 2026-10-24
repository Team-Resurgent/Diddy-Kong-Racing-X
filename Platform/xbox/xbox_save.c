/* xbox_save.c -- the N64's EEPROM and Controller Paks, and the port's own
 * settings, kept on the hard disk.
 *
 * Everything Diddy Kong Racing saves to the cartridge -- the three adventure
 * slots, the best times and the options -- is its 512-byte EEPROM; the
 * time-trial ghosts go to the Controller Pak in a controller. Here they are
 * eeprom.bin and cpak1.bin..cpak4.bin (one for each controller port, made the
 * first time the game writes to that pak), with settings.bin (the settings
 * menu's, dkr_settings.c), in the title's save game "Diddy Kong Racing": the
 * dashboard lists it under the game, with the save icon, and can copy or
 * delete it like any other (deleting it starts the game afresh).
 *
 * The game writes a save as a burst of small writes over a few frames; each
 * file is written once its burst is over, beside the old one and then put in
 * its place, so a failed write leaves the last good save. */
#include "xbox_xtl.h"

#include <string.h>

#include "dkr_host.h"
#include "dkr_settings.h"
#include "xbox_port.h"

#define SAVE_NAME L"Diddy Kong Racing"
#define QUIET_FRAMES 30 /* frames with no write before a file is stored */

/* what is stored: the EEPROM, the settings, then each port's Controller Pak */
#define SLOT_EEPROM 0
#define SLOT_SETTINGS 1
#define SLOT_CPAK 2
#define SLOTS (SLOT_CPAK + HOST_PADS)

static char sDir[MAX_PATH];
static int sDirState; /* 0 not tried, 1 there, -1 none */
/* frames since the last write; -1 when stored */
static int sQuiet[SLOTS] = { [0 ... SLOTS - 1] = -1 };

static const char *save_dir(void)
{
    DWORD rv;
    size_t n;

    if (sDirState)
        return sDirState > 0 ? sDir : NULL;
    rv = XCreateSaveGame("U:\\", SAVE_NAME, OPEN_ALWAYS, 0, sDir, sizeof(sDir));
    if (rv != ERROR_SUCCESS)
    {
        xb_log("save: no save game (%lu)", (unsigned long)rv);
        sDirState = -1;
        return NULL;
    }
    n = strlen(sDir);
    if (n && sDir[n - 1] != '\\' && n + 1 < sizeof(sDir))
    {
        sDir[n] = '\\';
        sDir[n + 1] = '\0';
    }
    sDirState = 1;
    return sDir;
}

static int path_of(char *out, size_t size, const char *name)
{
    const char *d = save_dir();

    if (!d || strlen(d) + strlen(name) + 1 > size)
        return 0;
    strcpy(out, d);
    strcat(out, name);
    return 1;
}

/* a slot's file name, and its name while it is being written */
static void slot_names(int slot, char *name, char *tmp)
{
    if (slot == SLOT_EEPROM)
    {
        strcpy(name, "eeprom.bin");
        strcpy(tmp, "eeprom.tmp");
    }
    else if (slot == SLOT_SETTINGS)
    {
        strcpy(name, "settings.bin");
        strcpy(tmp, "settings.tmp");
    }
    else
    {
        strcpy(name, "cpak1.bin");
        strcpy(tmp, "cpak1.tmp");
        name[4] = tmp[4] = (char)('1' + slot - SLOT_CPAK);
    }
}

void xb_save_init(void)
{
    save_dir();
}

static int load(int slot, uint8_t *buf, int size)
{
    char name[16], tmp[16], path[MAX_PATH];
    DWORD got = 0;
    HANDLE h;
    int ok;

    slot_names(slot, name, tmp);
    if (!path_of(path, sizeof(path), name))
        return 0;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = ReadFile(h, buf, (DWORD)size, &got, NULL) && got == (DWORD)size;
    CloseHandle(h);
    return ok;
}

int host_eeprom_load(uint8_t *buf, int size)
{
    return load(SLOT_EEPROM, buf, size);
}

void host_eeprom_changed(void)
{
    sQuiet[SLOT_EEPROM] = 0;
}

int host_cpak_load(int port, uint8_t *buf, int size)
{
    if (port < 0 || port >= HOST_PADS)
        return 0;
    return load(SLOT_CPAK + port, buf, size);
}

void host_cpak_changed(int port)
{
    if (port >= 0 && port < HOST_PADS)
        sQuiet[SLOT_CPAK + port] = 0;
}

int host_settings_load(uint8_t *buf, int size)
{
    return load(SLOT_SETTINGS, buf, size);
}

void host_settings_changed(void)
{
    sQuiet[SLOT_SETTINGS] = 0;
}

/* the save game's folder flushed too: the new folder and file entries, not
 * only the file's bytes, on the disk before anyone resets the console (an
 * in-game reset or the power button skips the cache's own flush) */
static void flush_dir(void)
{
    HANDLE h;

    if (sDirState <= 0)
        return;
    h = CreateFileA(sDir, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    FlushFileBuffers(h);
    CloseHandle(h);
}

static int store(int slot)
{
    char name[16], tmpName[16], path[MAX_PATH], tmp[MAX_PATH];
    const uint8_t *img;
    DWORD len, put = 0;
    HANDLE h;
    int ok;

    if (slot == SLOT_EEPROM)
    {
        img = dkr_eeprom_image();
        len = (DWORD)dkr_eeprom_size();
    }
    else if (slot == SLOT_SETTINGS)
    {
        img = dkr_settings_image();
        len = (DWORD)dkr_settings_size();
    }
    else
    {
        img = dkr_cpak_image(slot - SLOT_CPAK);
        len = (DWORD)dkr_cpak_size();
    }
    if (sDirState < 0)
        sDirState = 0; /* the dashboard could not make it at boot: try again */
    slot_names(slot, name, tmpName);
    if (!path_of(path, sizeof(path), name) || !path_of(tmp, sizeof(tmp), tmpName))
        return 0;
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        xb_log("save: cannot write %s (%lu)", tmp, (unsigned long)GetLastError());
        return 0;
    }
    ok = WriteFile(h, img, len, &put, NULL) && put == len;
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (ok)
    {
        DeleteFileA(path);
        ok = MoveFileA(tmp, path) != 0;
    }
    if (!ok)
    {
        xb_log("save: %s was not written (%lu)", path, (unsigned long)GetLastError());
        DeleteFileA(tmp);
        return 0;
    }
    flush_dir();
    xb_log("save: %s", path);
    return 1;
}

void xb_save_frame(void)
{
    int i;

    for (i = 0; i < SLOTS; i++)
    {
        if (sQuiet[i] < 0 || ++sQuiet[i] < QUIET_FRAMES)
            continue;
        sQuiet[i] = -1;
        store(i);
    }
}
