// dkr_settings.h -- the port's own settings: what the settings menu
// (dkr_menu.c, the right stick's click) changes, kept on the hard disk beside
// the save. The game's files read them too (under TARGET_XBOX), so plain C
// types only, as dkr_host.h.
//
// Every setting's first value is how the port behaves without it, so a new
// setting changes nothing until it is chosen.
#ifndef DKR_SETTINGS_H
#define DKR_SETTINGS_H

enum {
    DKR_SET_WIDESCREEN,   // the 3D scene across a 16:9 display (src/camera.c, dkr_dl.c)
    DKR_SET_CAMERA_WALLS, // the chase camera at a wall (src/tracks.c)
    DKR_SET_PLANE_STICK,  // up and down for the plane (src/racer.c)
    DKR_SET_RUMBLE,       // the pads' motors (xbox_input.c)
    DKR_SET_FRAME_RATE,   // the pacer (xbox_main.c)
    DKR_SET_TEXTURES,     // texture filtering (dkr_dl.c)
    DKR_SET_COUNT
};

enum { DKR_WIDESCREEN_OFF, DKR_WIDESCREEN_ON };
enum { DKR_CAMERA_KEEP_OUT, DKR_CAMERA_ORIGINAL };
enum { DKR_PLANE_NORMAL, DKR_PLANE_INVERTED };
enum { DKR_RUMBLE_HIGH, DKR_RUMBLE_MEDIUM, DKR_RUMBLE_LOW, DKR_RUMBLE_OFF };
enum { DKR_FRAME_RATE_60, DKR_FRAME_RATE_30 };
enum { DKR_TEXTURES_SMOOTH, DKR_TEXTURES_SHARP };

int dkr_setting(int id);
void dkr_setting_set(int id, int value);
// how many values a setting has, and the names the menu shows
int dkr_setting_values(int id);
const char *dkr_setting_name(int id);
const char *dkr_setting_value_name(int id, int value);

// Widescreen chosen, on a display that is 16:9: what the game's camera and
// the display-list interpreter go by.
int dkr_widescreen(void);

// Screens that show the 3D world through a frame of their own (the track
// select's window, the race results) keep that view 4:3, so it fits the frame
// they draw around it; they say so on every frame they draw it.
void dkr_widescreen_framed(int framed);
// Widescreen for the 3D world: the camera's projection and how the
// interpreter lays the scene out.
int dkr_widescreen_world(void);

// The stored form, for the platform to keep (dkr_host.h).
const unsigned char *dkr_settings_image(void);
int dkr_settings_size(void);

#endif
