/*
 * detail_screen.c
 *
 * Full-page game info view.
 *
 * Layout (640×480):
 *   ┌────────────────────────────────────────────────────────┐
 *   │  HEADER                                                │
 *   ├──────────────┬─────────────────────────────────────────┤
 *   │ Full cover    │  Title (large)                         │
 *   │ (160×200)     │  Genre tags / subjects                 │
 *   │               │  Description (wrapped, scrollable)    │
 *   │               │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─     │
 *   │               │  Size: xxxx MB         Downloads: nnn │
 *   │               │  Source: archive.org/…               │
 *   ├──────────────┴───────────────────┬────────────────────┤
 *   │  [X] Download   [O] Back          │ [Tri] Share ID    │
 *   └───────────────────────────────────┴────────────────────┘
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "detail_screen.h"
#include "ui.h"
#include "main.h"
#include "input/pad.h"
#include "catalog/catalog.h"
#include "catalog/genres.h"
#include "net/archive.h"

#define COVER_W  152
#define COVER_H  200
#define INFO_X   (MARGIN + COVER_W + 14)
#define INFO_W   (SCREEN_W - INFO_X - MARGIN)

static GameEntry   s_game;
static GSTEXTURE  *s_cover_tex  = NULL;
static int         s_desc_scroll = 0;
static int         s_confirm_dl  = 0;   /* 1 = "Press X again to confirm" */

/* ── Wrapped-text helper ─────────────────────────────────────────────────── */
static void draw_wrapped(const char *text, float x, float y,
                         float max_w, float scale, uint64_t color,
                         int skip_lines, int max_lines, int *total_out)
{
    char  buf[512];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    float lh  = ui_text_h(scale) + 2.0f;
    int   line = 0;
    char *p   = buf;

    while (*p && (max_lines < 0 || (line - skip_lines) < max_lines)) {
        /* Find as many chars as fit in max_w */
        int  n   = 0;
        char tmp[128];
        while (p[n] && p[n] != '\n') {
            tmp[n] = p[n];
            tmp[n + 1] = '\0';
            if (ui_text_w(scale, tmp) > max_w) break;
            n++;
        }
        /* Back-track to last space */
        if (p[n] && p[n] != '\n') {
            int back = n;
            while (back > 0 && p[back] != ' ') back--;
            if (back > 0) n = back;
        }
        /* Draw this line */
        if (line >= skip_lines) {
            char row[128];
            strncpy(row, p, (size_t)n);
            row[n] = '\0';
            ui_text(x, y + (float)(line - skip_lines) * lh,
                    scale, color, "%s", row);
        }
        /* Advance */
        p += n;
        if (*p == ' ' || *p == '\n') p++;
        line++;
    }
    if (total_out) *total_out = line;
}

/* ── Screen callbacks ─────────────────────────────────────────────────────── */

void detail_screen_init(void)
{
    int count = 0;
    const GameEntry *list = catalog_get_genre((Genre)g_state.selected_genre, &count);
    if (g_state.selected_game < count)
        memcpy(&s_game, &list[g_state.selected_game], sizeof(GameEntry));
    else
        memset(&s_game, 0, sizeof(GameEntry));

    s_desc_scroll = 0;
    s_confirm_dl  = 0;

    /* Load cover */
    if (!s_cover_tex)
        s_cover_tex = ui_tex_alloc(COVER_W, COVER_H, GS_PSM_CT32);

    const uint8_t *jpg  = NULL;
    int            jlen = 0;
    catalog_get_cover(s_game.identifier, &jpg, &jlen);
    if (jpg && jlen > 0)
        ui_tex_from_jpeg(s_cover_tex, jpg, jlen);
}

void detail_screen_update(void)
{
    if (pad_pressed(PAD_UP)   && s_desc_scroll > 0) s_desc_scroll--;
    if (pad_pressed(PAD_DOWN))                       s_desc_scroll++;

    if (pad_pressed(PAD_CROSS)) {
        if (!s_confirm_dl) {
            s_confirm_dl = 1;   /* first press → show confirm prompt */
        } else {
            /* Second press → enqueue download */
            catalog_enqueue_download(s_game.identifier);
            g_state.active_downloads++;
            s_confirm_dl = 0;
            app_switch_screen(SCREEN_DOWNLOAD);
        }
    }

    if (pad_pressed(PAD_CIRCLE)) {
        s_confirm_dl = 0;
        app_switch_screen(SCREEN_GENRE);
    }
}

void detail_screen_render(void)
{
    int net_ok = (g_state.net_status == NET_CONNECTED);
    ui_header(s_game.title[0] ? s_game.title : "Game Detail",
              net_ok, g_state.active_downloads);

    float cy = (float)HEADER_H + 12.0f;

    /* ── Cover ────────────────────────────────────────────────── */
    float cx = (float)MARGIN;
    if (s_cover_tex && s_cover_tex->Mem) {
        ui_rect(cx + 4, cy + 4, COVER_W, COVER_H, RGBA(0,0,0,0x50));
        ui_sprite(s_cover_tex, cx, cy, COVER_W, COVER_H, COL_WHITE);
        ui_rect_outline(cx - 1, cy - 1, COVER_W + 2, COVER_H + 2,
                        COL_ACCENT, 2.0f);
    } else {
        ui_cover_placeholder(cx, cy, COVER_W, COVER_H, s_game.title);
    }

    /* ── Title ────────────────────────────────────────────────── */
    float iy = cy;
    ui_text(INFO_X, iy, SCALE_LARGE, COL_TEXT_TITLE, "%s", s_game.title);
    iy += ui_text_h(SCALE_LARGE) + 6;

    /* ── Genre badge ──────────────────────────────────────────── */
    Genre genre = (Genre)g_state.selected_genre;
    GenreColour gc = GENRE_COLOURS[genre];
    uint64_t gc_col = RGB(gc.r, gc.g, gc.b);
    ui_badge(INFO_X, iy, GENRE_NAMES[genre], gc_col, COL_WHITE);
    iy += ui_text_h(SCALE_TINY) + 8 + 8;

    /* ── Metadata row ─────────────────────────────────────────── */
    ui_hline(INFO_X, iy, INFO_W, COL_BORDER_DIM);
    iy += 4;

    if (s_game.size_bytes > 0) {
        float mb = (float)s_game.size_bytes / (1024.0f * 1024.0f);
        if (mb > 1024.0f)
            ui_text(INFO_X, iy, SCALE_SMALL, COL_TEXT_DIM,
                    "Size: %.2f GB", mb / 1024.0f);
        else
            ui_text(INFO_X, iy, SCALE_SMALL, COL_TEXT_DIM,
                    "Size: %.0f MB", mb);
    } else {
        ui_text(INFO_X, iy, SCALE_SMALL, COL_TEXT_DIM, "Size: unknown");
    }

    if (s_game.download_count > 0) {
        ui_text_right(INFO_X + INFO_W, iy, SCALE_SMALL, COL_TEXT_DIM,
                      "Downloads: %d", s_game.download_count);
    }
    iy += ui_text_h(SCALE_SMALL) + 4;

    /* Archive identifier */
    ui_text(INFO_X, iy, SCALE_TINY, COL_ACCENT_DIM,
            "ID: %s", s_game.identifier);
    iy += ui_text_h(SCALE_TINY) + 8;

    ui_hline(INFO_X, iy, INFO_W, COL_BORDER_DIM);
    iy += 6;

    /* ── Description ──────────────────────────────────────────── */
    float desc_h = (float)(SCREEN_H - FOOTER_H - (int)iy - 4);
    int   max_lines = (int)(desc_h / (ui_text_h(SCALE_SMALL) + 2));
    int   total_lines = 0;

    if (s_game.description[0]) {
        draw_wrapped(s_game.description, INFO_X, iy,
                     (float)INFO_W, SCALE_SMALL, COL_TEXT,
                     s_desc_scroll, max_lines, &total_lines);

        /* Scrollbar for description */
        if (total_lines > max_lines)
            ui_scrollbar((float)(INFO_X + INFO_W + 2), iy, desc_h,
                         total_lines, max_lines, s_desc_scroll);
    } else {
        ui_text(INFO_X, iy, SCALE_SMALL, COL_TEXT_DIM,
                "No description available.");
    }

    /* ── Confirm download overlay ─────────────────────────────── */
    if (s_confirm_dl) {
        float bw = 360, bh = 70;
        float bx = (SCREEN_W - bw) * 0.5f;
        float by = (SCREEN_H - bh) * 0.5f;
        ui_rect(bx, by, bw, bh, COL_PANEL);
        ui_rect_outline(bx, by, bw, bh, COL_ACCENT, 2.0f);
        ui_text_center(bx, by + 10, bw, SCALE_NORMAL, COL_YELLOW,
                       "Confirm download?");
        ui_text_center(bx, by + 38, bw, SCALE_SMALL, COL_TEXT,
                       "[X] Yes — Queue it!   [O] Cancel");
    }

    ui_footer("[X] Download  [O] Back", "[Up/Down] Scroll desc");
}

void detail_screen_destroy(void)
{
    /* Cover texture stays alive (owned by genre cache) */
    s_cover_tex = NULL;
}
