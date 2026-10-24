/* gfx_soft.c -- dkr_gfx.h drawn in software, for the PC test harness.
 *
 * The same pipe the Xbox's Direct3D backend (Platform/xbox/gfx_xbox.c) sets
 * up, rasterised on the CPU at twice the N64's resolution: depth test and
 * write (1/w, compared greater, as there), the decal offset, perspective-correct texturing (wrap, mirror,
 * clamp; point or bilinear), Gouraud colour, the two texture environments,
 * alpha test, source-alpha blending, vertex fog and the scissor. Its frames
 * are what host_main.c saves as screenshots. Not fast, and not meant to be. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dkr_gfx.h"

#define SCALE 2
#define FB_W (320 * SCALE)
#define FB_H (240 * SCALE)

#define MAX_TEX 4096

typedef struct {
    int used;
    int w, h, cmS, cmT;
    unsigned char *rgba;
} SoftTex;

static unsigned int sFb[FB_W * FB_H]; /* 0xAABBGGRR */
static float sDepth[FB_W * FB_H];
static SoftTex sTex[MAX_TEX];
static int sBound;
static int sPoint;
static int sFog;
static unsigned char sFogColor[4];
static int sBlendEnv;
static unsigned char sEnvColor[4];
static int sDepthTest = 1, sDepthWrite = 1, sDepthOffset;
static float sAlphaRef;
static int sScX0, sScY0, sScX1 = FB_W, sScY1 = FB_H;

const unsigned int *soft_framebuffer(int *w, int *h)
{
    *w = FB_W;
    *h = FB_H;
    return sFb;
}

void gfx_init(void)
{
}

double gSoftDlMs; /* the display list's own time, for host_main.c --time */
static double sDlStart;

static double soft_ms(void)
{
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void gfx_frame_begin(void)
{
    int i;

    sDlStart = soft_ms();

    for (i = 0; i < FB_W * FB_H; i++)
    {
        sFb[i] = 0xFF000000u;
        sDepth[i] = 0.0f;
    }
}

unsigned int gfx_create_texture(const void *rgba, int width, int height, int cmS, int cmT)
{
    int i;

    for (i = 1; i < MAX_TEX; i++)
        if (!sTex[i].used)
            break;
    if (i == MAX_TEX)
        return 0;
    sTex[i].rgba = (unsigned char *)malloc((size_t)width * height * 4);
    if (sTex[i].rgba == NULL)
        return 0;
    memcpy(sTex[i].rgba, rgba, (size_t)width * height * 4);
    sTex[i].used = 1;
    sTex[i].w = width;
    sTex[i].h = height;
    sTex[i].cmS = cmS;
    sTex[i].cmT = cmT;
    return (unsigned int)i;
}

void gfx_delete_texture(unsigned int handle)
{
    if (handle == 0 || handle >= MAX_TEX || !sTex[handle].used)
        return;
    free(sTex[handle].rgba);
    sTex[handle].used = 0;
}

void gfx_bind_texture(unsigned int handle)
{
    sBound = (handle < MAX_TEX && sTex[handle].used) ? (int)handle : 0;
}

void gfx_set_texture_filter(int point)
{
    sPoint = point;
}

void gfx_set_fog(int enable, const unsigned char color[4])
{
    sFog = enable;
    memcpy(sFogColor, color, 4);
}

void gfx_set_texenv_blend(const unsigned char color[4])
{
    sBlendEnv = 1;
    memcpy(sEnvColor, color, 4);
}

void gfx_set_texenv_modulate(void)
{
    sBlendEnv = 0;
}

void gfx_set_depth_test(int enable)
{
    sDepthTest = enable;
}

void gfx_set_depth_write(int enable)
{
    sDepthWrite = enable;
}

void gfx_set_depth_offset(int enable)
{
    sDepthOffset = enable;
}

void gfx_set_alpha_test(float ref)
{
    sAlphaRef = ref;
}

void gfx_set_scissor(float x0, float y0, float x1, float y1)
{
    sScX0 = (int)(x0 * SCALE);
    sScY0 = (int)(y0 * SCALE);
    sScX1 = (int)(x1 * SCALE);
    sScY1 = (int)(y1 * SCALE);
    if (sScX0 < 0) sScX0 = 0;
    if (sScY0 < 0) sScY0 = 0;
    if (sScX1 > FB_W) sScX1 = FB_W;
    if (sScY1 > FB_H) sScY1 = FB_H;
}

void gfx_disable_scissor(void)
{
    sScX0 = sScY0 = 0;
    sScX1 = FB_W;
    sScY1 = FB_H;
}

void gfx_display_map(float *scaleX, float *scaleY, float *left, float *top)
{
    *scaleX = *scaleY = (float)SCALE;
    *left = *top = 0.0f;
}

static int wrap(int i, int n, int cm)
{
    if (cm & 2)
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    if (cm & 1)
    {
        int p = ((i % (2 * n)) + 2 * n) % (2 * n);

        return p < n ? p : 2 * n - 1 - p;
    }
    return ((i % n) + n) % n;
}

static void texel(const SoftTex *t, int x, int y, float out[4])
{
    const unsigned char *p = t->rgba + ((size_t)wrap(y, t->h, t->cmT) * t->w + wrap(x, t->w, t->cmS)) * 4;

    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = p[3];
}

static void sample(const SoftTex *t, float u, float v, float out[4])
{
    float x = u * t->w, y = v * t->h;

    if (sPoint)
    {
        texel(t, (int)floorf(x), (int)floorf(y), out);
    }
    else
    {
        float a[4], b[4], c[4], d[4], fx, fy;
        int x0, y0, k;

        x -= 0.5f;
        y -= 0.5f;
        x0 = (int)floorf(x);
        y0 = (int)floorf(y);
        fx = x - x0;
        fy = y - y0;
        texel(t, x0, y0, a);
        texel(t, x0 + 1, y0, b);
        texel(t, x0, y0 + 1, c);
        texel(t, x0 + 1, y0 + 1, d);
        for (k = 0; k < 4; k++)
            out[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
    }
}

/* one pixel: colour (0..255), fog (0..1), depth */
static void shade(int x, int y, float z, const float col[4], float u, float v, float fog)
{
    float c[4], s[4];
    unsigned int *dst;
    float a;
    int k;

    if (x < sScX0 || x >= sScX1 || y < sScY0 || y >= sScY1)
        return;
    if (sDepthOffset)
        z *= 1.0005f;
    if (sDepthTest && !(z > sDepth[y * FB_W + x]))
        return;
    memcpy(c, col, sizeof(c));
    if (sBound)
    {
        sample(&sTex[sBound], u, v, s);
        if (sBlendEnv)
        {
            for (k = 0; k < 3; k++)
                c[k] = c[k] + (sEnvColor[k] - c[k]) * (s[k] / 255.0f);
            c[3] = c[3] * s[3] / 255.0f;
        }
        else
        {
            for (k = 0; k < 4; k++)
                c[k] = c[k] * s[k] / 255.0f;
        }
    }
    if (!(c[3] / 255.0f > sAlphaRef))
        return;
    if (sFog)
        for (k = 0; k < 3; k++)
            c[k] = c[k] + (sFogColor[k] - c[k]) * fog;
    if (sDepthWrite)
        sDepth[y * FB_W + x] = z;
    dst = &sFb[y * FB_W + x];
    a = c[3] / 255.0f;
    {
        float r = (float)(*dst & 0xFF), g = (float)((*dst >> 8) & 0xFF), b = (float)((*dst >> 16) & 0xFF);

        r = c[0] * a + r * (1 - a);
        g = c[1] * a + g * (1 - a);
        b = c[2] * a + b * (1 - a);
        *dst = 0xFF000000u | ((unsigned)(b + 0.5f) << 16) | ((unsigned)(g + 0.5f) << 8) | (unsigned)(r + 0.5f);
    }
}

typedef struct {
    float x, y, z, iw;       /* screen, depth, 1/w */
    float uw, vw;            /* u/w, v/w */
    float c[4], fog;
} Sv;

static void raster(const Sv *a, const Sv *b, const Sv *c)
{
    float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    int minx, maxx, miny, maxy, x, y, k;

    if (area == 0.0f)
        return;
    minx = (int)floorf(fminf(a->x, fminf(b->x, c->x)));
    maxx = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    miny = (int)floorf(fminf(a->y, fminf(b->y, c->y)));
    maxy = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (minx < sScX0) minx = sScX0;
    if (miny < sScY0) miny = sScY0;
    if (maxx > sScX1) maxx = sScX1;
    if (maxy > sScY1) maxy = sScY1;
    for (y = miny; y < maxy; y++)
    {
        float py = y + 0.5f;

        for (x = minx; x < maxx; x++)
        {
            float px = x + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) / area;
            float w1 = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) / area;
            float w2 = 1.0f - w0 - w1;
            float iw, col[4], u, v;

            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            iw = a->iw * w0 + b->iw * w1 + c->iw * w2;
            u = (a->uw * w0 + b->uw * w1 + c->uw * w2) / iw;
            v = (a->vw * w0 + b->vw * w1 + c->vw * w2) / iw;
            for (k = 0; k < 4; k++)
                col[k] = a->c[k] * w0 + b->c[k] * w1 + c->c[k] * w2;
            shade(x, y, a->z * w0 + b->z * w1 + c->z * w2, col, u, v,
                  a->fog * w0 + b->fog * w1 + c->fog * w2);
        }
    }
}

int gSoftNoRaster; /* host_main.c --time: the game's own work only */
unsigned long gSoftTris;

void gfx_draw_tris(const GfxTriVert *verts, int count)
{
    int i, j;

    gSoftTris += count / 3;
    if (gSoftNoRaster)
        return;
    for (i = 0; i + 2 < count; i += 3)
    {
        Sv s[3];

        for (j = 0; j < 3; j++)
        {
            const GfxTriVert *v = &verts[i + j];
            float iw = (v->w != 0.0f) ? 1.0f / v->w : 1.0f;

            s[j].x = v->x * SCALE;
            s[j].y = v->y * SCALE;
            s[j].z = iw; /* the depth, as gfx_xbox.c's: 1/w, nearer is larger */
            s[j].iw = iw;
            s[j].uw = v->u * iw;
            s[j].vw = v->v * iw;
            s[j].c[0] = v->r;
            s[j].c[1] = v->g;
            s[j].c[2] = v->b;
            s[j].c[3] = v->a;
            s[j].fog = v->fog;
        }
        raster(&s[0], &s[1], &s[2]);
    }
}

void host_frame_done(void);

void gfx_frame_end(void)
{
    gSoftDlMs += soft_ms() - sDlStart;
    host_frame_done();
}
