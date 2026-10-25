// dkr_overlay.c -- HD drawing for the port's settings menu (dkr_overlay.h).
//
// The text is Fredoka (Platform/xbox/fonts, SIL Open Font License), embedded
// by tools/xbox/gen_font.py and rasterised with stb_truetype the first time
// the menu opens: each size at the number of display pixels it will cover,
// into an atlas texture, so a glyph is drawn texel for pixel, on whole pixels.
// Shapes and text go through the same triangle pipe as the game (dkr_gfx.h),
// in the N64 screen's coordinates, reached from the 960x720 layout through
// the display's own mapping (gfx_display_map).

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dkr_gfx.h"
#include "dkr_overlay.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

#include "gen/menu_font.h" // kMenuFontTtf

#define OVL_PER_N64 3.0f // the layout's pixels to each N64 pixel
#define ATLAS_W 512
#define ATLAS_H 512
#define FIRST_CHAR 32
#define CHAR_COUNT 95 // printable ASCII
#define MAX_TEXT 96   // characters drawn at once
#define ROUND_STEPS 6 // segments in a rounded corner
#define CIRCLE_STEPS 24

// the sizes, in layout pixels (the line's height)
static const float kFontSize[OVL_FONT_COUNT] = { 34.0f, 54.0f, 26.0f };
// drawn twice, a display pixel apart: Fredoka's regular weight, made bolder
// for reading across a room
static const int kFontBold[OVL_FONT_COUNT] = { 1, 1, 0 };

typedef struct {
    unsigned texture;
    stbtt_packedchar chars[CHAR_COUNT];
    float ascent, descent; // display pixels
    float scale;           // the size it was made at, in display pixels a layout pixel
} OvlFont;

static OvlFont sFonts[OVL_FONT_COUNT];
static stbtt_fontinfo sInfo;
static int sFontState; // 0 not tried, 1 made, -1 failed

// the display: N64 x at display x * sScaleX + sLeft (gfx_display_map)
static float sScaleX, sScaleY, sLeft, sTop;

static void display_map(void) {
    gfx_display_map(&sScaleX, &sScaleY, &sLeft, &sTop);
}

// a layout position to the display's pixels, and a display position to N64
static float disp_x(float x) {
    return sLeft + x * sScaleX / OVL_PER_N64;
}

static float disp_y(float y) {
    return sTop + y * sScaleY / OVL_PER_N64;
}

static float n64_x(float dx) {
    return (dx - sLeft) / sScaleX;
}

static float n64_y(float dy) {
    return (dy - sTop) / sScaleY;
}

static void colour_vert(GfxTriVert *v, float dx, float dy, unsigned colour) {
    v->x = n64_x(dx);
    v->y = n64_y(dy);
    v->z = 0.0f;
    v->w = 1.0f;
    v->u = v->v = 0.0f;
    v->fog = 0.0f;
    v->a = (unsigned char) (colour >> 24);
    v->r = (unsigned char) (colour >> 16);
    v->g = (unsigned char) (colour >> 8);
    v->b = (unsigned char) colour;
}

static unsigned mix(unsigned a, unsigned b, float t) {
    unsigned out = 0;
    int shift;

    for (shift = 0; shift < 32; shift += 8) {
        float ca = (float) ((a >> shift) & 0xFF), cb = (float) ((b >> shift) & 0xFF);

        out |= ((unsigned) (ca + (cb - ca) * t + 0.5f) & 0xFF) << shift;
    }
    return out;
}

// the 2D state, as the interpreter's draw_2d_quad leaves it, with a texture or none
static void draw_state(unsigned texture, int point) {
    static const unsigned char kNoFog[4] = { 0, 0, 0, 255 };

    gfx_set_wide(GFX_WIDE_OFF); // the menu is laid out on the 4:3 picture
    gfx_set_texenv_modulate();
    gfx_set_depth_test(0);
    gfx_set_depth_write(0);
    gfx_set_depth_offset(0);
    gfx_set_alpha_test(0.0f);
    gfx_set_fog(0, kNoFog);
    gfx_bind_texture(texture);
    gfx_set_texture_filter(point);
}

void ovl_rect(float x0, float y0, float x1, float y1, unsigned top, unsigned bottom) {
    GfxTriVert q[6];
    float l, t, r, b;

    display_map();
    // across the whole layout: across the whole display, a 16:9 one too
    if (x0 <= 0.0f && x1 >= OVL_W && gfx_display_wide()) {
        x0 = GFX_WIDE_X0 * OVL_PER_N64;
        x1 = GFX_WIDE_X1 * OVL_PER_N64;
    }
    // on whole display pixels, for clean edges
    l = floorf(disp_x(x0) + 0.5f);
    r = floorf(disp_x(x1) + 0.5f);
    t = floorf(disp_y(y0) + 0.5f);
    b = floorf(disp_y(y1) + 0.5f);
    colour_vert(&q[0], l, t, top);
    colour_vert(&q[1], r, t, top);
    colour_vert(&q[2], r, b, bottom);
    colour_vert(&q[3], l, t, top);
    colour_vert(&q[4], r, b, bottom);
    colour_vert(&q[5], l, b, bottom);
    draw_state(0, 0);
    gfx_draw_tris(q, 6);
}

void ovl_round_rect(float x0, float y0, float x1, float y1, float radius, unsigned top, unsigned bottom) {
    // the outline, corner by corner, then a fan from the middle
    static const float kCorner[4][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };
    float px[4 * (ROUND_STEPS + 1)], py[4 * (ROUND_STEPS + 1)];
    GfxTriVert tris[3 * 4 * (ROUND_STEPS + 1)];
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float h = (y1 - y0);
    int n = 0, c, s, i, k = 0;

    if (radius * 2.0f > x1 - x0) {
        radius = (x1 - x0) * 0.5f;
    }
    if (radius * 2.0f > h) {
        radius = h * 0.5f;
    }
    display_map();
    for (c = 0; c < 4; c++) {
        // the corner's centre, and its quarter turn: top right, bottom right,
        // bottom left, top left
        float ox = kCorner[c][0] > 0 ? x1 - radius : x0 + radius;
        float oy = kCorner[c][1] > 0 ? y1 - radius : y0 + radius;
        float a0 = -1.5707963f + c * 1.5707963f;

        for (s = 0; s <= ROUND_STEPS; s++) {
            float a = a0 + 1.5707963f * s / ROUND_STEPS;

            px[n] = ox + cosf(a) * radius;
            py[n] = oy + sinf(a) * radius;
            n++;
        }
    }
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;

        colour_vert(&tris[k++], disp_x(cx), disp_y(cy), mix(top, bottom, (cy - y0) / h));
        colour_vert(&tris[k++], disp_x(px[i]), disp_y(py[i]), mix(top, bottom, (py[i] - y0) / h));
        colour_vert(&tris[k++], disp_x(px[j]), disp_y(py[j]), mix(top, bottom, (py[j] - y0) / h));
    }
    draw_state(0, 0);
    gfx_draw_tris(tris, k);
}

void ovl_circle(float cx, float cy, float radius, unsigned colour) {
    GfxTriVert tris[3 * CIRCLE_STEPS];
    int i, k = 0;

    display_map();
    for (i = 0; i < CIRCLE_STEPS; i++) {
        float a0 = 6.2831853f * i / CIRCLE_STEPS, a1 = 6.2831853f * (i + 1) / CIRCLE_STEPS;

        colour_vert(&tris[k++], disp_x(cx), disp_y(cy), colour);
        colour_vert(&tris[k++], disp_x(cx + cosf(a0) * radius), disp_y(cy + sinf(a0) * radius), colour);
        colour_vert(&tris[k++], disp_x(cx + cosf(a1) * radius), disp_y(cy + sinf(a1) * radius), colour);
    }
    draw_state(0, 0);
    gfx_draw_tris(tris, k);
}

void ovl_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned colour) {
    GfxTriVert t[3];

    display_map();
    colour_vert(&t[0], disp_x(x0), disp_y(y0), colour);
    colour_vert(&t[1], disp_x(x1), disp_y(y1), colour);
    colour_vert(&t[2], disp_x(x2), disp_y(y2), colour);
    draw_state(0, 0);
    gfx_draw_tris(t, 3);
}

// Each size rasterised for this display, once: white glyphs whose alpha is
// their coverage, which the vertex colour then tints.
static int fonts_make(void) {
    static unsigned char alpha[ATLAS_W * ATLAS_H];
    unsigned char *rgba;
    float scale;
    int f, i, ascent, descent, gap;

    if (sFontState) {
        return sFontState > 0;
    }
    sFontState = -1;
    if (!stbtt_InitFont(&sInfo, kMenuFontTtf, stbtt_GetFontOffsetForIndex(kMenuFontTtf, 0))) {
        return 0;
    }
    rgba = (unsigned char *) malloc(ATLAS_W * ATLAS_H * 4);
    if (rgba == NULL) {
        return 0;
    }
    display_map();
    scale = sScaleY / OVL_PER_N64;
    stbtt_GetFontVMetrics(&sInfo, &ascent, &descent, &gap);
    for (f = 0; f < OVL_FONT_COUNT; f++) {
        OvlFont *font = &sFonts[f];
        float px = kFontSize[f] * scale;
        float em = stbtt_ScaleForPixelHeight(&sInfo, px);
        stbtt_pack_context pc;

        memset(alpha, 0, sizeof(alpha));
        if (!stbtt_PackBegin(&pc, alpha, ATLAS_W, ATLAS_H, 0, 1, NULL)) {
            free(rgba);
            return 0;
        }
        stbtt_PackFontRange(&pc, kMenuFontTtf, 0, px, FIRST_CHAR, CHAR_COUNT, font->chars);
        stbtt_PackEnd(&pc);
        for (i = 0; i < ATLAS_W * ATLAS_H; i++) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha[i];
        }
        font->texture = gfx_create_texture(rgba, ATLAS_W, ATLAS_H, 2, 2); // clamped both ways
        font->ascent = ascent * em;
        font->descent = descent * em;
        font->scale = scale;
    }
    free(rgba);
    sFontState = 1;
    return 1;
}

float ovl_text_width(int font, const char *text) {
    float x = 0.0f;
    int prev = 0;

    if (font < 0 || font >= OVL_FONT_COUNT || !fonts_make()) {
        return 0.0f;
    }
    for (; *text; text++) {
        int c = (unsigned char) *text;

        if (c < FIRST_CHAR || c >= FIRST_CHAR + CHAR_COUNT) {
            continue;
        }
        if (prev) {
            x += stbtt_GetCodepointKernAdvance(&sInfo, prev, c) *
                 stbtt_ScaleForPixelHeight(&sInfo, kFontSize[font] * sFonts[font].scale);
        }
        x += sFonts[font].chars[c - FIRST_CHAR].xadvance;
        prev = c;
    }
    // display pixels, back to the layout's
    return x / sFonts[font].scale;
}

void ovl_text(int font, float x, float y, const char *text, unsigned colour, int align) {
    // static: too big for a small stack, and the menu draws on one thread
    static GfxTriVert verts[6 * MAX_TEXT];
    OvlFont *f;
    float pen, base, width, squeeze, em;
    int n = 0, prev = 0;

    if (font < 0 || font >= OVL_FONT_COUNT || !fonts_make() || sFonts[font].texture == 0) {
        return;
    }
    f = &sFonts[font];
    width = ovl_text_width(font, text);
    if (align == OVL_ALIGN_CENTRE) {
        x -= width * 0.5f;
    } else if (align == OVL_ALIGN_RIGHT) {
        x -= width;
    }
    display_map();
    // A 16:9 picture sent in 480 lines has narrower pixels than it has tall
    // ones: the glyphs are made square and narrowed as they are drawn.
    squeeze = sScaleX / sScaleY;
    pen = floorf(disp_x(x) + 0.5f);
    base = floorf(disp_y(y) + (f->ascent + f->descent) * 0.5f + 0.5f);
    em = stbtt_ScaleForPixelHeight(&sInfo, kFontSize[font] * f->scale);
    for (; *text && n < MAX_TEXT; text++) {
        int c = (unsigned char) *text;
        stbtt_aligned_quad q;
        float gx = 0.0f, gy = 0.0f, x0, x1;
        stbtt_packedchar *pc;

        if (c < FIRST_CHAR || c >= FIRST_CHAR + CHAR_COUNT) {
            continue;
        }
        if (prev) {
            pen += floorf(stbtt_GetCodepointKernAdvance(&sInfo, prev, c) * em * squeeze + 0.5f);
        }
        prev = c;
        pc = &f->chars[c - FIRST_CHAR];
        stbtt_GetPackedQuad(f->chars, ATLAS_W, ATLAS_H, c - FIRST_CHAR, &gx, &gy, &q, 1);
        x0 = pen + q.x0 * squeeze;
        x1 = pen + q.x1 * squeeze;
        if (q.x1 > q.x0) {
            GfxTriVert *v = &verts[n * 6];

            colour_vert(&v[0], x0, base + q.y0, colour);
            colour_vert(&v[1], x1, base + q.y0, colour);
            colour_vert(&v[2], x1, base + q.y1, colour);
            colour_vert(&v[3], x0, base + q.y0, colour);
            colour_vert(&v[4], x1, base + q.y1, colour);
            colour_vert(&v[5], x0, base + q.y1, colour);
            v[0].u = q.s0; v[0].v = q.t0;
            v[1].u = q.s1; v[1].v = q.t0;
            v[2].u = q.s1; v[2].v = q.t1;
            v[3].u = q.s0; v[3].v = q.t0;
            v[4].u = q.s1; v[4].v = q.t1;
            v[5].u = q.s0; v[5].v = q.t1;
            n++;
        }
        pen += floorf(pc->xadvance * squeeze + 0.5f);
    }
    if (n > 0) {
        // texel for pixel: unfiltered, unless the glyphs are being narrowed
        draw_state(f->texture, squeeze == 1.0f);
        gfx_draw_tris(verts, n * 6);
        if (kFontBold[font]) {
            int i;

            for (i = 0; i < n * 6; i++) {
                verts[i].x += 1.0f / sScaleX; // one display pixel on
            }
            gfx_draw_tris(verts, n * 6);
        }
    }
}
