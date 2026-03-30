/*
 * home_screen.c
 *
 * The "Launch" screen: 4×3 genre tile grid + animated accent lines.
 *
 * Layout (640×480):
 *
 *   ┌─────────────────────────────────────────────┐
 *   │  HEADER  (38 px)                            │
 *   ├─────────────────────────────────────────────┤
 *   │  [ALL]  [ACTION]  [RPG]  [SPORTS]   row 0   │
 *   │  [RACE] [SHOOT]  [FIGHT][ADVENT]    row 1   │
 *   │  [PUZ]  [SIM]   [STRAT][MUSIC]     row 2   │  ← GENRE_OTHER tucked at tail
 *   ├─────────────────────────────────────────────┤
 *   │  FOOTER  (30 px)                            │
 *   └─────────────────────────────────────────────┘
 *
 * Controller:
 *   D-pad     — move cursor
 *   Cross     — enter genre / All Games
 *   Triangle  — open Search
 *   Select    — open Settings
 *   Start     — open Download Manager
 */

#include <string.h>
#include <math.h>

#include "home_screen.h"
#include "ui.h"
#include "main.h"
#include "input/pad.h"
#include "catalog/genres.h"
#include "genre_screen.h"
#include "search_screen.h"
#include "download_screen.h"

/* ── Tile grid layout ─────────────────────────────────────────────────────── */
#define TILE_COLS    4
#define TILE_ROWS    4       /* (GENRE_COUNT - 1 = 14 genres) → ceil(14/4)=4  */
#define TILE_W       142
#define TILE_H        96
#define TILE_PAD      6

/* Derived: total grid width/height */
#define GRID_W  (TILE_COLS * (TILE_W + TILE_PAD) - TILE_PAD)
#define GRID_H  (TILE_ROWS * (TILE_H + TILE_PAD) - TILE_PAD)

static int s_cursor = 0;     /* flat index in GENRE_ALL … GENRE_OTHER */
static float s_anim = 0.0f;  /* selection pulse 0–1 */

/* Convert flat index to grid col/row */
static inline int tile_col(int idx) { return idx % TILE_COLS; }
static inline int tile_row(int idx) { return idx / TILE_COLS; }

/*  Pixel coordinates of a tile's top-left corner */
static void tile_pos(int idx, float *x, float *y)
{
    float gx = (SCREEN_W - GRID_W) * 0.5f;
    float gy = HEADER_H + ((SCREEN_H - HEADER_H - FOOTER_H - GRID_H) * 0.5f);
    *x = gx + tile_col(idx) * (TILE_W + TILE_PAD);
    *y = gy + tile_row(idx) * (TILE_H + TILE_PAD);
}

/* ── Screen callbacks ─────────────────────────────────────────────────────── */

void home_screen_init(void)
{
    s_cursor = 0;
    s_anim   = 0.0f;
}

void home_screen_update(void)
{
    int tiles = (int)(GENRE_COUNT - 1); /* skip the sentinel GENRE_COUNT */

    /* D-pad navigation */
    if (pad_pressed(PAD_LEFT))  { s_cursor = (s_cursor - 1 + tiles) % tiles; s_anim = 0.0f; }
    if (pad_pressed(PAD_RIGHT)) { s_cursor = (s_cursor + 1)          % tiles; s_anim = 0.0f; }
    if (pad_pressed(PAD_UP))    { s_cursor = (s_cursor - TILE_COLS + tiles) % tiles; s_anim = 0.0f; }
    if (pad_pressed(PAD_DOWN))  { s_cursor = (s_cursor + TILE_COLS) % tiles; s_anim = 0.0f; }

    /* Enter genre */
    if (pad_pressed(PAD_CROSS)) {
        g_state.selected_genre = s_cursor;   /* matches Genre enum (GENRE_ALL=0 …) */
        g_state.selected_game  = 0;
        g_state.list_scroll    = 0;
        app_switch_screen(SCREEN_GENRE);
    }

    /* Shortcuts */
    if (pad_pressed(PAD_TRIANGLE)) app_switch_screen(SCREEN_SEARCH);
    if (pad_pressed(PAD_START))    app_switch_screen(SCREEN_DOWNLOAD);

    /* Animate selection pulse */
    s_anim += 0.05f;
    if (s_anim > 1.0f) s_anim -= 1.0f;
}

void home_screen_render(void)
{
    int net_ok = (g_state.net_status == NET_CONNECTED);
    ui_header("Select Genre", net_ok, g_state.active_downloads);

    int tiles = (int)(GENRE_COUNT - 1);

    for (int i = 0; i < tiles; i++) {
        float tx, ty;
        tile_pos(i, &tx, &ty);

        Genre genre = (Genre)i;   /* GENRE_ALL = 0 */
        int sel = (i == s_cursor);

        /* ── Background ─────────────────────────────── */
        GenreColour gc  = GENRE_COLOURS[genre];
        uint8_t     dim = sel ? 0x80 : 0x50;
        uint64_t    bg  = RGBA(gc.r * dim / 0x80,
                               gc.g * dim / 0x80,
                               gc.b * dim / 0x80, 0x80);
        uint64_t    bg2 = RGBA(gc.r / 4, gc.g / 4, gc.b / 4, 0x80);

        ui_gradient_rect(tx, ty, TILE_W, TILE_H, bg, bg, bg2, bg2);

        /* ── Selection glow border ──────────────────── */
        if (sel) {
            float pulse = 0.6f + 0.4f * sinf(s_anim * 6.28f);
            uint8_t ba  = (uint8_t)(0x80 * pulse);
            uint64_t bc = RGBA(gc.r, gc.g, gc.b, ba);
            ui_rect_outline(tx - 1, ty - 1, TILE_W + 2, TILE_H + 2, bc, 2.0f);
        } else {
            ui_rect_outline(tx, ty, TILE_W, TILE_H, COL_BORDER_DIM, 1.0f);
        }

        /* ── Genre name centred ─────────────────────── */
        float scale = sel ? SCALE_MEDIUM : SCALE_NORMAL;
        uint64_t tc = sel ? COL_WHITE : COL_TEXT;
        ui_text_center(tx, ty + (TILE_H - ui_text_h(scale)) * 0.5f,
                       TILE_W, scale, tc, "%s", GENRE_NAMES[genre]);
    }

    /* ── Status bar / hint ──────────────────────────── */
    ui_footer("[X] Enter  [\u25B3] Search  [Start] Downloads",
              "v" APP_VERSION);
}

void home_screen_destroy(void) { /* nothing to release */ }
