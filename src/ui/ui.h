#ifndef UI_H
#define UI_H

#include <gsKit.h>
#if defined(__has_include)
#  if __has_include(<gsFontM.h>)
#    include <gsFontM.h>
#  elif __has_include(<gsFont.h>)
#    include <gsFont.h>
#  else
#    error "Neither gsFontM.h nor gsFont.h found in gsKit includes"
#  endif
#else
#  include <gsFont.h>
#endif
#include <dmaKit.h>
#include <stdint.h>
#include <stdarg.h>

/* ── Colour palette (GS RGBA, alpha 0x80 = fully opaque) ─────────────────── */
#define COL_BG          GS_SETREG_RGBA(0x08, 0x08, 0x10, 0x80)
#define COL_BG2         GS_SETREG_RGBA(0x10, 0x10, 0x1C, 0x80)
#define COL_PANEL       GS_SETREG_RGBA(0x16, 0x16, 0x28, 0x80)
#define COL_PANEL_DARK  GS_SETREG_RGBA(0x0C, 0x0C, 0x18, 0x80)
#define COL_ACCENT      GS_SETREG_RGBA(0x00, 0xB4, 0xD8, 0x80)
#define COL_ACCENT2     GS_SETREG_RGBA(0x48, 0xCA, 0xD9, 0x80)
#define COL_ACCENT_DIM  GS_SETREG_RGBA(0x00, 0x60, 0x78, 0x80)
#define COL_HIGHLIGHT   GS_SETREG_RGBA(0x02, 0x55, 0x8A, 0x80)
#define COL_SEL_BG      GS_SETREG_RGBA(0x00, 0x44, 0x6E, 0x80)
#define COL_BORDER      GS_SETREG_RGBA(0x00, 0x80, 0xA0, 0x80)
#define COL_BORDER_DIM  GS_SETREG_RGBA(0x20, 0x30, 0x40, 0x80)
#define COL_TEXT        GS_SETREG_RGBA(0xE8, 0xF0, 0xFE, 0x80)
#define COL_TEXT_DIM    GS_SETREG_RGBA(0x88, 0x99, 0xAA, 0x80)
#define COL_TEXT_TITLE  GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80)
#define COL_GREEN       GS_SETREG_RGBA(0x26, 0xD0, 0x7A, 0x80)
#define COL_RED         GS_SETREG_RGBA(0xE5, 0x39, 0x35, 0x80)
#define COL_YELLOW      GS_SETREG_RGBA(0xFF, 0xC1, 0x07, 0x80)
#define COL_WHITE       GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80)
#define COL_BLACK       GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80)

/* Construct a colour from RGB + full alpha */
#define RGB(r,g,b)      GS_SETREG_RGBA((r),(g),(b), 0x80)
/* Same with explicit alpha (0–0x80) */
#define RGBA(r,g,b,a)   GS_SETREG_RGBA((r),(g),(b),(a))

/* ── Z-layers ─────────────────────────────────────────────────────────────── */
#define Z_BG        1
#define Z_PANEL     2
#define Z_ITEM      3
#define Z_OVERLAY   4
#define Z_TEXT      5

/* ── Layout constants ────────────────────────────────────────────────────── */
#define HEADER_H    38
#define FOOTER_H    30
#define MARGIN      14
#define PADDING     8
#define CORNER_R    4     /* rounded-corner approximation (pixel steps) */

/* ── Text scale presets ──────────────────────────────────────────────────── */
#define SCALE_TINY    0.5f
#define SCALE_SMALL   0.6f
#define SCALE_NORMAL  0.75f
#define SCALE_MEDIUM  0.9f
#define SCALE_LARGE   1.2f
#define SCALE_TITLE   1.6f
#define SCALE_HUGE    2.2f

/* ── UIContext ────────────────────────────────────────────────────────────── */
typedef struct {
    GSGLOBAL *gs;
    GSFONTM  *font;          /* built-in metrics font                       */
    int       frame;         /* increments each rendered frame              */
    int       screen_w;
    int       screen_h;
} UIContext;

extern UIContext g_ui;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
void ui_init(void);
void ui_shutdown(void);
void ui_begin_frame(void);
void ui_end_frame(void);

/* ── Primitive drawing ───────────────────────────────────────────────────── */
void ui_fill(void);   /* clear to COL_BG */
void ui_rect(float x, float y, float w, float h, uint64_t color);
void ui_rect_outline(float x, float y, float w, float h,
                     uint64_t color, float thickness);
void ui_gradient_rect(float x, float y, float w, float h,
                      uint64_t c_tl, uint64_t c_tr,
                      uint64_t c_bl, uint64_t c_br);
void ui_line(float x1, float y1, float x2, float y2, uint64_t color);
void ui_hline(float x, float y, float w, uint64_t color);
void ui_vline(float x, float y, float h, uint64_t color);

/* ── Texture drawing ─────────────────────────────────────────────────────── */
void ui_sprite(GSTEXTURE *tex,
               float dst_x, float dst_y, float dst_w, float dst_h,
               uint64_t tint);
void ui_sprite_region(GSTEXTURE *tex,
                      float dst_x, float dst_y, float dst_w, float dst_h,
                      float u0, float v0, float u1, float v1,
                      uint64_t tint);

/* ── Text ────────────────────────────────────────────────────────────────── */
void  ui_text(float x, float y, float scale, uint64_t color,
              const char *fmt, ...);
void  ui_text_center(float cx, float y, float w,
                     float scale, uint64_t color, const char *fmt, ...);
void  ui_text_right(float rx, float y, float scale, uint64_t color,
                    const char *fmt, ...);
float ui_text_w(float scale, const char *str);
float ui_text_h(float scale);

/* ── Composite widgets ───────────────────────────────────────────────────── */
/* Top header bar */
void ui_header(const char *title, int net_ok, int dl_count);
/* Bottom hint bar */
void ui_footer(const char *left_hint, const char *right_hint);
/* Titled bordered panel */
void ui_panel(float x, float y, float w, float h, const char *title);
/* Animated spinner (loading indicator) */
void ui_spinner(float cx, float cy, float r, uint64_t color);
/* Horizontal progress bar */
void ui_progress_bar(float x, float y, float w, float h,
                     float pct,       /* 0.0 – 1.0 */
                     uint64_t bg_col, uint64_t bar_col);
/* Small labelled badge / pill */
void ui_badge(float x, float y, const char *label,
              uint64_t bg_col, uint64_t text_col);
/* Cover art placeholder (when no texture available) */
void ui_cover_placeholder(float x, float y, float w, float h,
                          const char *label);
/* Scrollbar on right edge of a list area */
void ui_scrollbar(float x, float y, float h, int total, int visible,
                  int first_visible);

/* ── VRAM texture helpers ─────────────────────────────────────────────────── */
GSTEXTURE *ui_tex_alloc(int w, int h, int psm);
void       ui_tex_free(GSTEXTURE *tex);
int        ui_tex_from_rgb(GSTEXTURE *tex,
                           const uint8_t *rgb, int w, int h);
/* Load cover art from raw JPEG bytes (uses TJpgDec internally) */
int        ui_tex_from_jpeg(GSTEXTURE *tex,
                            const uint8_t *data, int size);

/* ── Easing / lerp helpers ───────────────────────────────────────────────── */
float ui_lerp(float a, float b, float t);
float ui_ease_out_quad(float t);    /* t in [0,1] */
float ui_ease_in_out(float t);

#endif /* UI_H */
