/* gfx_xbox.c -- dkr_gfx.h on the NV2A, through Direct3D 8.
 *
 * The display-list interpreter (dkr_dl.c) hands over triangles already
 * transformed, clipped against the near plane and projected into the N64's
 * 320x240 screen, with their colours run through the RDP's combiner. They are
 * drawn here as pre-transformed vertices (XYZRHW): the screen position scaled
 * to the television's mode, rhw = 1/w so the NV2A interpolates the texture
 * perspective-correctly as the RDP does, the combiner's colour as the diffuse
 * colour and the RSP's fog factor in the specular alpha (Direct3D's vertex
 * fog).
 *
 * Depth. The interpreter clips at w = 1, not at the projection's near plane,
 * so a few percent of what it hands over lies nearer than that plane: its NDC
 * depth is below -1, outside anything a 0..1 depth buffer holds, and clamped
 * there it flattens the depth across a whole triangle (a water surface reaching
 * the camera then failed against the bed under it, here and there, frame to
 * frame). So the depth is 1/w instead, which every vertex in front of w = 1
 * has in (0, 1]: compared GREATER (nearer is larger), cleared to 0, in the
 * NV2A's 24-bit float depth buffer, whose precision follows the value down
 * -- as the PVR's 1/w depth does for the Dreamcast port (and as the Doom 64
 * port's Xbox renderer does). 1/w is affine in screen space, so the depth the
 * rasteriser interpolates is exact. A decal (ZMODE_DEC) is moved nearer by a
 * small fraction of its 1/w, the same in depth at any distance.
 *
 * The mode is the dashboard's, as in the port's other titles: 720p when the
 * HD pack is in and 720p is ticked, else 480 lines (progressive when 480p is
 * ticked). The game's 4:3 picture is 640x480 at 480 lines and 960x720 at
 * 720p, centred; on a 16:9 television sent 480 lines (anamorphic) it is
 * drawn a quarter narrower, so it keeps its shape. Black either side.
 *
 * Textures are RGBA8888 from the interpreter's decoder. The NV2A's swizzled
 * textures are a power of two each way; a texture that is not is padded to
 * one along an axis it clamps on (its coordinates scaled to match) and
 * resampled to one along an axis it repeats on, so wrapping still works. */
#include "xbox_xtl.h"
#include <xgraphics.h>

#include <stdlib.h>
#include <string.h>

#include "dkr_gfx.h"
#include "xbox_port.h"

#define MAX_TEX 4096
#define MAX_VERTS 3072 /* per DrawPrimitiveUP */
/* a decal's depth, 1/w, scaled nearer by this: a twentieth of a percent of
 * its distance */
#define DECAL_NEARER 1.0005f

typedef struct {
    float x, y, z, rhw;
    D3DCOLOR diffuse, specular;
    float u, v;
} XbVert;

#define XB_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)

typedef struct {
    LPDIRECT3DTEXTURE8 tex;
    float su, sv; /* the image's share of the texture, when padded */
    int cmS, cmT;
} XbTex;

static LPDIRECT3D8 sD3D;
static LPDIRECT3DDEVICE8 sDev;
static int sDispW, sDispH, sRefresh = 60;
static float sScaleX, sScaleY, sOffX, sOffY;

static XbTex sTex[MAX_TEX];
static int sFreeTex[MAX_TEX], sFreeCount;
static struct {
    LPDIRECT3DTEXTURE8 tex;
    unsigned frame;
} sPending[MAX_TEX];
static int sPendingCount;
static unsigned sFrame; /* frames presented */
static unsigned int sBound;
static XbVert sVerts[MAX_VERTS];

/* what the device has, so a call is only made for a change */
static int sDepthTest = -1, sDepthWrite = -1, sDepthOffset = -1, sFog = -1, sPoint = -1;
static int sTexMode = -1; /* 0 untextured, 1 modulate, 2 blend */
static int sBlend;        /* the texture environment asked for */
static DWORD sAlphaRef = 0xFFFFFFFFu, sFogColor, sEnvColor = 0xFFFFFFFFu;
static int sAddrU = -1, sAddrV = -1;

/* ============================================================== video */

int xb_gfx_init(void)
{
    D3DPRESENT_PARAMETERS pp;
    DWORD vflags = XGetVideoFlags();
    int want720 = XGetAVPack() == XC_AV_PACK_HDTV && (vflags & XC_VIDEO_FLAGS_HDTV_720p);
    int pal50 = XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I && !(vflags & XC_VIDEO_FLAGS_PAL_60Hz);
    int wide480 = 0;
    HRESULT hr;
    int i;

    sD3D = Direct3DCreate8(D3D_SDK_VERSION);
    if (sD3D == NULL)
        return -1;
    for (;;)
    {
        memset(&pp, 0, sizeof(pp));
        if (want720)
        {
            sDispW = 1280;
            sDispH = 720;
            pp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
        }
        else
        {
            sDispW = 640;
            sDispH = 480;
            if (vflags & XC_VIDEO_FLAGS_HDTV_480p)
                pp.Flags = D3DPRESENTFLAG_PROGRESSIVE;
            if (vflags & XC_VIDEO_FLAGS_WIDESCREEN)
                pp.Flags |= D3DPRESENTFLAG_WIDESCREEN;
        }
        wide480 = !want720 && (vflags & XC_VIDEO_FLAGS_WIDESCREEN);
        pp.BackBufferWidth = (UINT)sDispW;
        pp.BackBufferHeight = (UINT)sDispH;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_F24S8;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        /* 60 Hz, unless the console is PAL with 60 Hz not allowed (the HD
         * modes are 60 Hz everywhere); the pacer turns 50 Hz fields into the
         * game's 60 Hz time */
        sRefresh = (pal50 && !want720 && !(vflags & XC_VIDEO_FLAGS_HDTV_480p)) ? 50 : 60;
        pp.FullScreen_RefreshRateInHz = (UINT)sRefresh;
        pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        hr = IDirect3D8_CreateDevice(sD3D, 0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                     &pp, &sDev);
        if (SUCCEEDED(hr) || !want720)
            break;
        xb_log("gfx: 720p failed (%08lx); 480 instead", (unsigned long)hr);
        want720 = 0;
    }
    if (FAILED(hr))
        return -1;

    /* the 4:3 picture in its place */
    sScaleY = (float)sDispH / 240.0f;
    sScaleX = sScaleY * (wide480 ? 0.75f : 1.0f);
    sOffX = ((float)sDispW - 320.0f * sScaleX) * 0.5f - 0.5f;
    sOffY = -0.5f;

    for (i = MAX_TEX - 1; i >= 1; i--)
        sFreeTex[sFreeCount++] = i;

    IDirect3DDevice8_SetVertexShader(sDev, XB_FVF);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ZFUNC, D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_DITHERENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_SPECULARENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice8_SetTextureStageState(sDev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice8_SetTextureStageState(sDev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 0.0f, 0);
    IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);
    xb_log("gfx: %dx%d%s at %d Hz, the picture %dx%d at %d", sDispW, sDispH,
           (pp.Flags & D3DPRESENTFLAG_PROGRESSIVE) ? "p" : "i", sRefresh, (int)(320.0f * sScaleX),
           (int)(240.0f * sScaleY), (int)(sOffX + 0.5f));
    return 0;
}

int xb_gfx_refresh(void)
{
    return sRefresh;
}

/* A solid screen with nothing else (the missing-data screen). */
void xb_gfx_solid(D3DCOLOR c)
{
    if (sDev == NULL)
        return;
    IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET, c, 1.0f, 0);
    IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);
}

void gfx_init(void)
{
    /* xbox_main.c brings the device up before the game starts */
}

/* ========================================================== textures */

static int next_pow2(int n)
{
    int p = 1;

    while (p < n)
        p <<= 1;
    return p;
}

unsigned int gfx_create_texture(const void *rgba, int width, int height, int cmS, int cmT)
{
    const unsigned char *src = (const unsigned char *)rgba;
    int pw = next_pow2(width), ph = next_pow2(height);
    /* along a repeating axis the image is stretched to the power of two;
     * along a clamped one it is padded with its own edge */
    int padS = (cmS & 2) != 0, padT = (cmT & 2) != 0;
    LPDIRECT3DTEXTURE8 tex = NULL;
    D3DLOCKED_RECT lr;
    DWORD *lin;
    unsigned int h;
    int x, y;

    if (sDev == NULL || sFreeCount == 0 || width <= 0 || height <= 0 || pw > 1024 || ph > 1024)
        return 0;
    lin = (DWORD *)malloc((size_t)pw * ph * 4);
    if (lin == NULL)
        return 0;
    for (y = 0; y < ph; y++)
    {
        int sy = padT ? (y < height ? y : height - 1) : (y * height / ph);

        for (x = 0; x < pw; x++)
        {
            int sx = padS ? (x < width ? x : width - 1) : (x * width / pw);
            const unsigned char *p = src + ((size_t)sy * width + sx) * 4;

            lin[y * pw + x] = ((DWORD)p[3] << 24) | ((DWORD)p[0] << 16) | ((DWORD)p[1] << 8) | p[2];
        }
    }
    /* Most of the game's textures are the N64's 16-bit RGBA (or palettes of
     * it): five bits a channel, alpha on or off. Those keep every bit in
     * A1R5G5B5 at half the memory; the rest (intensity, intensity-alpha,
     * 32-bit) stay A8R8G8B8. */
    {
        int n = pw * ph, k, fits = 1;

        for (k = 0; k < n && fits; k++)
        {
            DWORD p = lin[k], a = p >> 24, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;

            fits = (a == 0 || a == 0xFF) && r == ((r & 0xF8) | (r >> 5)) && g == ((g & 0xF8) | (g >> 5)) &&
                   b == ((b & 0xF8) | (b >> 5));
        }
        if (fits)
        {
            WORD *lin16 = (WORD *)lin; /* in place: each 16-bit texel behind its 32-bit one */

            for (k = 0; k < n; k++)
            {
                DWORD p = lin[k];

                lin16[k] = (WORD)(((p >> 24) ? 0x8000 : 0) | (((p >> 19) & 0x1F) << 10) | (((p >> 11) & 0x1F) << 5) |
                                  ((p >> 3) & 0x1F));
            }
        }
        if (FAILED(IDirect3DDevice8_CreateTexture(sDev, pw, ph, 1, 0, fits ? D3DFMT_A1R5G5B5 : D3DFMT_A8R8G8B8,
                                                  D3DPOOL_DEFAULT, &tex)))
        {
            free(lin);
            return 0;
        }
        if (FAILED(IDirect3DTexture8_LockRect(tex, 0, &lr, NULL, 0)))
        {
            IDirect3DTexture8_Release(tex);
            free(lin);
            return 0;
        }
        XGSwizzleRect(lin, pw * (fits ? 2 : 4), NULL, lr.pBits, pw, ph, NULL, fits ? 2 : 4);
        IDirect3DTexture8_UnlockRect(tex, 0);
        free(lin);
    }

    h = (unsigned int)sFreeTex[--sFreeCount];
    sTex[h].tex = tex;
    sTex[h].su = padS ? (float)width / (float)pw : 1.0f;
    sTex[h].sv = padT ? (float)height / (float)ph : 1.0f;
    sTex[h].cmS = cmS;
    sTex[h].cmT = cmT;
    return h;
}

void gfx_delete_texture(unsigned int handle)
{
    if (handle == 0 || handle >= MAX_TEX || sTex[handle].tex == NULL)
        return;
    if (sBound == handle)
    {
        IDirect3DDevice8_SetTexture(sDev, 0, NULL);
        sBound = 0;
    }
    /* The GPU may still draw with it: the frame being built, and the one
     * before it if that is still in the push buffer. The Xbox's Direct3D
     * frees a texture's memory on its last Release whatever the GPU is
     * doing, so the release waits two frames and until the GPU says it is
     * done with it; the slot is free now. */
    if (sPendingCount < MAX_TEX)
    {
        sPending[sPendingCount].tex = sTex[handle].tex;
        sPending[sPendingCount].frame = sFrame;
        sPendingCount++;
    }
    else
    {
        IDirect3DTexture8_BlockUntilNotBusy(sTex[handle].tex);
        IDirect3DTexture8_Release(sTex[handle].tex);
    }
    sTex[handle].tex = NULL;
    sFreeTex[sFreeCount++] = (int)handle;
}

void xb_gfx_release_pending(void)
{
    int i, kept = 0;

    for (i = 0; i < sPendingCount; i++)
    {
        /* two frames on, and the GPU done with it (it can run further behind) */
        if (sFrame - sPending[i].frame >= 2 && !IDirect3DTexture8_IsBusy(sPending[i].tex))
            IDirect3DTexture8_Release(sPending[i].tex);
        else
            sPending[kept++] = sPending[i];
    }
    sPendingCount = kept;
}

static DWORD address_mode(int cm)
{
    if (cm & 2)
        return D3DTADDRESS_CLAMP;
    if (cm & 1)
        return D3DTADDRESS_MIRROR;
    return D3DTADDRESS_WRAP;
}

void gfx_bind_texture(unsigned int handle)
{
    if (handle >= MAX_TEX || sTex[handle].tex == NULL)
        handle = 0;
    if (handle != sBound)
    {
        IDirect3DDevice8_SetTexture(sDev, 0, handle ? (D3DBaseTexture *)sTex[handle].tex : NULL);
        sBound = handle;
    }
    if (handle)
    {
        int u = (int)address_mode(sTex[handle].cmS), v = (int)address_mode(sTex[handle].cmT);

        if (u != sAddrU)
        {
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ADDRESSU, (DWORD)u);
            sAddrU = u;
        }
        if (v != sAddrV)
        {
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ADDRESSV, (DWORD)v);
            sAddrV = v;
        }
    }
}

void gfx_set_texture_filter(int point)
{
    if (point == sPoint)
        return;
    sPoint = point;
    IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_MINFILTER, point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_MAGFILTER, point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
}

/* the texture stage for what is bound and asked for */
static void apply_texenv(void)
{
    int mode = sBound == 0 ? 0 : (sBlend ? 2 : 1);

    if (mode == sTexMode)
        return;
    sTexMode = mode;
    switch (mode)
    {
        case 0:
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            break;
        case 1:
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            break;
        default:
            /* GL's GL_BLEND env: the vertex colour towards the constant by the
             * texel, channel by channel; alpha still texel * vertex */
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLOROP, D3DTOP_LERP);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG0, D3DTA_TEXTURE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            IDirect3DDevice8_SetTextureStageState(sDev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            break;
    }
}

void gfx_set_texenv_blend(const unsigned char color[4])
{
    DWORD c = ((DWORD)color[3] << 24) | ((DWORD)color[0] << 16) | ((DWORD)color[1] << 8) | color[2];

    sBlend = 1;
    if (c != sEnvColor)
    {
        IDirect3DDevice8_SetRenderState(sDev, D3DRS_TEXTUREFACTOR, c);
        sEnvColor = c;
    }
}

void gfx_set_texenv_modulate(void)
{
    sBlend = 0;
}

void gfx_set_fog(int enable, const unsigned char color[4])
{
    enable = enable != 0;
    if (enable != sFog)
    {
        IDirect3DDevice8_SetRenderState(sDev, D3DRS_FOGENABLE, (DWORD)enable);
        sFog = enable;
    }
    if (enable)
    {
        DWORD c = 0xFF000000u | ((DWORD)color[0] << 16) | ((DWORD)color[1] << 8) | color[2];

        if (c != sFogColor)
        {
            IDirect3DDevice8_SetRenderState(sDev, D3DRS_FOGCOLOR, c);
            sFogColor = c;
        }
    }
}

/* ============================================================ states */

void gfx_set_depth_test(int enable)
{
    enable = enable != 0;
    if (enable == sDepthTest)
        return;
    sDepthTest = enable;
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ZENABLE, enable ? D3DZB_TRUE : D3DZB_FALSE);
}

void gfx_set_depth_write(int enable)
{
    enable = enable != 0;
    if (enable == sDepthWrite)
        return;
    sDepthWrite = enable;
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ZWRITEENABLE, (DWORD)enable);
}

void gfx_set_depth_offset(int enable)
{
    enable = enable != 0;
    if (enable == sDepthOffset)
        return;
    sDepthOffset = enable; /* applied to each vertex's depth (gfx_draw_tris) */
}

void gfx_set_alpha_test(float ref)
{
    DWORD r = (DWORD)(ref * 255.0f + 0.5f);

    if (r > 255)
        r = 255;
    if (r == sAlphaRef)
        return;
    sAlphaRef = r;
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ALPHAREF, r);
}

/* The scissor, kept within the screen. The game's can lie partly or wholly
 * off it, or be inside out: the track select's preview window, sliding off
 * the left edge, sends a right edge of -78 against a left one of 0. The RDP
 * draws nothing there; handed to the NV2A as an empty rectangle, it hung the
 * console. So an empty one draws nothing here, and is never sent. */
static int sScissorEmpty;

void gfx_set_scissor(float x0, float y0, float x1, float y1)
{
    D3DRECT r;

    r.x1 = (LONG)(x0 * sScaleX + sOffX + 0.5f);
    r.y1 = (LONG)(y0 * sScaleY + sOffY + 0.5f);
    r.x2 = (LONG)(x1 * sScaleX + sOffX + 0.5f);
    r.y2 = (LONG)(y1 * sScaleY + sOffY + 0.5f);
    if (r.x1 < 0)
        r.x1 = 0;
    if (r.y1 < 0)
        r.y1 = 0;
    if (r.x2 > sDispW)
        r.x2 = sDispW;
    if (r.y2 > sDispH)
        r.y2 = sDispH;
    sScissorEmpty = r.x2 <= r.x1 || r.y2 <= r.y1;
    if (!sScissorEmpty)
        IDirect3DDevice8_SetScissors(sDev, 1, FALSE, &r);
}

void gfx_display_map(float *scaleX, float *scaleY, float *left, float *top)
{
    /* sOffX/Y carry Direct3D's half-pixel shift; the pixel edges are half a
     * pixel on */
    *scaleX = sScaleX;
    *scaleY = sScaleY;
    *left = sOffX + 0.5f;
    *top = sOffY + 0.5f;
}

void gfx_disable_scissor(void)
{
    sScissorEmpty = 0;
    IDirect3DDevice8_SetScissors(sDev, 0, FALSE, NULL);
}

/* ============================================================= draws */

static D3DCOLOR argb(const GfxTriVert *v)
{
    return ((DWORD)v->a << 24) | ((DWORD)v->r << 16) | ((DWORD)v->g << 8) | v->b;
}

void gfx_draw_tris(const GfxTriVert *verts, int count)
{
    float su = 1.0f, sv = 1.0f;
    int done = 0;

    if (sDev == NULL || count < 3 || sScissorEmpty)
        return;
    apply_texenv();
    if (sBound)
    {
        su = sTex[sBound].su;
        sv = sTex[sBound].sv;
    }
    count -= count % 3;
    while (done < count)
    {
        int n = count - done, i;

        if (n > MAX_VERTS)
            n = MAX_VERTS;
        for (i = 0; i < n; i++)
        {
            const GfxTriVert *s = &verts[done + i];
            XbVert *d = &sVerts[i];
            /* the depth: 1/w (see the top of this file), a decal's a little
             * nearer */
            float rhw = (s->w >= 1.0f) ? 1.0f / s->w : 1.0f;
            float z = (sDepthOffset > 0) ? rhw * DECAL_NEARER : rhw;
            float fog = s->fog;

            if (z > 1.0f)
                z = 1.0f;
            if (fog < 0.0f)
                fog = 0.0f;
            if (fog > 1.0f)
                fog = 1.0f;
            d->x = s->x * sScaleX + sOffX;
            d->y = s->y * sScaleY + sOffY;
            d->z = z;
            d->rhw = rhw;
            d->diffuse = argb(s);
            /* Direct3D's fog factor: 1 is clear, 0 is the fog colour */
            d->specular = (DWORD)((1.0f - fog) * 255.0f + 0.5f) << 24;
            d->u = s->u * su;
            d->v = s->v * sv;
        }
        IDirect3DDevice8_DrawPrimitiveUP(sDev, D3DPT_TRIANGLELIST, n / 3, sVerts, sizeof(XbVert));
        done += n;
    }
}

/* ============================================================ frames */

void gfx_frame_begin(void)
{
    if (sDev == NULL)
        return;
    /* a clear honours the scissor: lift it first */
    IDirect3DDevice8_SetScissors(sDev, 0, FALSE, NULL);
    sScissorEmpty = 0;
    IDirect3DDevice8_SetRenderState(sDev, D3DRS_ZWRITEENABLE, TRUE);
    sDepthWrite = 1;
    xb_at_game = XB_AT_DRAW;
    IDirect3DDevice8_Clear(sDev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 0.0f, 0);
    IDirect3DDevice8_BeginScene(sDev);
}

void gfx_frame_end(void)
{
    if (sDev == NULL)
        return;
    IDirect3DDevice8_EndScene(sDev);
    xb_at_game = XB_AT_PRESENT;
    IDirect3DDevice8_Present(sDev, NULL, NULL, NULL, NULL);
    xb_at_game = XB_AT_GAME;
    sFrame++;
    xb_gfx_release_pending();
    xb_frame_done();
}
