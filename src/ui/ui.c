/*
 * ui.c — gsKit rendering layer for PSBBN Downloader
 *
 * Wraps gsKit primitives into a reusable widget library.
 * All coordinates are in logical pixels (640×480 NTSC).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <gsKit.h>
#include <gsFont.h>
#include <dmaKit.h>
#include <malloc.h>

#include "ui.h"
#include "main.h"
#include "util/log.h"
#include "util/tjpgd.h"

/* TJpgDec workspace size (must be >= TJPGD_WORKSPACE_SIZE) */
#define JPEG_WORKSPACE_SIZE  3100

UIContext g_ui = {0};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void ui_init(void)
{
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    g_ui.gs = gsKit_init_global();
    if (!g_ui.gs) {
        LOGE("gsKit_init_global failed");
        return;
    }

    g_ui.gs->Mode           = GS_MODE_NTSC;
    g_ui.gs->Interlace      = GS_INTERLACE_ON;
    g_ui.gs->Field          = GS_FIELD_BOTTOM;
    g_ui.gs->PSM            = GS_PSM_CT32;
    g_ui.gs->PSMZ           = GS_PSMZ_16S;
    g_ui.gs->DoubleBuffering = GS_SETTING_ON;
    g_ui.gs->ZBuffering     = GS_SETTING_OFF;
    g_ui.gs->PrimAAEnable   = GS_SETTING_ON;

    gsKit_init_screen(g_ui.gs);
    gsKit_TexManager_init(g_ui.gs);

    g_ui.screen_w = g_ui.gs->Width;
    g_ui.screen_h = g_ui.gs->Height;
    g_ui.frame    = 0;

    /* Built-in metrics font — always available without external files */
    g_ui.font = gsKit_fontm_create(g_ui.gs, GSKIT_FONTM_METRICS);
    if (!g_ui.font) {
        LOGE("gsKit_fontm_create failed");
    }

    LOGI("UI initialised: %dx%d", g_ui.screen_w, g_ui.screen_h);
}

void ui_shutdown(void)
{
    if (g_ui.font) {
        gsKit_fontm_unload(g_ui.font);
        g_ui.font = NULL;
    }
    if (g_ui.gs) {
        gsKit_TexManager_free(g_ui.gs);
        /* gsKit has no shutdown function — GS hardware stays alive */
        g_ui.gs = NULL;
    }
}

void ui_begin_frame(void)
{
    gsKit_clear(g_ui.gs, COL_BG);
    gsKit_TexManager_nextFrame(g_ui.gs);
}

void ui_end_frame(void)
{
    gsKit_queue_exec(g_ui.gs);
    gsKit_sync_flip(g_ui.gs);
    g_ui.frame++;
}

/* ── Primitives ──────────────────────────────────────────────────────────── */

void ui_fill(void)
{
    gsKit_clear(g_ui.gs, COL_BG);
}

void ui_rect(float x, float y, float w, float h, uint64_t color)
{
    gsKit_prim_quad_filled(g_ui.gs,
                           x, y, x + w, y,
                           x, y + h, x + w, y + h,
                           Z_PANEL, color);
}

void ui_rect_outline(float x, float y, float w, float h,
                     uint64_t color, float t)
{
    /* top */
    ui_rect(x, y, w, t, color);
    /* bottom */
    ui_rect(x, y + h - t, w, t, color);
    /* left */
    ui_rect(x, y, t, h, color);
    /* right */
    ui_rect(x + w - t, y, t, h, color);
}

void ui_gradient_rect(float x, float y, float w, float h,
                      uint64_t c_tl, uint64_t c_tr,
                      uint64_t c_bl, uint64_t c_br)
{
    gsKit_prim_quad_gouraud_filled(g_ui.gs,
        x,     y,     Z_PANEL, c_tl,
        x+w,   y,     Z_PANEL, c_tr,
        x,     y+h,   Z_PANEL, c_bl,
        x+w,   y+h,   Z_PANEL, c_br);
}

void ui_line(float x1, float y1, float x2, float y2, uint64_t color)
{
    gsKit_prim_line(g_ui.gs, x1, y1, x2, y2, Z_TEXT, color);
}

void ui_hline(float x, float y, float w, uint64_t color)
{
    ui_line(x, y, x + w, y, color);
}

void ui_vline(float x, float y, float h, uint64_t color)
{
    ui_line(x, y, x, y + h, color);
}

/* ── Texture drawing ─────────────────────────────────────────────────────── */

void ui_sprite(GSTEXTURE *tex,
               float dx, float dy, float dw, float dh,
               uint64_t tint)
{
    gsKit_prim_sprite_texture(g_ui.gs, tex,
        dx,      dy,      0.0f, 0.0f,
        dx + dw, dy + dh, (float)tex->Width, (float)tex->Height,
        Z_ITEM, tint);
}

void ui_sprite_region(GSTEXTURE *tex,
                      float dx, float dy, float dw, float dh,
                      float u0, float v0, float u1, float v1,
                      uint64_t tint)
{
    gsKit_prim_sprite_texture(g_ui.gs, tex,
        dx, dy, u0, v0,
        dx + dw, dy + dh, u1, v1,
        Z_ITEM, tint);
}

/* ── Text ────────────────────────────────────────────────────────────────── */

void ui_text(float x, float y, float scale, uint64_t color,
             const char *fmt, ...)
{
    char buf[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    if (g_ui.font)
        gsKit_fontm_print_scaled(g_ui.gs, g_ui.font,
                                 x, y, Z_TEXT, scale, color, buf);
}

void ui_text_center(float cx, float y, float w,
                    float scale, uint64_t color, const char *fmt, ...)
{
    char buf[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    float tw = ui_text_w(scale, buf);
    float px = cx + (w - tw) * 0.5f;
    if (g_ui.font)
        gsKit_fontm_print_scaled(g_ui.gs, g_ui.font,
                                 px, y, Z_TEXT, scale, color, buf);
}

void ui_text_right(float rx, float y, float scale, uint64_t color,
                   const char *fmt, ...)
{
    char buf[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    float tw = ui_text_w(scale, buf);
    if (g_ui.font)
        gsKit_fontm_print_scaled(g_ui.gs, g_ui.font,
                                 rx - tw, y, Z_TEXT, scale, color, buf);
}

float ui_text_w(float scale, const char *str)
{
    if (!g_ui.font || !str) return 0.0f;
    return gsKit_fontm_width_scaled(g_ui.gs, g_ui.font, scale, str);
}

float ui_text_h(float scale)
{
    if (!g_ui.font) return 0.0f;
    return gsKit_fontm_height_scaled(g_ui.gs, g_ui.font, scale);
}

/* ── Composite widgets ───────────────────────────────────────────────────── */

void ui_header(const char *title, int net_ok, int dl_count)
{
    float W = (float)g_ui.screen_w;

    /* Dark gradient band */
    ui_gradient_rect(0, 0, W, HEADER_H,
                     COL_PANEL_DARK, COL_PANEL_DARK,
                     COL_PANEL,      COL_PANEL);
    ui_hline(0, HEADER_H - 1, W, COL_ACCENT);

    /* App name */
    ui_text(MARGIN, (HEADER_H - ui_text_h(SCALE_MEDIUM)) * 0.5f,
            SCALE_MEDIUM, COL_ACCENT, APP_NAME);

    /* Screen title (centred) */
    ui_text_center(0, (HEADER_H - ui_text_h(SCALE_NORMAL)) * 0.5f, W,
                   SCALE_NORMAL, COL_TEXT_TITLE, "%s", title);

    /* Right cluster: download count + net indicator */
    float rx = W - MARGIN;
    float ty = (HEADER_H - ui_text_h(SCALE_SMALL)) * 0.5f;

    uint64_t net_col = net_ok ? COL_GREEN : COL_RED;
    const char *net_str = net_ok ? "NET" : "OFF";
    ui_badge(rx - 34, ty - 2, net_str, net_col, COL_WHITE);

    if (dl_count > 0) {
        ui_text(rx - 80, ty, SCALE_SMALL, COL_YELLOW,
                "\x18%d", dl_count);   /* ↓ arrow + count */
    }
}

void ui_footer(const char *left_hint, const char *right_hint)
{
    float W = (float)g_ui.screen_w;
    float H = (float)g_ui.screen_h;
    float y = H - FOOTER_H;

    ui_hline(0, y, W, COL_BORDER_DIM);
    ui_gradient_rect(0, y, W, FOOTER_H,
                     COL_PANEL, COL_PANEL,
                     COL_PANEL_DARK, COL_PANEL_DARK);

    float ty = y + (FOOTER_H - ui_text_h(SCALE_SMALL)) * 0.5f;
    if (left_hint)
        ui_text(MARGIN, ty, SCALE_SMALL, COL_TEXT_DIM, "%s", left_hint);
    if (right_hint)
        ui_text_right(W - MARGIN, ty, SCALE_SMALL, COL_TEXT_DIM,
                      "%s", right_hint);
}

void ui_panel(float x, float y, float w, float h, const char *title)
{
    /* Slightly inset shadow */
    ui_rect(x + 2, y + 2, w, h, RGBA(0,0,0,0x40));
    ui_rect(x, y, w, h, COL_PANEL);
    ui_rect_outline(x, y, w, h, COL_BORDER_DIM, 1.0f);

    if (title && title[0]) {
        float th = ui_text_h(SCALE_SMALL) + PADDING;
        ui_rect(x, y, w, th, COL_PANEL_DARK);
        ui_hline(x, y + th, w, COL_BORDER);
        ui_text(x + PADDING, y + (th - ui_text_h(SCALE_SMALL)) * 0.5f,
                SCALE_SMALL, COL_ACCENT, "%s", title);
    }
}

void ui_spinner(float cx, float cy, float r, uint64_t color)
{
    /* Eight-segment rotating dot indicator */
    static const float angles[8] = {
        0.0f, 0.785f, 1.571f, 2.356f,
        3.142f, 3.927f, 4.712f, 5.498f
    };
    int tick = (g_ui.frame >> 2) & 7;
    for (int i = 0; i < 8; i++) {
        float a    = angles[(i + tick) & 7];
        float px   = cx + cosf(a) * r;
        float py   = cy + sinf(a) * r;
        float alpha = 0x80 - (0x60 * i / 7);
        uint64_t c  = RGBA((color >> 0)  & 0xFF,
                           (color >> 8)  & 0xFF,
                           (color >> 16) & 0xFF,
                           (int)alpha);
        ui_rect(px - 2, py - 2, 4, 4, c);
    }
}

void ui_progress_bar(float x, float y, float w, float h,
                     float pct, uint64_t bg_col, uint64_t bar_col)
{
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    ui_rect(x, y, w, h, bg_col);
    if (pct > 0.0f)
        ui_rect(x, y, w * pct, h, bar_col);
    ui_rect_outline(x, y, w, h, COL_BORDER_DIM, 1.0f);
}

void ui_badge(float x, float y, const char *label,
              uint64_t bg_col, uint64_t text_col)
{
    float tw = ui_text_w(SCALE_TINY, label);
    float bw = tw + 8.0f;
    float bh = ui_text_h(SCALE_TINY) + 4.0f;
    ui_rect(x, y, bw, bh, bg_col);
    ui_text(x + 4.0f, y + 2.0f, SCALE_TINY, text_col, "%s", label);
}

void ui_cover_placeholder(float x, float y, float w, float h,
                          const char *label)
{
    /* Dark box with a centred label */
    ui_rect(x, y, w, h, RGBA(0x15, 0x15, 0x25, 0x80));
    ui_rect_outline(x, y, w, h, COL_BORDER_DIM, 1.0f);

    /* "?" glyph centred */
    ui_text_center(x, y + h * 0.35f, w, SCALE_LARGE, COL_TEXT_DIM, "?");

    if (label && label[0]) {
        /* Truncate label to fit */
        char trunc[18];
        if ((int)strlen(label) > 17) {
            strncpy(trunc, label, 14);
            trunc[14] = '.'; trunc[15] = '.'; trunc[16] = '.';
            trunc[17] = '\0';
        } else {
            strncpy(trunc, label, sizeof(trunc) - 1);
            trunc[sizeof(trunc)-1] = '\0';
        }
        ui_text_center(x, y + h * 0.72f, w, SCALE_TINY, COL_TEXT_DIM,
                       "%s", trunc);
    }
}

void ui_scrollbar(float x, float y, float h,
                  int total, int visible, int first)
{
    if (total <= visible) return;
    ui_rect(x, y, 4, h, COL_PANEL_DARK);

    float ratio  = (float)visible / (float)total;
    float offset = (float)first   / (float)total;
    float bh = h * ratio;
    float by = y + h * offset;
    if (bh < 8.0f) bh = 8.0f;
    ui_rect(x, by, 4, bh, COL_ACCENT_DIM);
}

/* ── VRAM texture helpers ─────────────────────────────────────────────────── */

GSTEXTURE *ui_tex_alloc(int w, int h, int psm)
{
    GSTEXTURE *t = (GSTEXTURE *)memalign(128, sizeof(GSTEXTURE));
    if (!t) return NULL;
    memset(t, 0, sizeof(GSTEXTURE));
    t->Width  = w;
    t->Height = h;
    t->PSM    = psm;
    t->Filter = GS_FILTER_LINEAR;
    int bpp   = (psm == GS_PSM_CT32) ? 4 : 2;
    t->Mem    = memalign(128, w * h * bpp);
    if (!t->Mem) { free(t); return NULL; }
    return t;
}

void ui_tex_free(GSTEXTURE *tex)
{
    if (!tex) return;
    gsKit_TexManager_free(g_ui.gs);  /* flush all; TODO: per-tex free */
    if (tex->Mem) { free(tex->Mem); tex->Mem = NULL; }
    free(tex);
}

int ui_tex_from_rgb(GSTEXTURE *tex, const uint8_t *rgb, int w, int h)
{
    if (!tex || !rgb) return -1;
    tex->Width  = w;
    tex->Height = h;
    tex->PSM    = GS_PSM_CT32;
    tex->Filter = GS_FILTER_LINEAR;
    if (!tex->Mem)
        tex->Mem = memalign(128, w * h * 4);
    if (!tex->Mem) return -1;

    uint32_t *dst = (uint32_t *)tex->Mem;
    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgb[i * 3 + 0];
        uint8_t g = rgb[i * 3 + 1];
        uint8_t b = rgb[i * 3 + 2];
        dst[i] = ((uint32_t)0x80 << 24) | ((uint32_t)b << 16)
               | ((uint32_t)g << 8)     | r;
    }

    gsKit_texture_upload(g_ui.gs, tex);
    return 0;
}

/* ── TJpgDec JPEG → texture ─────────────────────────────────────────────── */

typedef struct {
    const uint8_t *src;
    int            pos;
    int            size;
} JpegSrc;

static unsigned int jpeg_input_cb(JDEC *jdec, uint8_t *buf, unsigned int nd)
{
    JpegSrc *src = (JpegSrc *)jdec->device;
    int rem = src->size - src->pos;
    if ((int)nd > rem) nd = (unsigned int)rem;
    if (buf)
        memcpy(buf, src->src + src->pos, nd);
    src->pos += (int)nd;
    return nd;
}

typedef struct {
    uint8_t *pixels;   /* output RGB */
    int      width;
} JpegOut;

static int jpeg_output_cb(JDEC *jdec, void *bitmap, JRECT *rect)
{
    JpegOut  *out   = (JpegOut *)jdec->device;
    uint8_t  *bm    = (uint8_t *)bitmap;
    int       stride = rect->right - rect->left + 1;
    for (int row = rect->top; row <= rect->bottom; row++) {
        int off = row * out->width + rect->left;
        memcpy(out->pixels + off * 3,
               bm + (row - rect->top) * stride * 3,
               stride * 3);
    }
    return 1;
}

int ui_tex_from_jpeg(GSTEXTURE *tex, const uint8_t *data, int size)
{
    if (!tex || !data || size <= 0) return -1;

    static uint8_t workspace[JPEG_WORKSPACE_SIZE];
    JDEC    jdec;
    JpegSrc src = { data, 0, size };

    JRESULT r = jd_prepare(&jdec, jpeg_input_cb, workspace,
                            JPEG_WORKSPACE_SIZE, &src);
    if (r != JDR_OK) {
        LOGE("jd_prepare failed: %d", r);
        return -1;
    }

    int w = jdec.width;
    int h = jdec.height;

    uint8_t *rgb = (uint8_t *)malloc((size_t)(w * h * 3));
    if (!rgb) return -1;

    JpegOut out = { rgb, w };
    /* reuse src — reset position, re-link device */
    src.pos     = 0;
    jdec.device = &out;

    r = jd_decomp(&jdec, jpeg_output_cb, 0);  /* scale factor 0 = full size */
    if (r != JDR_OK) {
        LOGE("jd_decomp failed: %d", r);
        free(rgb);
        return -1;
    }

    int ret = ui_tex_from_rgb(tex, rgb, w, h);
    free(rgb);
    return ret;
}

/* ── Easing helpers ──────────────────────────────────────────────────────── */

float ui_lerp(float a, float b, float t)   { return a + (b - a) * t; }
float ui_ease_out_quad(float t)             { return 1.0f - (1.0f - t) * (1.0f - t); }
float ui_ease_in_out(float t)
{
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}
