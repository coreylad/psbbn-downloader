/*
 * download_screen.c
 *
 * Download queue manager — shows active + queued + completed transfers
 * with real-time progress bars and speed readouts.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  HEADER — "Download Manager"                             │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  [#]  Title                 [========     ] 64%  1.2MB/s │
 *   │  [#]  Title                 [=====        ] 38%   800KB/s│
 *   │  [Q]  Title                 Queued                       │
 *   │  [Q]  Title                 Queued                       │
 *   │  [✓]  Title                 Complete  •  Jul 2026        │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  Storage: mass:/PS2ISO       Free: 123.4 GB              │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  FOOTER                                                  │
 *   └──────────────────────────────────────────────────────────┘
 *
 * Controller:
 *   Up/Down  — select entry
 *   X        — pause / resume active download
 *   Square   — cancel / remove selected
 *   Circle   — back
 *   Triangle — clear completed entries
 */

#include <stdio.h>
#include <string.h>

#include "download_screen.h"
#include "ui.h"
#include "main.h"
#include "input/pad.h"
#include "catalog/catalog.h"
#include "net/archive.h"

#define ROW_H  36
#define BAR_W  180
#define BAR_H    8

static int s_cursor  = 0;

/* ── Screen callbacks ─────────────────────────────────────────────────────── */

void download_screen_init(void)
{
    s_cursor = 0;
}

void download_screen_update(void)
{
    int count = catalog_dl_count();
    if (count > 0) {
        if (pad_pressed(PAD_UP)   && s_cursor > 0)         s_cursor--;
        if (pad_pressed(PAD_DOWN) && s_cursor < count - 1) s_cursor++;
    }

    if (pad_pressed(PAD_CROSS) && count > 0) {
        catalog_dl_toggle_pause(s_cursor);
    }
    if (pad_pressed(PAD_SQUARE) && count > 0) {
        catalog_dl_cancel(s_cursor);
        if (s_cursor >= catalog_dl_count() && s_cursor > 0)
            s_cursor--;
        g_state.active_downloads = catalog_dl_active_count();
    }
    if (pad_pressed(PAD_TRIANGLE)) {
        catalog_dl_clear_completed();
        if (s_cursor >= catalog_dl_count() && s_cursor > 0)
            s_cursor--;
    }
    if (pad_pressed(PAD_CIRCLE)) {
        app_switch_screen(g_state.prev_screen);
    }

    /* Keep active count in sync */
    g_state.active_downloads = catalog_dl_active_count();
}

void download_screen_render(void)
{
    int net_ok = (g_state.net_status == NET_CONNECTED);
    ui_header("Download Manager", net_ok, g_state.active_downloads);

    float W  = (float)SCREEN_W;
    float y  = (float)(HEADER_H + 10);
    int count = catalog_dl_count();

    if (count == 0) {
        ui_text_center(0, y + 80, W, SCALE_NORMAL, COL_TEXT_DIM,
                       "No downloads queued.");
        ui_text_center(0, y + 108, W, SCALE_SMALL, COL_TEXT_DIM,
                       "Browse a genre and press [\u25B3] to queue a game.");
    }

    for (int i = 0; i < count; i++) {
        DownloadEntry *de = catalog_dl_get(i);
        if (!de) continue;

        float ry  = y + i * ROW_H;
        int   sel = (i == s_cursor);

        /* Row background */
        if (sel) {
            ui_rect(MARGIN, ry, W - MARGIN * 2, ROW_H - 2, COL_SEL_BG);
            ui_rect_outline(MARGIN, ry, W - MARGIN * 2, ROW_H - 2,
                            COL_ACCENT_DIM, 1.0f);
        }

        /* Status badge (left column) */
        const char *badge_str;
        uint64_t    badge_col;
        switch (de->status) {
            case DL_STATUS_ACTIVE:
                badge_str = "DL";  badge_col = COL_GREEN;   break;
            case DL_STATUS_PAUSED:
                badge_str = "||";  badge_col = COL_YELLOW;  break;
            case DL_STATUS_QUEUED:
                badge_str = " Q";  badge_col = COL_ACCENT;  break;
            case DL_STATUS_DONE:
                badge_str = "\u2713"; badge_col = RGB(0x30,0xA0,0x40); break;
            case DL_STATUS_ERROR:
                badge_str = "!";   badge_col = COL_RED;     break;
            default:
                badge_str = "?";   badge_col = COL_TEXT_DIM; break;
        }
        ui_badge(MARGIN + 2, ry + 8, badge_str, badge_col, COL_WHITE);

        /* Title */
        char title[42];
        strncpy(title, de->title, 41);
        title[41] = '\0';
        ui_text(MARGIN + 28, ry + 7, SCALE_SMALL,
                sel ? COL_TEXT_TITLE : COL_TEXT, "%s", title);

        /* Progress bar or status text (right side) */
        float bar_x = W - MARGIN - BAR_W - 80;
        if (de->status == DL_STATUS_ACTIVE || de->status == DL_STATUS_PAUSED) {
            float pct = (de->total_bytes > 0)
                        ? (float)de->recv_bytes / (float)de->total_bytes
                        : 0.0f;
            ui_progress_bar(bar_x, ry + 12, BAR_W, BAR_H,
                            pct, COL_PANEL_DARK, COL_GREEN);
            ui_text(bar_x, ry + 22, SCALE_TINY, COL_TEXT_DIM,
                    "%.0f%%", pct * 100.0f);

            /* Speed */
            char spd[20];
            if (de->bytes_per_sec > 1024 * 1024)
                snprintf(spd, sizeof(spd), "%.1f MB/s",
                         de->bytes_per_sec / (1024.0f * 1024.0f));
            else
                snprintf(spd, sizeof(spd), "%d KB/s",
                         (int)(de->bytes_per_sec / 1024));
            ui_text_right(W - MARGIN, ry + 7, SCALE_TINY, COL_TEXT_DIM,
                          "%s", spd);
        } else if (de->status == DL_STATUS_DONE) {
            ui_text_right(W - MARGIN, ry + 7, SCALE_TINY,
                          RGB(0x30, 0xA0, 0x40), "Complete");
        } else if (de->status == DL_STATUS_ERROR) {
            ui_text_right(W - MARGIN, ry + 7, SCALE_TINY, COL_RED,
                          "Error");
        } else {
            ui_text_right(W - MARGIN, ry + 7, SCALE_TINY, COL_TEXT_DIM,
                          "Queued");
        }
    }

    /* ── Storage info ─────────────────────────────────────────── */
    float info_y = (float)(SCREEN_H - FOOTER_H - 30);
    ui_hline(MARGIN, info_y, W - MARGIN * 2, COL_BORDER_DIM);
    info_y += 6;

    StorageInfo si;
    catalog_get_storage_info(&si);
    ui_text(MARGIN, info_y, SCALE_TINY, COL_TEXT_DIM,
            "Storage: %s", si.path);
    if (si.free_bytes > 0) {
        float gb = (float)si.free_bytes / (1024.0f * 1024.0f * 1024.0f);
        ui_text_right(W - MARGIN, info_y, SCALE_TINY, COL_TEXT_DIM,
                      "Free: %.1f GB", gb);
    }

    ui_footer("[X] Pause/Resume  [\u25A0] Cancel  [\u25B3] Clear Done  [O] Back",
              NULL);
}

void download_screen_destroy(void) { /* nothing */ }
