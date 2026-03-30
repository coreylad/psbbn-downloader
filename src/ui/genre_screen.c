/*
 * genre_screen.c
 *
 * Shows the game list for the selected genre.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  HEADER                                                  │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  Cover carousel (top)  ── 5 covers, centre selected ──   │
 *   ├────────┬─────────────────────────────────────┬──────────┤
 *   │  Cover │ Title / Size / Downloads / Subjects  │ Scrollbar│
 *   │ (big)  │ ─ row … (MAX_GAMES_PER_PAGE rows) ─  │          │
 *   │        │                                       │          │
 *   ├────────┴─────────────────────────────────────┴──────────┤
 *   │  FOOTER                                                   │
 *   └──────────────────────────────────────────────────────────┘
 *
 * Controller:
 *   D-pad Up/Down — navigate list
 *   L1 / R1       — page up / down
 *   Cross         — go to detail screen
 *   Circle        — back to home
 *   Square        — toggle sort (name / downloads)
 *   Triangle      — quick-download (bypass detail)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "genre_screen.h"
#include "detail_screen.h"
#include "download_screen.h"
#include "home_screen.h"
#include "ui.h"
#include "main.h"
#include "input/pad.h"
#include "catalog/catalog.h"
#include "catalog/genres.h"
#include "net/archive.h"

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define LIST_X           (MARGIN)
#define LIST_Y           (HEADER_H + 10)
#define COVER_THUMB_W    72
#define COVER_THUMB_H    96
#define CAROUSEL_Y       (LIST_Y)
#define CAROUSEL_H       (COVER_THUMB_H + 6)
#define LIST_AREA_Y      (CAROUSEL_Y + CAROUSEL_H + 6)
#define LIST_AREA_H      (SCREEN_H - LIST_AREA_Y - FOOTER_H - 4)
#define LIST_ROW_H       26
#define VISIBLE_ROWS     ((int)(LIST_AREA_H / LIST_ROW_H))
#define DETAILS_X        (MARGIN + COVER_THUMB_W + 10)

/* ── State ───────────────────────────────────────────────────────────────── */
typedef enum { SORT_NAME = 0, SORT_DL } SortMode;

static int         s_count      = 0;
static GameEntry  *s_list       = NULL;    /* pointer into catalog */
static int         s_cursor     = 0;
static int         s_scroll     = 0;
static SortMode    s_sort       = SORT_NAME;
static int         s_loading    = 0;       /* 1 while archive fetch in flight */

/* Cover texture ring-buffer */
static GSTEXTURE  *s_covers[MAX_COVER_CACHE]   = {0};
static int         s_cover_idx[MAX_COVER_CACHE] = {-1};  /* which game idx */

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void ensure_scroll(void)
{
    if (s_cursor < s_scroll)
        s_scroll = s_cursor;
    if (s_cursor >= s_scroll + VISIBLE_ROWS)
        s_scroll = s_cursor - VISIBLE_ROWS + 1;
}

static void fetch_cover_for(int game_idx)
{
    if (game_idx < 0 || game_idx >= s_count) return;
    /* Already cached? */
    for (int i = 0; i < MAX_COVER_CACHE; i++)
        if (s_cover_idx[i] == game_idx) return;

    /* Find least-recently-used slot (simple: use (game_idx % MAX_COVER_CACHE)) */
    int slot = game_idx % MAX_COVER_CACHE;
    if (!s_covers[slot])
        s_covers[slot] = ui_tex_alloc(COVER_THUMB_W, COVER_THUMB_H, GS_PSM_CT32);

    /* Attempt to load cover from catalog cache; kick async fetch if absent */
    const uint8_t *jpg  = NULL;
    int            jlen = 0;
    catalog_get_cover(s_list[game_idx].identifier, &jpg, &jlen);
    if (jpg && jlen > 0) {
        ui_tex_from_jpeg(s_covers[slot], jpg, jlen);
    }
    /* else: placeholder rendered in draw path */
    s_cover_idx[slot] = game_idx;
}

static GSTEXTURE *cover_tex(int game_idx)
{
    for (int i = 0; i < MAX_COVER_CACHE; i++)
        if (s_cover_idx[i] == game_idx) return s_covers[i];
    return NULL;
}

/* ── Screen callbacks ─────────────────────────────────────────────────────── */

void genre_screen_init(void)
{
    s_cursor  = g_state.selected_game;
    s_scroll  = g_state.list_scroll;
    s_loading = 1;

    /* Kick async archive.org search for this genre */
    archive_search_genre((Genre)g_state.selected_genre, 0 /* page */, 50);

    /* Pull whatever the catalog already has */
    s_list  = catalog_get_genre((Genre)g_state.selected_genre, &s_count);
    s_loading = 0;

    ensure_scroll();

    /* Pre-warm cover cache around cursor */
    for (int i = s_cursor - 2; i <= s_cursor + 2; i++)
        fetch_cover_for(i);
}

void genre_screen_update(void)
{
    /* Re-sync list in case archive fetch completed */
    s_list = catalog_get_genre((Genre)g_state.selected_genre, &s_count);
    if (s_count == 0) { s_cursor = 0; s_scroll = 0; }

    /* Navigation */
    int old = s_cursor;
    if (pad_pressed(PAD_UP)   && s_cursor > 0)          s_cursor--;
    if (pad_pressed(PAD_DOWN) && s_cursor < s_count - 1) s_cursor++;
    if (pad_pressed(PAD_L1))   s_cursor = (s_cursor - VISIBLE_ROWS < 0) ? 0 : s_cursor - VISIBLE_ROWS;
    if (pad_pressed(PAD_R1))   s_cursor = (s_cursor + VISIBLE_ROWS >= s_count) ? s_count - 1 : s_cursor + VISIBLE_ROWS;

    if (s_cursor != old) {
        ensure_scroll();
        /* Warm cover cache around new cursor */
        for (int i = s_cursor - 2; i <= s_cursor + 2; i++)
            fetch_cover_for(i);
    }

    /* Actions */
    if (pad_pressed(PAD_CROSS) && s_count > 0) {
        g_state.selected_game = s_cursor;
        app_switch_screen(SCREEN_DETAIL);
    }
    if (pad_pressed(PAD_CIRCLE)) {
        app_switch_screen(SCREEN_HOME);
    }
    if (pad_pressed(PAD_TRIANGLE) && s_count > 0) {
        /* Quick-enqueue download */
        catalog_enqueue_download(s_list[s_cursor].identifier);
        g_state.active_downloads++;
    }
    if (pad_pressed(PAD_SQUARE)) {
        s_sort = (s_sort == SORT_NAME) ? SORT_DL : SORT_NAME;
        catalog_sort_genre((Genre)g_state.selected_genre, s_sort == SORT_DL);
        s_list = catalog_get_genre((Genre)g_state.selected_genre, &s_count);
    }
}

void genre_screen_render(void)
{
    int net_ok = (g_state.net_status == NET_CONNECTED);
    Genre genre = (Genre)g_state.selected_genre;

    char header_title[64];
    snprintf(header_title, sizeof(header_title), "%s  (%d)",
             GENRE_NAMES[genre], s_count);
    ui_header(header_title, net_ok, g_state.active_downloads);

    float W = (float)SCREEN_W;

    /* ── Cover carousel ─────────────────────────────────────────── */
    /* Show 5 cover thumbs centred on the selected game */
    int carousel_items = 5;
    float cw = COVER_THUMB_W + 4;
    float total_cw = carousel_items * cw;
    float cx_start = (W - total_cw) * 0.5f;

    for (int k = 0; k < carousel_items; k++) {
        int gi    = s_cursor - 2 + k;    /* game index */
        float x   = cx_start + k * cw;
        float y   = (float)CAROUSEL_Y;
        int   sel = (k == 2);            /* centre slot is selected */

        if (gi < 0 || gi >= s_count) {
            /* Empty slot */
            ui_cover_placeholder(x, y, COVER_THUMB_W, COVER_THUMB_H, "");
            continue;
        }

        GSTEXTURE *tex = cover_tex(gi);
        if (tex && tex->Mem) {
            float scale = sel ? 1.0f : 0.85f;
            float sw = COVER_THUMB_W * scale;
            float sh = COVER_THUMB_H * scale;
            float ox = x + (COVER_THUMB_W - sw) * 0.5f;
            float oy = y + (COVER_THUMB_H - sh) * 0.5f;
            /* Shadow */
            ui_rect(ox + 3, oy + 3, sw, sh, RGBA(0,0,0,0x40));
            ui_sprite(tex, ox, oy, sw, sh, sel ? COL_WHITE : RGBA(0xA0,0xA0,0xA0,0x80));
        } else {
            ui_cover_placeholder(x, y, COVER_THUMB_W, COVER_THUMB_H,
                                 s_list[gi].title);
        }

        if (sel)
            ui_rect_outline(x - 1, y - 1, COVER_THUMB_W + 2,
                            COVER_THUMB_H + 2, COL_ACCENT, 2.0f);
    }

    /* ── Separator line ─────────────────────────────────────────── */
    float sep_y = (float)(CAROUSEL_Y + CAROUSEL_H + 2);
    ui_hline(MARGIN, sep_y, W - MARGIN * 2, COL_BORDER_DIM);

    /* ── Game list ──────────────────────────────────────────────── */
    float ly = (float)LIST_AREA_Y;
    float lw = W - MARGIN * 2 - 6;   /* leave 6 px for scrollbar */

    if (s_loading) {
        ui_spinner(W * 0.5f, ly + 40, 14, COL_ACCENT);
        ui_text_center(0, ly + 70, W, SCALE_NORMAL, COL_TEXT_DIM,
                       "Fetching from archive.org...");
    } else if (s_count == 0) {
        ui_text_center(0, ly + 60, W, SCALE_NORMAL, COL_TEXT_DIM,
                       "No games found for this genre.");
    } else {
        /* Column header */
        ui_text(MARGIN,           ly, SCALE_TINY, COL_ACCENT, "TITLE");
        ui_text_right(MARGIN + lw - 80, ly, SCALE_TINY, COL_ACCENT, "SIZE");
        ui_text_right(MARGIN + lw,      ly, SCALE_TINY, COL_ACCENT, "DLs");
        ly += 14;
        ui_hline(MARGIN, ly, lw, COL_BORDER_DIM);
        ly += 3;

        for (int i = 0; i < VISIBLE_ROWS && (s_scroll + i) < s_count; i++) {
            int   gi  = s_scroll + i;
            float ry  = ly + i * LIST_ROW_H;
            int   sel = (gi == s_cursor);

            if (sel) {
                ui_rect(MARGIN, ry, lw, LIST_ROW_H - 1, COL_SEL_BG);
                ui_rect_outline(MARGIN, ry, lw, LIST_ROW_H - 1,
                                COL_ACCENT_DIM, 1.0f);
            }

            uint64_t tc = sel ? COL_TEXT_TITLE : COL_TEXT;

            /* Title (truncated) */
            char title[48];
            strncpy(title, s_list[gi].title, 47);
            title[47] = '\0';
            ui_text(MARGIN + 4, ry + 5, SCALE_SMALL, tc, "%s", title);

            /* Size */
            char sz[16];
            if (s_list[gi].size_bytes > 0) {
                float mb = (float)s_list[gi].size_bytes / (1024.0f * 1024.0f);
                if (mb > 1024.0f)
                    snprintf(sz, sizeof(sz), "%.1fG", mb / 1024.0f);
                else
                    snprintf(sz, sizeof(sz), "%.0fM", mb);
            } else {
                snprintf(sz, sizeof(sz), "  ?  ");
            }
            ui_text_right(MARGIN + lw - 80, ry + 5, SCALE_TINY, COL_TEXT_DIM,
                          "%s", sz);

            /* Download count */
            if (s_list[gi].download_count > 0)
                ui_text_right(MARGIN + lw, ry + 5, SCALE_TINY, COL_TEXT_DIM,
                              "%d", s_list[gi].download_count);
        }

        /* Scrollbar */
        ui_scrollbar(W - MARGIN - 4, (float)LIST_AREA_Y,
                     (float)LIST_AREA_H, s_count, VISIBLE_ROWS, s_scroll);
    }

    /* ── Sort indicator ─────────────────────────────────────────── */
    ui_text_right(W - MARGIN, HEADER_H + 4, SCALE_TINY, COL_ACCENT_DIM,
                  "Sort: %s", s_sort == SORT_NAME ? "Name" : "Downloads");

    ui_footer("[X] Details  [\u25B3] Quick DL  [O] Back  [\u25A0] Sort",
              "[L1/R1] Page");
}

void genre_screen_destroy(void)
{
    g_state.selected_game = s_cursor;
    g_state.list_scroll   = s_scroll;
}
