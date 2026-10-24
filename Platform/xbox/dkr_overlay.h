// dkr_overlay.h -- HD drawing for the port's settings menu (dkr_menu.c): text in
// a real font and shapes, sharp at the display's own resolution, over the
// game's frame. Plain C types only, as dkr_host.h.
//
// Everything is laid out in a 960x720 space: the game's 4:3 picture as it is
// at 720p (three display pixels to each of the N64's), whatever the display.
#ifndef DKR_OVERLAY_H
#define DKR_OVERLAY_H

#define OVL_W 960
#define OVL_H 720

enum { OVL_FONT_BODY, OVL_FONT_TITLE, OVL_FONT_SMALL, OVL_FONT_COUNT };
enum { OVL_ALIGN_LEFT, OVL_ALIGN_CENTRE, OVL_ALIGN_RIGHT };

// colours are 0xAARRGGBB
void ovl_rect(float x0, float y0, float x1, float y1, unsigned top, unsigned bottom);
void ovl_round_rect(float x0, float y0, float x1, float y1, float radius, unsigned top, unsigned bottom);
void ovl_circle(float cx, float cy, float radius, unsigned colour);
void ovl_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned colour);
// y is the middle of the line
void ovl_text(int font, float x, float y, const char *text, unsigned colour, int align);
float ovl_text_width(int font, const char *text);

#endif
