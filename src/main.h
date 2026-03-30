#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <kernel.h>

/* ── App metadata ─────────────────────────────────────────────────────────── */
#define APP_NAME        "PSBBN Downloader"
#define APP_VERSION     "1.1.0"
#define APP_AUTHOR      "coreylad"

/* ── Display ─────────────────────────────────────────────────────────────── */
#define SCREEN_W        640
#define SCREEN_H        480

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define MAX_GAMES_PER_PAGE   12   /* lines visible in game list              */
#define MAX_SEARCH_RESULTS   50   /* max archive.org results per query       */
#define MAX_COVER_CACHE       8   /* simultaneous cover textures in VRAM     */
#define MAX_DL_QUEUE         10   /* max queued downloads                    */
#define TITLE_MAX           128
#define IDENT_MAX            64
#define DESC_MAX            512
#define URL_MAX             256

/* ── Screen IDs ──────────────────────────────────────────────────────────── */
typedef enum {
    SCREEN_HOME = 0,   /* genre grid + featured art                         */
    SCREEN_GENRE,      /* game list filtered by genre                       */
    SCREEN_DETAIL,     /* full game info + download button                  */
    SCREEN_DOWNLOAD,   /* download queue + progress bars                    */
    SCREEN_SEARCH,     /* virtual keyboard + live results                   */
    SCREEN_SETTINGS,   /* network config, storage path, video mode          */
    SCREEN_COUNT
} AppScreen;

/* ── Network status ─────────────────────────────────────────────────────── */
typedef enum {
    NET_DISCONNECTED = 0,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_ERROR
} NetStatus;

/* ── Global application state ───────────────────────────────────────────── */
typedef struct {
    AppScreen   screen;          /* currently visible screen                */
    AppScreen   prev_screen;     /* for back-navigation                     */
    int         running;         /* main loop flag                          */
    NetStatus   net_status;
    char        net_ip[16];      /* dotted-decimal IP string                */

    /* Navigation context */
    int         selected_genre;  /* genre index, -1 = All Games             */
    int         selected_game;   /* index within current genre list         */
    int         list_scroll;     /* top-of-list scroll offset               */

    /* Search state */
    char        search_query[128];
    int         search_cursor;   /* cursor position in query string         */

    /* Download queue counter (read by header widget) */
    int         active_downloads;
} AppState;

extern AppState g_state;

/* ── Screen switcher ─────────────────────────────────────────────────────── */
void app_switch_screen(AppScreen screen);

#endif /* MAIN_H */
