// dkr_settings.h -- the port's own settings: what the port's menu
// (dkr_menu.c, on Back) changes, kept on the hard disk beside
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

// The controls page (dkr_menu.c): the N64's buttons, and the Xbox inputs that
// press each, every pad alike. The left stick is the N64 stick, and Back the
// menu's: neither is offered.
enum {
    DKR_PAD_A,
    DKR_PAD_B,
    DKR_PAD_Z,
    DKR_PAD_R,
    DKR_PAD_L,
    DKR_PAD_START,
    DKR_PAD_CUP,
    DKR_PAD_CDOWN,
    DKR_PAD_CLEFT,
    DKR_PAD_CRIGHT,
    DKR_PAD_UP,
    DKR_PAD_DOWN,
    DKR_PAD_LEFT,
    DKR_PAD_RIGHT,
    DKR_PAD_COUNT
};
enum {
    DKR_IN_A,
    DKR_IN_B,
    DKR_IN_X,
    DKR_IN_Y,
    DKR_IN_BLACK,
    DKR_IN_WHITE,
    DKR_IN_LT,
    DKR_IN_RT,
    DKR_IN_START,
    DKR_IN_BACK,
    DKR_IN_LCLICK,
    DKR_IN_UP, // the D-pad
    DKR_IN_DOWN,
    DKR_IN_LEFT,
    DKR_IN_RIGHT,
    DKR_IN_RUP, // the right stick's directions
    DKR_IN_RDOWN,
    DKR_IN_RLEFT,
    DKR_IN_RRIGHT,
    DKR_IN_RCLICK,
    DKR_IN_COUNT
};
#define DKR_IN_BIT(in) (1u << (in))
// the inputs a button can go on: all but Back
#define DKR_IN_ALL ((DKR_IN_BIT(DKR_IN_COUNT) - 1u) & ~DKR_IN_BIT(DKR_IN_BACK))

// the inputs (DKR_IN_BIT's) that press an N64 button, and setting them
unsigned int dkr_control(int pad);
void dkr_control_set(int pad, unsigned int inputs);
void dkr_controls_reset(void);
// the Xbox inputs held, as the N64's buttons (dkr_host.h's HOST_BTN_*)
unsigned int dkr_controls_buttons(unsigned int inputs);
const char *dkr_control_name(int pad);
const char *dkr_input_name(int in);

// The stored form, for the platform to keep (dkr_host.h).
const unsigned char *dkr_settings_image(void);
int dkr_settings_size(void);

#endif
