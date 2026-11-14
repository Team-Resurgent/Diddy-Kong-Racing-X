// dkr_menu.c -- the port's own menu, drawn over the game. Back, pressed and
// let go on its own, opens and closes it. Its pages are the other ports' menus'
// (Ocarina Of Time-X's lusx_menu.cpp): Controls, Options, and restarting the
// game or leaving it for the dashboard.
//
// While it is open the game does not run at all. pc_gfx_task_submit (dkr_dl.c)
// draws the frame the game last built again, every video frame, with the menu
// over it, and only goes back to the game once the menu has closed: a race is
// frozen exactly where it was, and nothing in the game can tell. The menu is
// drawn in HD, in its own font, at the display's resolution (dkr_overlay.c).
//
// The Controls page puts the N64's buttons on the pad's (dkr_settings.c keeps
// them). The menu itself goes by the pad's own buttons, before that mapping,
// so no mapping can lock anyone out of it: A chooses, B goes back, Back
// closes it, and cancels a button being asked for.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "dkr_host.h"
#include "dkr_overlay.h"
#include "dkr_settings.h"

#define STICK_PUSH 40 // a stick pushed past this counts as the D-pad

// the layout, in dkr_overlay.h's 960x720
#define PANEL_X0 150.0f
#define PANEL_X1 810.0f
#define PANEL_Y0 60.0f
#define PANEL_Y1 676.0f
#define BORDER 4.0f
#define RADIUS 30.0f
#define HEADER_Y1 154.0f
#define TITLE_Y 107.0f
#define ROWS_Y 208.0f
#define ROW_STEP 58.0f
#define ROW_HALF 24.0f
#define LIST_Y 192.0f
#define LIST_STEP 46.0f
#define LIST_HALF 20.0f
#define LIST_ROWS 9 // of a page that scrolls, on screen at once
#define ROW_X0 186.0f
#define ROW_X1 774.0f
#define LABEL_X 214.0f
#define VALUE_X 744.0f
#define ARROW_GAP 12.0f
#define ARROW_SIZE 9.0f
#define FOOTER_Y 634.0f
#define BADGE_RADIUS 15.0f
#define CENTRE_X ((PANEL_X0 + PANEL_X1) * 0.5f)

// colours, 0xAARRGGBB: the game's own blues and golds
#define C_DIM 0xA0000010u
#define C_SHADOW 0x70000000u
#define C_GOLD_TOP 0xFFFFDA55u
#define C_GOLD_BOTTOM 0xFFE09A18u
#define C_PANEL_TOP 0xF5203E96u
#define C_PANEL_BOTTOM 0xF50A1640u
#define C_HEADER_TOP 0xFF4A7CF0u
#define C_HEADER_BOTTOM 0xFF22429Cu
#define C_HEADER_MIDDLE 0xFF2D52B4u
#define C_ROW 0x16FFFFFFu
#define C_CHOSEN_TOP 0xF05C98FFu
#define C_CHOSEN_BOTTOM 0xF02A5ED8u
#define C_TEXT_SHADOW 0x90000000u
#define C_TITLE 0xFFFFFFFFu
#define C_LABEL 0xFFE4ECFFu
#define C_VALUE 0xFF9CCCFFu
#define C_CHOSEN 0xFFFFFFFFu
#define C_CHOSEN_VALUE 0xFFFFE060u
#define C_HINT 0xFFB4C2EEu
#define C_BADGE_A 0xFF3DB24Au
#define C_BADGE_B 0xFFD8443Cu
#define C_BADGE_X 0xFF3C78D8u

#define REPEAT_AFTER 24 // frames a direction is held before it repeats
#define REPEAT_EVERY 6

enum { PAGE_MAIN, PAGE_CONTROLS, PAGE_CAPTURE, PAGE_OPTIONS, PAGE_CONFIRM };
enum { ROW_RESUME, ROW_CONTROLS, ROW_OPTIONS, ROW_RESTART, ROW_EXIT, ROW_COUNT };
// the Controls page's rows after the N64's buttons
enum { LIST_RUMBLE = DKR_PAD_COUNT, LIST_RESET, LIST_BACK, LIST_COUNT };

// the Options page: the settings but Rumble, which is the Controls page's
static const int kOptions[] = {
    DKR_SET_WIDESCREEN, DKR_SET_CAMERA_WALLS, DKR_SET_PLANE_STICK, DKR_SET_FRAME_RATE, DKR_SET_TEXTURES,
};
#define OPTION_COUNT ((int) (sizeof(kOptions) / sizeof(kOptions[0])))

// The menu's view of the pads: their own buttons (DKR_IN_BIT's), and the left
// stick pushed.
#define NAV_UP (1u << 24)
#define NAV_DOWN (1u << 25)
#define NAV_LEFT (1u << 26)
#define NAV_RIGHT (1u << 27)
#define M_A DKR_IN_BIT(DKR_IN_A)
#define M_B DKR_IN_BIT(DKR_IN_B)
#define M_X DKR_IN_BIT(DKR_IN_X)
#define M_BACK DKR_IN_BIT(DKR_IN_BACK)
#define M_UP (DKR_IN_BIT(DKR_IN_UP) | NAV_UP)
#define M_DOWN (DKR_IN_BIT(DKR_IN_DOWN) | NAV_DOWN)
#define M_LEFT (DKR_IN_BIT(DKR_IN_LEFT) | NAV_LEFT)
#define M_RIGHT (DKR_IN_BIT(DKR_IN_RIGHT) | NAV_RIGHT)
// held with Back, the dashboard's buttons (xbox_input.c): Back is not alone
#define M_DASHBOARD (DKR_IN_BIT(DKR_IN_START) | DKR_IN_BIT(DKR_IN_LT) | DKR_IN_BIT(DKR_IN_RT))

void pc_gfx_overlay_begin(void);

static int sOpen;         // drawn and taking the pads
static int sClosing;      // closed, but the buttons that closed it are not yet let go
static int sPage;
static int sCursor;
static int sTop;          // a scrolling page's first row on screen
static int sMainCursor;   // what the main page goes back to
static int sAdd;          // the button asked for goes on beside the ones there
static int sConfirmRow;   // the main page's row being confirmed
static int sHeldFor;      // frames up or down has been held
static int sBackDown, sBackAlone;
static unsigned int sHeld;    // last frame's buttons, every pad together (pads_now)
static unsigned int sPressed; // every button pressed since the menu opened

static unsigned int pads_now(void) {
    HostPad pads[HOST_PADS];
    unsigned int b = 0;
    int i;

    host_pads_read(pads);
    for (i = 0; i < HOST_PADS; i++) {
        if (!pads[i].connected) {
            continue;
        }
        b |= pads[i].inputs;
        // (the stick goes by the N64's D-pad when it is idle: the stick only,
        // not whatever the D-pad has been put on)
        if (!(pads[i].buttons & (HOST_BTN_UP | HOST_BTN_DOWN))) {
            if (pads[i].stickY > STICK_PUSH) {
                b |= NAV_UP;
            } else if (pads[i].stickY < -STICK_PUSH) {
                b |= NAV_DOWN;
            }
        }
        if (!(pads[i].buttons & (HOST_BTN_LEFT | HOST_BTN_RIGHT))) {
            if (pads[i].stickX > STICK_PUSH) {
                b |= NAV_RIGHT;
            } else if (pads[i].stickX < -STICK_PUSH) {
                b |= NAV_LEFT;
            }
        }
    }
    return b;
}

static void go_main(void) {
    sPage = PAGE_MAIN;
    sCursor = sMainCursor;
}

// a scrolling page's first row on screen, with the chosen one on it
static void scroll_to_cursor(void) {
    sTop = sCursor < sTop ? sCursor : sCursor >= sTop + LIST_ROWS ? sCursor - LIST_ROWS + 1 : sTop;
}

static void change_setting(int id, int step) {
    int count = dkr_setting_values(id);

    dkr_setting_set(id, (dkr_setting(id) + step + count) % count);
}

static void update_main(unsigned int pressed, int step) {
    sCursor = (sCursor + step + ROW_COUNT) % ROW_COUNT;
    if (pressed & M_B) {
        sClosing = 1;
    }
    if (!(pressed & M_A)) {
        return;
    }
    sMainCursor = sCursor;
    switch (sCursor) {
        case ROW_RESUME:
            sClosing = 1;
            break;
        case ROW_CONTROLS:
            sPage = PAGE_CONTROLS;
            sCursor = sTop = 0;
            break;
        case ROW_OPTIONS:
            sPage = PAGE_OPTIONS;
            sCursor = sTop = 0;
            break;
        default:
            sPage = PAGE_CONFIRM;
            sConfirmRow = sCursor;
            sCursor = 0; // No: the safe answer first
            break;
    }
}

static void update_controls(unsigned int pressed, int step) {
    sCursor = (sCursor + step + LIST_COUNT) % LIST_COUNT;
    scroll_to_cursor();
    if (pressed & M_B) {
        go_main();
    } else if (sCursor == LIST_RUMBLE) {
        if (pressed & M_RIGHT) {
            change_setting(DKR_SET_RUMBLE, 1);
        } else if (pressed & M_LEFT) {
            change_setting(DKR_SET_RUMBLE, -1);
        }
    } else if (sCursor >= DKR_PAD_COUNT) {
        if (pressed & M_A) {
            if (sCursor == LIST_RESET) {
                dkr_controls_reset();
            } else {
                go_main();
            }
        }
    } else if (pressed & (M_A | M_X)) {
        // A puts the button on the next one pressed alone, X on it as well
        sAdd = (pressed & M_A) == 0;
        sPage = PAGE_CAPTURE;
    }
}

static void update_capture(unsigned int pressed) {
    unsigned int in = pressed & DKR_IN_ALL;

    if (in == 0) {
        return;
    }
    in &= ~(in - 1); // one, if two came at once
    dkr_control_set(sCursor, sAdd ? (dkr_control(sCursor) | in) : in);
    sPage = PAGE_CONTROLS;
}

static void update_options(unsigned int pressed, int step) {
    sCursor = (sCursor + step + OPTION_COUNT + 1) % (OPTION_COUNT + 1);
    if (pressed & M_B) {
        go_main();
    } else if (sCursor == OPTION_COUNT) {
        if (pressed & M_A) {
            go_main();
        }
    } else if (pressed & M_RIGHT) {
        change_setting(kOptions[sCursor], 1);
    } else if (pressed & M_LEFT) {
        change_setting(kOptions[sCursor], -1);
    }
}

static void update_confirm(unsigned int pressed, int step) {
    sCursor = (sCursor + step + 2) % 2;
    if (pressed & M_B) {
        go_main();
    } else if (pressed & M_A) {
        if (sCursor == 0) {
            go_main();
        } else {
            host_leave(sConfirmRow == ROW_RESTART);
        }
    }
}

/**
 * Once a video frame, after it is shown: the pads, for the menu. Returns
 * nonzero while the menu holds the game.
 */
int dkr_menu_update(void) {
    unsigned int now = pads_now();
    unsigned int pressed = now & ~sHeld;
    int step, i;

    sHeld = now;
    sPressed |= pressed;
    if (!sOpen) {
        // Back, pressed and let go on its own: with the dashboard's buttons it
        // is theirs (xbox_input.c)
        if (now & M_BACK) {
            if (!sBackDown) {
                sBackDown = 1;
                sBackAlone = 1;
            }
            if (now & M_DASHBOARD) {
                sBackAlone = 0;
            }
        } else if (sBackDown) {
            sBackDown = 0;
            if (sBackAlone) {
                sOpen = 1;
                sClosing = 0;
                sPage = PAGE_MAIN;
                sCursor = sMainCursor = 0;
                sTop = 0;
                sPressed = 0;
                sHeldFor = 0;
                // the game is frozen with its motors as they were: stop them
                for (i = 0; i < HOST_PADS; i++) {
                    host_rumble(i, 0);
                }
            }
        }
        return sOpen;
    }
    if (sClosing) {
        // Back to the game only once the buttons pressed in the menu are up:
        // it would take one still held for a press of its own. A button held
        // from before the menu opened (the accelerator) does not hold it up.
        if ((now & sPressed) == 0) {
            sOpen = 0;
            sClosing = 0;
        }
        return sOpen;
    }
    // Back is the way out from anywhere, and all a button asked for can't be
    if (pressed & M_BACK) {
        if (sPage == PAGE_CAPTURE) {
            sPage = PAGE_CONTROLS;
        } else {
            sClosing = 1;
        }
        return 1;
    }
    step = (pressed & M_DOWN) ? 1 : (pressed & M_UP) ? -1 : 0;
    sHeldFor = (now & (M_UP | M_DOWN)) ? sHeldFor + 1 : 0;
    if (step == 0 && sHeldFor >= REPEAT_AFTER && (sHeldFor - REPEAT_AFTER) % REPEAT_EVERY == 0) {
        step = (now & M_DOWN) ? 1 : -1;
    }
    switch (sPage) {
        case PAGE_CONTROLS:
            update_controls(pressed, step);
            break;
        case PAGE_CAPTURE:
            update_capture(pressed);
            break;
        case PAGE_OPTIONS:
            update_options(pressed, step);
            break;
        case PAGE_CONFIRM:
            update_confirm(pressed, step);
            break;
        default:
            update_main(pressed, step);
            break;
    }
    return 1;
}

// text with a soft drop shadow under it
static void text(int font, float x, float y, const char *s, unsigned colour, int align) {
    ovl_text(font, x + 2.0f, y + 3.0f, s, C_TEXT_SHADOW, align);
    ovl_text(font, x, y, s, colour, align);
}

static void row_back(float y, float half, int chosen) {
    if (chosen) {
        ovl_round_rect(ROW_X0 - 3.0f, y - half - 3.0f, ROW_X1 + 3.0f, y + half + 3.0f, 17.0f, C_GOLD_TOP,
                       C_GOLD_BOTTOM);
        ovl_round_rect(ROW_X0, y - half, ROW_X1, y + half, 14.0f, C_CHOSEN_TOP, C_CHOSEN_BOTTOM);
    } else {
        ovl_round_rect(ROW_X0, y - half, ROW_X1, y + half, 14.0f, C_ROW, C_ROW);
    }
}

// a row of one centred label
static void row(float y, float half, const char *label, int chosen) {
    row_back(y, half, chosen);
    text(OVL_FONT_BODY, CENTRE_X, y, label, chosen ? C_CHOSEN_VALUE : C_LABEL, OVL_ALIGN_CENTRE);
}

// a row of a label and its value; `arrows`, chosen: left and right change it,
// and it says so
static void value_row(float y, float half, const char *label, const char *value, int chosen, int arrows) {
    float l, r;

    row_back(y, half, chosen);
    text(OVL_FONT_BODY, LABEL_X, y, label, chosen ? C_CHOSEN : C_LABEL, OVL_ALIGN_LEFT);
    text(OVL_FONT_SMALL, VALUE_X, y, value, chosen ? C_CHOSEN_VALUE : C_VALUE, OVL_ALIGN_RIGHT);
    if (chosen && arrows) {
        l = VALUE_X - ovl_text_width(OVL_FONT_SMALL, value) - ARROW_GAP;
        r = VALUE_X + ARROW_GAP;
        ovl_tri(l, y - ARROW_SIZE, l, y + ARROW_SIZE, l - ARROW_SIZE * 1.2f, y, C_GOLD_TOP);
        ovl_tri(r, y - ARROW_SIZE, r + ARROW_SIZE * 1.2f, y, r, y + ARROW_SIZE, C_GOLD_TOP);
    }
}

// a pad button as the pad has it, and what it does: returns the width used
static float hint(float x, const char *badge, unsigned badgeColour, const char *what) {
    float w = BADGE_RADIUS * 2.0f + 8.0f;

    ovl_circle(x + BADGE_RADIUS, FOOTER_Y, BADGE_RADIUS, badgeColour);
    ovl_text(OVL_FONT_SMALL, x + BADGE_RADIUS, FOOTER_Y, badge, 0xFFFFFFFFu, OVL_ALIGN_CENTRE);
    ovl_text(OVL_FONT_SMALL, x + w, FOOTER_Y, what, C_HINT, OVL_ALIGN_LEFT);
    return w + ovl_text_width(OVL_FONT_SMALL, what);
}

static float hint_width(const char *what) {
    return BADGE_RADIUS * 2.0f + 8.0f + ovl_text_width(OVL_FONT_SMALL, what);
}

// for a row left and right change: two arrows, and B
static void footer_change(void) {
    static const float kGap = 28.0f, kArrows = 34.0f;
    float x = CENTRE_X - (kArrows + ovl_text_width(OVL_FONT_SMALL, "Change") + kGap + hint_width("Back")) * 0.5f;

    ovl_tri(x + 10.0f, FOOTER_Y - 8.0f, x + 10.0f, FOOTER_Y + 8.0f, x, FOOTER_Y, C_HINT);
    ovl_tri(x + 16.0f, FOOTER_Y - 8.0f, x + 26.0f, FOOTER_Y, x + 16.0f, FOOTER_Y + 8.0f, C_HINT);
    ovl_text(OVL_FONT_SMALL, x + kArrows, FOOTER_Y, "Change", C_HINT, OVL_ALIGN_LEFT);
    x += kArrows + ovl_text_width(OVL_FONT_SMALL, "Change") + kGap;
    hint(x, "B", C_BADGE_B, "Back");
}

static void footer(const char *a, const char *x, const char *b) {
    static const float kGap = 34.0f;
    // measured first, to centre them
    float at = CENTRE_X - (hint_width(a) + kGap + (x ? hint_width(x) + kGap : 0.0f) + hint_width(b)) * 0.5f;

    at += hint(at, "A", C_BADGE_A, a) + kGap;
    if (x != NULL) {
        at += hint(at, "X", C_BADGE_X, x) + kGap;
    }
    hint(at, "B", C_BADGE_B, b);
}

static void panel(const char *title) {
    pc_gfx_overlay_begin();
    // the game dimmed behind; the panel's shadow, gold rim and body
    ovl_rect(0.0f, 0.0f, OVL_W, OVL_H, C_DIM, C_DIM);
    ovl_round_rect(PANEL_X0 + 8.0f, PANEL_Y0 + 12.0f, PANEL_X1 + 8.0f, PANEL_Y1 + 12.0f, RADIUS, C_SHADOW, C_SHADOW);
    ovl_round_rect(PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1, RADIUS, C_GOLD_TOP, C_GOLD_BOTTOM);
    ovl_round_rect(PANEL_X0 + BORDER, PANEL_Y0 + BORDER, PANEL_X1 - BORDER, PANEL_Y1 - BORDER, RADIUS - BORDER,
                   C_PANEL_TOP, C_PANEL_BOTTOM);
    // the title's band: rounded at the top, square where it meets the body
    ovl_round_rect(PANEL_X0 + BORDER, PANEL_Y0 + BORDER, PANEL_X1 - BORDER, HEADER_Y1, RADIUS - BORDER,
                   C_HEADER_TOP, C_HEADER_BOTTOM);
    ovl_rect(PANEL_X0 + BORDER, HEADER_Y1 - RADIUS, PANEL_X1 - BORDER, HEADER_Y1, C_HEADER_MIDDLE, C_HEADER_BOTTOM);
    ovl_rect(PANEL_X0 + BORDER, HEADER_Y1, PANEL_X1 - BORDER, HEADER_Y1 + 3.0f, C_GOLD_TOP, C_GOLD_BOTTOM);
    text(OVL_FONT_TITLE, CENTRE_X, TITLE_Y, title, C_TITLE, OVL_ALIGN_CENTRE);
}

// more of a page above or below
static void more_marks(int rows) {
    float y;

    if (sTop > 0) {
        y = 168.0f;
        ovl_tri(CENTRE_X - 12.0f, y, CENTRE_X, y - 10.0f, CENTRE_X + 12.0f, y, C_GOLD_TOP);
    }
    if (sTop + LIST_ROWS < rows) {
        y = LIST_Y + LIST_ROWS * LIST_STEP - 20.0f;
        ovl_tri(CENTRE_X - 12.0f, y, CENTRE_X + 12.0f, y, CENTRE_X, y + 10.0f, C_GOLD_TOP);
    }
}

static void draw_main(void) {
    static const char *const kRows[ROW_COUNT] = { "Back to the game", "Controls", "Options", "Restart game",
                                                  "Exit to dashboard" };
    int i;

    panel("Diddy Kong Racing-X");
    for (i = 0; i < ROW_COUNT; i++) {
        row(ROWS_Y + i * ROW_STEP, ROW_HALF, kRows[i], sCursor == i);
    }
    footer("Select", NULL, "Back");
}

// what a button is on, as much as fits in `width`, and how many more
static void control_text(int pad, char *out, int size, float width) {
    unsigned int inputs = dkr_control(pad);
    int len = 0, left, i, n;
    char more[16];

    out[0] = '\0';
    if (inputs == 0) {
        snprintf(out, size, "None");
        return;
    }
    for (left = 0, i = 0; i < DKR_IN_COUNT; i++) {
        left += (inputs >> i) & 1;
    }
    for (i = 0; i < DKR_IN_COUNT; i++) {
        if (!(inputs & DKR_IN_BIT(i))) {
            continue;
        }
        n = snprintf(out + len, size - len, "%s%s", len > 0 ? ", " : "", dkr_input_name(i));
        snprintf(more, sizeof(more), " +%d", left - 1);
        if (len > 0 && (len + n >= size || ovl_text_width(OVL_FONT_SMALL, out) +
                                                   (left > 1 ? ovl_text_width(OVL_FONT_SMALL, more) : 0.0f) > width)) {
            snprintf(out + len, size - len, " +%d", left);
            return;
        }
        len += n;
        left--;
    }
}

static void draw_controls(void) {
    char value[96];
    float y;
    int i, at, chosen;

    panel("Controls");
    for (i = 0; i < LIST_ROWS && sTop + i < LIST_COUNT; i++) {
        at = sTop + i;
        y = LIST_Y + i * LIST_STEP;
        chosen = at == sCursor;
        if (at == LIST_RUMBLE) {
            value_row(y, LIST_HALF, dkr_setting_name(DKR_SET_RUMBLE),
                      dkr_setting_value_name(DKR_SET_RUMBLE, dkr_setting(DKR_SET_RUMBLE)), chosen, 1);
        } else if (at == LIST_RESET) {
            row(y, LIST_HALF, "Reset to defaults", chosen);
        } else if (at == LIST_BACK) {
            row(y, LIST_HALF, "Back", chosen);
        } else if (chosen && sPage == PAGE_CAPTURE) {
            value_row(y, LIST_HALF, dkr_control_name(at), "Press a button", 1, 0);
        } else {
            control_text(at, value, sizeof(value), VALUE_X - LABEL_X - 190.0f);
            value_row(y, LIST_HALF, dkr_control_name(at), value, chosen, 0);
        }
    }
    more_marks(LIST_COUNT);
    if (sPage == PAGE_CAPTURE) {
        ovl_text(OVL_FONT_SMALL, CENTRE_X, FOOTER_Y,
                 sAdd ? "Press the button to add. Back cancels." : "Press the new button. Back cancels.", C_HINT,
                 OVL_ALIGN_CENTRE);
    } else if (sCursor < DKR_PAD_COUNT) {
        footer("Change", "Add another", "Back");
    } else if (sCursor == LIST_RUMBLE) {
        footer_change();
    } else {
        footer("Select", NULL, "Back");
    }
}

static void draw_options(void) {
    float y;
    int i, id;

    panel("Options");
    for (i = 0; i < OPTION_COUNT; i++) {
        id = kOptions[i];
        y = LIST_Y + i * LIST_STEP;
        value_row(y, LIST_HALF, dkr_setting_name(id), dkr_setting_value_name(id, dkr_setting(id)), sCursor == i, 1);
    }
    row(LIST_Y + OPTION_COUNT * LIST_STEP, LIST_HALF, "Back", sCursor == OPTION_COUNT);
    if (sCursor < OPTION_COUNT) {
        footer_change();
    } else {
        footer("Select", NULL, "Back");
    }
}

static void draw_confirm(void) {
    int restart = sConfirmRow == ROW_RESTART;

    panel(restart ? "Restart game?" : "Exit to dashboard?");
    text(OVL_FONT_BODY, CENTRE_X, ROWS_Y + 20.0f, "Anything not saved will be lost.", C_LABEL, OVL_ALIGN_CENTRE);
    row(ROWS_Y + ROW_STEP * 2.0f, ROW_HALF, "No, go back", sCursor == 0);
    row(ROWS_Y + ROW_STEP * 3.0f, ROW_HALF, restart ? "Yes, restart" : "Yes, exit", sCursor == 1);
    footer("Select", NULL, "Back");
}

/**
 * The menu over the frame, while it is open.
 */
void dkr_menu_draw(void) {
    if (!sOpen || sClosing) {
        return;
    }
    switch (sPage) {
        case PAGE_CONTROLS:
        case PAGE_CAPTURE:
            draw_controls();
            break;
        case PAGE_OPTIONS:
            draw_options();
            break;
        case PAGE_CONFIRM:
            draw_confirm();
            break;
        default:
            draw_main();
            break;
    }
}
