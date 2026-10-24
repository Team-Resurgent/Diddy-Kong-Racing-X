// dkr_gfx.h -- what the display-list interpreter (dkr_dl.c) draws with.
//
// The Linux port's linux/gfx.h, unchanged in meaning: a fixed-function
// triangle pipe in the N64's 320x240 screen space. The Xbox implements it with
// Direct3D 8 (gfx_xbox.c), the PC test harness with a software rasteriser
// (tools/xbox/hosttest/gfx_soft.c).
#ifndef DKR_GFX_H
#define DKR_GFX_H

typedef struct {
    // x, y in N64 screen pixels; z the NDC depth negated (nearer is LARGER: the
    // Linux renderer's convention, which its glOrtho flipped back); w the clip w
    float x, y, z, w;
    float u, v;       // normalised texture coordinates
    float fog;        // 0 = clear, 1 = fully fogged
    unsigned char r, g, b, a;
} GfxTriVert;

// Brings the video up. Called once, before the game draws.
void gfx_init(void);

void gfx_frame_begin(void);

// Uploads an RGBA8888 image (bytes r, g, b, a) and returns a handle for
// gfx_bind_texture(). `cmS`/`cmT` are the RDP's raw 2-bit clamp/mirror fields
// for each axis (G_TX_WRAP / G_TX_MIRROR / G_TX_CLAMP).
unsigned int gfx_create_texture(const void *rgba, int width, int height, int cmS, int cmT);

// Binds a texture for subsequent draws. Handle 0 means untextured (the
// triangles are then shaded from vertex colours alone).
void gfx_bind_texture(unsigned int handle);

// Point- or bilinear-samples the currently bound texture -- the RDP's G_TF_POINT
// vs. G_TF_BILERP.
void gfx_set_texture_filter(int point);

// Fades fragments towards `color` by each vertex's fog coordinate.
void gfx_set_fog(int enable, const unsigned char color[4]);

// How the texel and the vertex colour are combined. `modulate` is texel *
// colour; `blend` lerps from the vertex colour to `color` by the texel (GL's
// GL_BLEND texture env), alpha still texel * vertex.
void gfx_set_texenv_blend(const unsigned char color[4]);
void gfx_set_texenv_modulate(void);

// Releases a texture created by gfx_create_texture().
void gfx_delete_texture(unsigned int handle);

// Draws `count` vertices as a triangle list.
void gfx_draw_tris(const GfxTriVert *verts, int count);

// The RDP's Z_CMP, Z_UPD and ZMODE_DEC.
void gfx_set_depth_test(int enable);
void gfx_set_depth_write(int enable);
void gfx_set_depth_offset(int enable);

// Discards fragments whose alpha is not greater than `ref` (0..1).
void gfx_set_alpha_test(float ref);

// Clips all drawing to a rectangle in N64 screen pixels, x1/y1 exclusive.
void gfx_set_scissor(float x0, float y0, float x1, float y1);
void gfx_disable_scissor(void);

// Presents the frame.
void gfx_frame_end(void);

// Where the N64 screen lies on the display: N64 x lands on the edge between
// display pixels x * scaleX + left (y likewise). The settings menu's HD drawing
// (dkr_overlay.c) puts its text on whole display pixels by it.
void gfx_display_map(float *scaleX, float *scaleY, float *left, float *top);

#endif
