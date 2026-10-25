// dkr_menu.c -- the port's settings menu, drawn over the game: the right
// stick's click (HOST_BTN_MENU) opens and closes it.
//
// While it is open the game does not run at all. pc_gfx_task_submit (dkr_dl.c)
// draws the frame the game last built again, every video frame, with the menu
// over it, and only goes back to the game once the menu has closed: a race is
// frozen exactly where it was, and nothing in the game can tell. The menu is
// drawn in HD, in its own font, at the display's resolution (dkr_overlay.c).

#include <stddef.h>
#include <stdint.h>

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
#define ROW_X0 186.0f
#define ROW_X1 774.0f
#define LABEL_X 214.0f
#define VALUE_X 744.0f
#define ARROW_GAP 12.0f
#define ARROW_SIZE 9.0f
#define FOOTER_Y 634.0f
#define BADGE_RADIUS 15.0f

// colours, 0xAARRGGBB: the game's own blues and golds
#define C_DIM 0xA0000010u
#define C_SHADOW 0x70000000u
#define C_GOLD_TOP 0xFFFFDA55u
#define C_GOLD_BOTTOM 0xFFE09A18u
#define C_PANEL_TOP 0xF5203E96u
#define C_PANEL_BOTTOM 0xF50A1640u
#define C_HEADER_TOP 0xFF4A7CF0u
#define C_HEADER_BOTTOM 0xFF22429Cu
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

enum { ITEM_CLOSE = DKR_SET_COUNT, ITEM_COUNT };

void pc_gfx_overlay_begin(void);

static int sOpen;         // drawn and taking the pads
static int sClosing;      // closed, but the buttons that closed it are not yet let go
static int sCursor;
static uint16_t sHeld;    // last frame's buttons, every pad together, the stick as the D-pad
static uint16_t sPressed; // every button pressed since the menu opened

static uint16_t pads_now(void) {
    HostPad pads[HOST_PADS];
    uint16_t b = 0;
    int i;

    host_pads_read(pads);
    for (i = 0; i < HOST_PADS; i++) {
        if (!pads[i].connected) {
            continue;
        }
        b |= pads[i].buttons;
        if (pads[i].stickY > STICK_PUSH) {
            b |= HOST_BTN_UP;
        } else if (pads[i].stickY < -STICK_PUSH) {
            b |= HOST_BTN_DOWN;
        }
        if (pads[i].stickX > STICK_PUSH) {
            b |= HOST_BTN_RIGHT;
        } else if (pads[i].stickX < -STICK_PUSH) {
            b |= HOST_BTN_LEFT;
        }
    }
    return b;
}

/**
 * Once a video frame, after it is shown: the pads, for the menu. Returns
 * nonzero while the menu holds the game.
 */
int dkr_menu_update(void) {
    uint16_t now = pads_now();
    uint16_t pressed = now & ~sHeld;
    int step, count, i;

    sHeld = now;
    sPressed |= pressed;
    if (!sOpen) {
        if (pressed & HOST_BTN_MENU) {
            sOpen = 1;
            sClosing = 0;
            sCursor = 0;
            sPressed = HOST_BTN_MENU;
            // the game is frozen with its motors as they were: stop them
            for (i = 0; i < HOST_PADS; i++) {
                host_rumble(i, 0);
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
    if (pressed & (HOST_BTN_MENU | HOST_BTN_B | HOST_BTN_START)) {
        sClosing = 1;
        return 1;
    }
    if (pressed & HOST_BTN_UP) {
        sCursor = (sCursor + ITEM_COUNT - 1) % ITEM_COUNT;
    }
    if (pressed & HOST_BTN_DOWN) {
        sCursor = (sCursor + 1) % ITEM_COUNT;
    }
    if (sCursor == ITEM_CLOSE) {
        if (pressed & HOST_BTN_A) {
            sClosing = 1;
        }
        return 1;
    }
    step = (pressed & (HOST_BTN_A | HOST_BTN_RIGHT)) ? 1 : (pressed & HOST_BTN_LEFT) ? -1 : 0;
    if (step != 0) {
        count = dkr_setting_values(sCursor);
        dkr_setting_set(sCursor, (dkr_setting(sCursor) + step + count) % count);
    }
    return 1;
}

// text with a soft drop shadow under it
static void text(int font, float x, float y, const char *s, unsigned colour, int align) {
    ovl_text(font, x + 2.0f, y + 3.0f, s, C_TEXT_SHADOW, align);
    ovl_text(font, x, y, s, colour, align);
}

static void row_back(float y, int chosen) {
    if (chosen) {
        ovl_round_rect(ROW_X0 - 3.0f, y - ROW_HALF - 3.0f, ROW_X1 + 3.0f, y + ROW_HALF + 3.0f, 17.0f, C_GOLD_TOP,
                       C_GOLD_BOTTOM);
        ovl_round_rect(ROW_X0, y - ROW_HALF, ROW_X1, y + ROW_HALF, 14.0f, C_CHOSEN_TOP, C_CHOSEN_BOTTOM);
    } else {
        ovl_round_rect(ROW_X0, y - ROW_HALF, ROW_X1, y + ROW_HALF, 14.0f, C_ROW, C_ROW);
    }
}

// arrows either side of a value that can change
static void value_arrows(float y, float width) {
    float l = VALUE_X - width - ARROW_GAP;
    float r = VALUE_X + ARROW_GAP;

    ovl_tri(l, y - ARROW_SIZE, l, y + ARROW_SIZE, l - ARROW_SIZE * 1.2f, y, C_GOLD_TOP);
    ovl_tri(r, y - ARROW_SIZE, r + ARROW_SIZE * 1.2f, y, r, y + ARROW_SIZE, C_GOLD_TOP);
}

// a pad button as the pad has it, and what it does: returns the width used
static float hint(float x, const char *badge, unsigned badgeColour, const char *what) {
    float w;

    if (badge != NULL) {
        ovl_circle(x + BADGE_RADIUS, FOOTER_Y, BADGE_RADIUS, badgeColour);
        ovl_text(OVL_FONT_SMALL, x + BADGE_RADIUS, FOOTER_Y, badge, 0xFFFFFFFFu, OVL_ALIGN_CENTRE);
        w = BADGE_RADIUS * 2.0f + 8.0f;
    } else {
        // left and right: two small arrows
        ovl_tri(x + 10.0f, FOOTER_Y - 8.0f, x + 10.0f, FOOTER_Y + 8.0f, x, FOOTER_Y, C_HINT);
        ovl_tri(x + 16.0f, FOOTER_Y - 8.0f, x + 26.0f, FOOTER_Y, x + 16.0f, FOOTER_Y + 8.0f, C_HINT);
        w = 34.0f;
    }
    ovl_text(OVL_FONT_SMALL, x + w, FOOTER_Y, what, C_HINT, OVL_ALIGN_LEFT);
    return w + ovl_text_width(OVL_FONT_SMALL, what);
}

static void footer(void) {
    static const float kGap = 34.0f;
    float total, x;

    // measured first, to centre the three
    total = (34.0f + ovl_text_width(OVL_FONT_SMALL, "Change")) + kGap +
            (BADGE_RADIUS * 2.0f + 8.0f + ovl_text_width(OVL_FONT_SMALL, "Select")) + kGap +
            (BADGE_RADIUS * 2.0f + 8.0f + ovl_text_width(OVL_FONT_SMALL, "Back"));
    x = (PANEL_X0 + PANEL_X1) * 0.5f - total * 0.5f;
    x += hint(x, NULL, 0, "Change") + kGap;
    x += hint(x, "A", C_BADGE_A, "Select") + kGap;
    hint(x, "B", C_BADGE_B, "Back");
}

/**
 * The menu over the frame, while it is open.
 */
void dkr_menu_draw(void) {
    const char *value;
    float y;
    int i, chosen;

    if (!sOpen || sClosing) {
        return;
    }
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
    ovl_rect(PANEL_X0 + BORDER, HEADER_Y1 - RADIUS, PANEL_X1 - BORDER, HEADER_Y1, 0xFF2D52B4u, C_HEADER_BOTTOM);
    ovl_rect(PANEL_X0 + BORDER, HEADER_Y1, PANEL_X1 - BORDER, HEADER_Y1 + 3.0f, C_GOLD_TOP, C_GOLD_BOTTOM);
    text(OVL_FONT_TITLE, (PANEL_X0 + PANEL_X1) * 0.5f, TITLE_Y, "Diddy Kong Racing X", C_TITLE, OVL_ALIGN_CENTRE);

    for (i = 0, y = ROWS_Y; i < DKR_SET_COUNT; i++, y += ROW_STEP) {
        chosen = i == sCursor;
        value = dkr_setting_value_name(i, dkr_setting(i));
        row_back(y, chosen);
        text(OVL_FONT_BODY, LABEL_X, y, dkr_setting_name(i), chosen ? C_CHOSEN : C_LABEL, OVL_ALIGN_LEFT);
        text(OVL_FONT_BODY, VALUE_X, y, value, chosen ? C_CHOSEN_VALUE : C_VALUE, OVL_ALIGN_RIGHT);
        if (chosen) {
            value_arrows(y, ovl_text_width(OVL_FONT_BODY, value));
        }
    }
    y += 10.0f;
    row_back(y, sCursor == ITEM_CLOSE);
    text(OVL_FONT_BODY, (PANEL_X0 + PANEL_X1) * 0.5f, y, "Back to the game",
         sCursor == ITEM_CLOSE ? C_CHOSEN_VALUE : C_LABEL, OVL_ALIGN_CENTRE);
    footer();
}
