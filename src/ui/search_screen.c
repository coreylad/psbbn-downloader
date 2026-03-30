/*
 * search_screen.c
 *
 * Virtual keyboard + live search results from archive.org.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  HEADER                                                  │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  ┌────────────────────────────────────────────────────┐  │
 *   │  │  QUERY: [Grand Theft Auto_                      ]  │  │
 *   │  └────────────────────────────────────────────────────┘  │
 *   │                                                          │
 *   │  ┌──── Virtual keyboard — 10×4 grid ──────────────────┐  │
 *   │  │  1 2 3 4 5 6 7 8 9 0 ← (backspace)                 │  │
 *   │  │  A B C D E F G H I J                               │  │
 *   │  │  K L M N O P Q R S T                               │  │
 *   │  │  U V W X Y Z . - : SPACE                           │  │
 *   │  └────────────────────────────────────────────────────┘  │
 *   │                                                          │
 *   │  ── Results ──────────────────────────────────────────   │
 *   │   Game Title A           Action    652 MB                │
 *   │ ▶ Game Title B           RPG      4.2 GB                 │
 *   │   Game Title C           Shooter  1.1 GB                 │
 *   └──────────────────────────────────────────────────────────┘
 *
 * Focus modes:
 *   MODE_KEYBOARD — d-pad navigates keys, X types, Triangle → submit
 *   MODE_RESULTS  — d-pad navigates results, X opens detail, Triangle → KB
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "search_screen.h"
#include "detail_screen.h"
#include "ui.h"
#include "main.h"
#include "input/pad.h"
#include "catalog/catalog.h"
#include "net/archive.h"

/* ── Virtual keyboard layout ─────────────────────────────────────────────── */
#define KB_COLS  11
#define KB_ROWS   4
#define KEY_W     46
#define KEY_H     28
#define KEY_PAD    4

static const char * const KB_KEYS[KB_ROWS][KB_COLS] = {
    { "1","2","3","4","5","6","7","8","9","0","\x7F" },   /* ← = backspace */
    { "A","B","C","D","E","F","G","H","I","J","K"   },
    { "L","M","N","O","P","Q","R","S","T","U","V"   },
    { "W","X","Y","Z",".","-",":","_","!","?","SP"  },
};

/* Pixel position of keyboard grid origin */
#define KB_OX   ((SCREEN_W - KB_COLS * (KEY_W + KEY_PAD)) / 2)
#define KB_OY   85

/* ── Focus mode ───────────────────────────────────────────────────────────── */
typedef enum { FOCUS_KEYBOARD = 0, FOCUS_RESULTS } FocusMode;

static FocusMode s_focus;
static int  s_kb_col = 0, s_kb_row = 0;   /* keyboard cursor */
static int  s_res_cursor = 0;              /* result list cursor */
static int  s_res_scroll = 0;
static int  s_res_count  = 0;
static GameEntry s_results[MAX_SEARCH_RESULTS];
static int  s_searching = 0;

#define RESULTS_Y  (KB_OY + KB_ROWS * (KEY_H + KEY_PAD) + 20)
#define RESULT_ROW_H  22
#define VISIBLE_RES   ((SCREEN_H - RESULTS_Y - FOOTER_H - 4) / RESULT_ROW_H)

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void type_key(int row, int col)
{
    const char *k = KB_KEYS[row][col];
    int len = (int)strlen(g_state.search_query);

    if (k[0] == '\x7F') {   /* backspace */
        if (len > 0) {
            g_state.search_query[len - 1] = '\0';
            g_state.search_cursor = len - 1;
        }
        return;
    }

    if (strcmp(k, "SP") == 0) {
        if (len < (int)(sizeof(g_state.search_query) - 1)) {
            g_state.search_query[len]     = ' ';
            g_state.search_query[len + 1] = '\0';
            g_state.search_cursor = len + 1;
        }
        return;
    }

    /* Normal character (single ASCII) */
    if (len < (int)(sizeof(g_state.search_query) - 1)) {
        g_state.search_query[len]     = k[0];
        g_state.search_query[len + 1] = '\0';
        g_state.search_cursor = len + 1;
    }
}

static void do_search(void)
{
    if (g_state.search_query[0] == '\0') return;
    s_searching  = 1;
    s_res_count  = 0;
    s_res_cursor = 0;
    s_res_scroll = 0;
    archive_search_text(g_state.search_query, 0, MAX_SEARCH_RESULTS);
}

static void scroll_results(void)
{
    if (s_res_cursor < s_res_scroll)
        s_res_scroll = s_res_cursor;
    if (s_res_cursor >= s_res_scroll + VISIBLE_RES)
        s_res_scroll = s_res_cursor - VISIBLE_RES + 1;
}

/* ── Screen callbacks ─────────────────────────────────────────────────────── */

void search_screen_init(void)
{
    s_focus    = FOCUS_KEYBOARD;
    s_kb_col   = 0;
    s_kb_row   = 0;
    s_searching = 0;
    /* Don't clear the query — user may have come back from detail */
}

void search_screen_update(void)
{
    /* Sync results if archive fetch completed */
    if (s_searching) {
        int n = 0;
        const GameEntry *r = archive_get_results(&n);
        if (n > 0 || !archive_is_fetching()) {
            if (n > MAX_SEARCH_RESULTS) n = MAX_SEARCH_RESULTS;
            for (int i = 0; i < n; i++) s_results[i] = r[i];
            s_res_count = n;
            s_searching = 0;
        }
    }

    if (s_focus == FOCUS_KEYBOARD) {
        /* D-pad moves keyboard cursor */
        if (pad_pressed(PAD_UP)    && s_kb_row > 0)            s_kb_row--;
        if (pad_pressed(PAD_DOWN)  && s_kb_row < KB_ROWS - 1)  s_kb_row++;
        if (pad_pressed(PAD_LEFT)  && s_kb_col > 0)            s_kb_col--;
        if (pad_pressed(PAD_RIGHT) && s_kb_col < KB_COLS - 1)  s_kb_col++;

        /* X — type key */
        if (pad_pressed(PAD_CROSS))
            type_key(s_kb_row, s_kb_col);

        /* Triangle — submit search */
        if (pad_pressed(PAD_TRIANGLE))
            do_search();

        /* R1 — switch focus to results (if any) */
        if (pad_pressed(PAD_R1) && s_res_count > 0)
            s_focus = FOCUS_RESULTS;

        /* Circle — back to genre screen / home */
        if (pad_pressed(PAD_CIRCLE))
            app_switch_screen(g_state.prev_screen);

    } else {  /* FOCUS_RESULTS */
        if (pad_pressed(PAD_UP)   && s_res_cursor > 0)             { s_res_cursor--; scroll_results(); }
        if (pad_pressed(PAD_DOWN) && s_res_cursor < s_res_count-1) { s_res_cursor++; scroll_results(); }

        /* X — open detail */
        if (pad_pressed(PAD_CROSS) && s_res_count > 0) {
            /* Inject result into catalog so detail screen can find it */
            catalog_inject_entry(&s_results[s_res_cursor]);
            g_state.selected_genre = (int)s_results[s_res_cursor].genre;
            g_state.selected_game  = catalog_find_by_id(s_results[s_res_cursor].identifier);
            app_switch_screen(SCREEN_DETAIL);
        }

        /* Triangle — quick-download */
        if (pad_pressed(PAD_TRIANGLE) && s_res_count > 0) {
            catalog_enqueue_download(s_results[s_res_cursor].identifier);
            g_state.active_downloads++;
        }

        /* L1 — switch focus back to keyboard */
        if (pad_pressed(PAD_L1))
            s_focus = FOCUS_KEYBOARD;

        if (pad_pressed(PAD_CIRCLE))
            app_switch_screen(g_state.prev_screen);
    }
}

void search_screen_render(void)
{
    int net_ok = (g_state.net_status == NET_CONNECTED);
    ui_header("Search Games", net_ok, g_state.active_downloads);

    float W = (float)SCREEN_W;

    /* ── Query box ────────────────────────────────────────────── */
    float qx = (float)MARGIN;
    float qy = (float)(HEADER_H + 8);
    float qw = W - MARGIN * 2;
    float qh = 26;

    ui_rect(qx, qy, qw, qh, COL_PANEL);
    uint64_t qborder = (s_focus == FOCUS_KEYBOARD) ? COL_ACCENT : COL_BORDER_DIM;
    ui_rect_outline(qx, qy, qw, qh, qborder, 1.5f);

    /* Query text + blinking cursor */
    char disp[132];
    snprintf(disp, sizeof(disp), "%s", g_state.search_query);
    int blink = (g_ui.frame >> 4) & 1;
    if (blink && s_focus == FOCUS_KEYBOARD)
        strncat(disp, "_", sizeof(disp) - strlen(disp) - 1);
    ui_text(qx + 6, qy + 5, SCALE_SMALL, COL_TEXT_TITLE, "%s", disp);

    if (!g_state.search_query[0])
        ui_text(qx + 6, qy + 5, SCALE_SMALL, COL_TEXT_DIM,
                "Type to search archive.org...");

    /* ── Virtual keyboard grid ────────────────────────────────── */
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            float kx = (float)(KB_OX + c * (KEY_W + KEY_PAD));
            float ky = (float)(KB_OY + r * (KEY_H + KEY_PAD));
            int   sel = (s_kb_row == r && s_kb_col == c &&
                         s_focus == FOCUS_KEYBOARD);

            uint64_t bg = sel ? COL_HIGHLIGHT : COL_PANEL;
            ui_rect(kx, ky, KEY_W, KEY_H, bg);
            uint64_t border = sel ? COL_ACCENT : COL_BORDER_DIM;
            ui_rect_outline(kx, ky, KEY_W, KEY_H, border, sel ? 1.5f : 1.0f);

            const char *label = KB_KEYS[r][c];
            uint64_t tc = sel ? COL_WHITE : COL_TEXT;
            if (label[0] == '\x7F') label = "<-";   /* render backspace */
            ui_text_center(kx, ky + (KEY_H - ui_text_h(SCALE_SMALL)) * 0.5f,
                           KEY_W, SCALE_SMALL, tc, "%s", label);
        }
    }

    /* ── Results list ─────────────────────────────────────────── */
    float ry = (float)RESULTS_Y;
    float lw = W - MARGIN * 2 - 6;

    if (s_searching) {
        ui_spinner(W * 0.5f, ry + 20, 12, COL_ACCENT);
        ui_text_center(0, ry + 44, W, SCALE_SMALL, COL_TEXT_DIM,
                       "Searching...");
    } else if (s_res_count == 0 && g_state.search_query[0]) {
        ui_text_center(0, ry + 20, W, SCALE_SMALL, COL_TEXT_DIM,
                       "No results. Press [\u25B3] to search.");
    } else {
        /* Focus indicator */
        uint64_t sec_border = (s_focus == FOCUS_RESULTS) ? COL_ACCENT : COL_BORDER_DIM;
        ui_hline(MARGIN, ry - 4, lw, sec_border);

        for (int i = 0; i < VISIBLE_RES && (s_res_scroll + i) < s_res_count; i++) {
            int    gi  = s_res_scroll + i;
            float  row_y = ry + i * RESULT_ROW_H;
            int    sel  = (gi == s_res_cursor && s_focus == FOCUS_RESULTS);

            if (sel) {
                ui_rect(MARGIN, row_y, lw, RESULT_ROW_H - 1, COL_SEL_BG);
                ui_rect_outline(MARGIN, row_y, lw, RESULT_ROW_H - 1,
                                COL_ACCENT_DIM, 1.0f);
            }

            uint64_t tc = sel ? COL_TEXT_TITLE : COL_TEXT;
            char title[40];
            strncpy(title, s_results[gi].title, 39);
            title[39] = '\0';
            ui_text(MARGIN + 4, row_y + 4, SCALE_SMALL, tc, "%s", title);

            /* Genre badge */
            Genre  g   = s_results[gi].genre;
            GenreColour gc = GENRE_COLOURS[g];
            ui_badge(lw - 60, row_y + 4, GENRE_NAMES[g],
                     RGB(gc.r/2, gc.g/2, gc.b/2), COL_TEXT_DIM);

            /* Size */
            if (s_results[gi].size_bytes > 0) {
                float mb = (float)s_results[gi].size_bytes /
                           (1024.0f * 1024.0f);
                ui_text_right(W - MARGIN, row_y + 4, SCALE_TINY,
                              COL_TEXT_DIM,
                              mb > 1024.0f ? "%.1fG" : "%.0fM",
                              mb > 1024.0f ? mb / 1024.0f : mb);
            }
        }

        /* Scrollbar */
        ui_scrollbar(W - MARGIN - 4, ry, (float)(VISIBLE_RES * RESULT_ROW_H),
                     s_res_count, VISIBLE_RES, s_res_scroll);
    }

    const char *l_hint = (s_focus == FOCUS_KEYBOARD)
        ? "[X] Type  [\u25B3] Search  [R1] Results  [O] Back"
        : "[X] Detail  [\u25B3] DL  [L1] Keyboard  [O] Back";
    ui_footer(l_hint, NULL);
}

void search_screen_destroy(void) { /* query string preserved in g_state */ }
