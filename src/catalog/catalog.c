/*
 * catalog.c — in-memory game catalog + download queue manager
 *
 * All data lives in static arrays.  PS2 has ~32 MB EE RAM; we reserve
 * a modest fixed pool here (expandable via config).
 *
 * The download worker pops entries from the queue, calls
 * archive_download_iso(), and updates status/progress.
 * Because PS2 EE threading is cooperative, the main loop must call
 * catalog_tick() each frame to advance downloads.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <kernel.h>
#include <fileXio_rpc.h>
#include <sys/stat.h>

#include "catalog.h"
#include "genres.h"
#include "../util/config.h"
#include "../util/log.h"
#include "../net/archive.h"

/* ── Capacity ─────────────────────────────────────────────────────────────── */
#define CATALOG_MAX     512     /* total games across all genres              */
#define COVER_CACHE_MAX  16     /* JPEG cover art cached in heap              */

/* ── Game storage ─────────────────────────────────────────────────────────── */
static GameEntry s_games[CATALOG_MAX];
static int       s_game_count = 0;

/* ── Cover cache ─────────────────────────────────────────────────────────── */
typedef struct {
    char     identifier[IDENT_MAX];
    uint8_t *jpg;
    int      len;
} CoverEntry;

static CoverEntry s_covers[COVER_CACHE_MAX];
static int        s_cover_count = 0;

/* ── Download queue ──────────────────────────────────────────────────────── */
static DownloadEntry s_dl[MAX_DL_QUEUE];
static int           s_dl_count = 0;

/* ── Scratch view arrays (one per genre) ─────────────────────────────────── */
/* Rather than allocating per-call, we keep a single view buffer that is      */
/* rebuilt whenever the caller switches genre.                                 */
static GameEntry *s_view[CATALOG_MAX];
static int        s_view_count  = 0;
static Genre      s_view_genre  = (Genre)-1;
static int        s_view_sorted_dl = 0;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void catalog_init(void)
{
    memset(s_games,  0, sizeof(s_games));
    memset(s_covers, 0, sizeof(s_covers));
    memset(s_dl,     0, sizeof(s_dl));
    s_game_count = 0;
    s_cover_count = 0;
    s_dl_count    = 0;
    s_view_genre  = (Genre)-1;

    fileXioInit();
    LOGI("Catalog initialised");
}

void catalog_shutdown(void)
{
    /* Free cover art heap */
    for (int i = 0; i < s_cover_count; i++) {
        if (s_covers[i].jpg) {
            free(s_covers[i].jpg);
            s_covers[i].jpg = NULL;
        }
    }
    fileXioExit();
}

/* ── Genre helpers ──────────────────────────────────────────────────────────*/

Genre genre_from_subject(const char *subjects)
{
    if (!subjects || !subjects[0]) return GENRE_OTHER;

    /* Simple keyword scan — subjects is a space/comma separated string */
    struct { const char *kw; Genre g; } map[] = {
        {"action",      GENRE_ACTION},
        {"role-play",   GENRE_RPG},
        {"rpg",         GENRE_RPG},
        {"sport",       GENRE_SPORTS},
        {"racing",      GENRE_RACING},
        {"race",        GENRE_RACING},
        {"shooter",     GENRE_SHOOTER},
        {"shoot",       GENRE_SHOOTER},
        {"fighting",    GENRE_FIGHTING},
        {"fight",       GENRE_FIGHTING},
        {"adventure",   GENRE_ADVENTURE},
        {"puzzle",      GENRE_PUZZLE},
        {"simulation",  GENRE_SIMULATION},
        {"strategy",    GENRE_STRATEGY},
        {"horror",      GENRE_HORROR},
        {"platform",    GENRE_PLATFORM},
        {"music",       GENRE_MUSIC},
        {"rhythm",      GENRE_MUSIC},
        {NULL, GENRE_OTHER}
    };

    /* Lower-case comparison */
    char lower[256];
    strncpy(lower, subjects, sizeof(lower) - 1);
    lower[sizeof(lower)-1] = '\0';
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    for (int i = 0; map[i].kw; i++)
        if (strstr(lower, map[i].kw))
            return map[i].g;

    return GENRE_OTHER;
}

/* ── Game list ─────────────────────────────────────────────────────────────*/

void catalog_inject_entry(const GameEntry *entry)
{
    if (!entry || !entry->identifier[0]) return;

    /* Upsert by identifier */
    for (int i = 0; i < s_game_count; i++) {
        if (strcmp(s_games[i].identifier, entry->identifier) == 0) {
            memcpy(&s_games[i], entry, sizeof(GameEntry));
            s_view_genre = (Genre)-1;   /* invalidate view cache */
            return;
        }
    }

    if (s_game_count < CATALOG_MAX) {
        memcpy(&s_games[s_game_count++], entry, sizeof(GameEntry));
        s_view_genre = (Genre)-1;
    } else {
        LOGW("catalog full (%d entries)", CATALOG_MAX);
    }
}

static int cmp_by_name(const void *a, const void *b)
{
    return strcmp((*(const GameEntry **)a)->title,
                  (*(const GameEntry **)b)->title);
}

static int cmp_by_dl(const void *a, const void *b)
{
    return (*(const GameEntry **)b)->download_count
         - (*(const GameEntry **)a)->download_count;
}

static void rebuild_view(Genre genre)
{
    s_view_count = 0;
    for (int i = 0; i < s_game_count; i++) {
        if (genre == GENRE_ALL || s_games[i].genre == genre)
            s_view[s_view_count++] = &s_games[i];
    }
    s_view_genre = genre;
}

GameEntry *catalog_get_genre(Genre genre, int *n_out)
{
    if (s_view_genre != genre)
        rebuild_view(genre);

    if (n_out) *n_out = s_view_count;

    /* Return a flat array — copy pointers' targets into a static result buf */
    static GameEntry flat[CATALOG_MAX];
    for (int i = 0; i < s_view_count; i++)
        flat[i] = *s_view[i];
    return flat;
}

void catalog_sort_genre(Genre genre, int by_downloads)
{
    if (s_view_genre != genre) rebuild_view(genre);
    qsort(s_view, (size_t)s_view_count, sizeof(GameEntry *),
          by_downloads ? cmp_by_dl : cmp_by_name);
    s_view_sorted_dl = by_downloads;
}

int catalog_find_by_id(const char *identifier)
{
    for (int i = 0; i < s_view_count; i++)
        if (strcmp(s_view[i]->identifier, identifier) == 0)
            return i;
    return -1;
}

/* ── Cover art ─────────────────────────────────────────────────────────────*/

void catalog_store_cover(const char *identifier,
                         const uint8_t *jpg, int len)
{
    if (!identifier || !jpg || len <= 0) return;

    /* Overwrite if already cached */
    for (int i = 0; i < s_cover_count; i++) {
        if (strcmp(s_covers[i].identifier, identifier) == 0) {
            free(s_covers[i].jpg);
            s_covers[i].jpg = (uint8_t *)malloc((size_t)len);
            if (s_covers[i].jpg) {
                memcpy(s_covers[i].jpg, jpg, (size_t)len);
                s_covers[i].len = len;
            }
            return;
        }
    }

    if (s_cover_count >= COVER_CACHE_MAX) {
        /* Evict oldest (index 0) */
        free(s_covers[0].jpg);
        memmove(&s_covers[0], &s_covers[1],
                (size_t)(s_cover_count - 1) * sizeof(CoverEntry));
        s_cover_count--;
    }

    CoverEntry *ce = &s_covers[s_cover_count++];
    strncpy(ce->identifier, identifier, IDENT_MAX - 1);
    ce->jpg = (uint8_t *)malloc((size_t)len);
    if (ce->jpg) {
        memcpy(ce->jpg, jpg, (size_t)len);
        ce->len = len;
    } else {
        s_cover_count--;
    }
}

int catalog_get_cover(const char *identifier,
                      const uint8_t **jpg_out, int *len_out)
{
    for (int i = 0; i < s_cover_count; i++) {
        if (strcmp(s_covers[i].identifier, identifier) == 0) {
            if (jpg_out) *jpg_out = s_covers[i].jpg;
            if (len_out) *len_out = s_covers[i].len;
            return 1;
        }
    }
    /* Cache miss — kick async fetch */
    archive_fetch_cover(identifier);
    if (jpg_out) *jpg_out = NULL;
    if (len_out) *len_out = 0;
    return 0;
}

/* ── Download queue ────────────────────────────────────────────────────────*/

void catalog_enqueue_download(const char *identifier)
{
    if (!identifier || s_dl_count >= MAX_DL_QUEUE) return;

    /* Avoid duplicates */
    for (int i = 0; i < s_dl_count; i++)
        if (strcmp(s_dl[i].identifier, identifier) == 0) return;

    DownloadEntry *de = &s_dl[s_dl_count++];
    memset(de, 0, sizeof(*de));
    strncpy(de->identifier, identifier, IDENT_MAX - 1);

    /* Look up title from catalog */
    for (int i = 0; i < s_game_count; i++) {
        if (strcmp(s_games[i].identifier, identifier) == 0) {
            strncpy(de->title, s_games[i].title, TITLE_MAX - 1);
            de->total_bytes = s_games[i].size_bytes;
            break;
        }
    }
    if (!de->title[0]) strncpy(de->title, identifier, TITLE_MAX - 1);

    /* Build destination path */
    AppConfig *cfg = config_get();
    snprintf(de->dest_path, URL_MAX - 1, "%s/%s.iso",
             cfg->storage_path, identifier);

    de->status = DL_STATUS_QUEUED;
    LOGI("Enqueued: %s", de->title);
}

int catalog_dl_count(void)       { return s_dl_count; }
DownloadEntry *catalog_dl_get(int idx)
{
    return (idx >= 0 && idx < s_dl_count) ? &s_dl[idx] : NULL;
}

int catalog_dl_active_count(void)
{
    int n = 0;
    for (int i = 0; i < s_dl_count; i++)
        if (s_dl[i].status == DL_STATUS_ACTIVE ||
            s_dl[i].status == DL_STATUS_QUEUED) n++;
    return n;
}

void catalog_dl_toggle_pause(int idx)
{
    if (idx < 0 || idx >= s_dl_count) return;
    DownloadEntry *de = &s_dl[idx];
    if (de->status == DL_STATUS_ACTIVE)  de->status = DL_STATUS_PAUSED;
    else if (de->status == DL_STATUS_PAUSED) de->status = DL_STATUS_ACTIVE;
}

void catalog_dl_cancel(int idx)
{
    if (idx < 0 || idx >= s_dl_count) return;
    /* Shift array down */
    memmove(&s_dl[idx], &s_dl[idx + 1],
            (size_t)(s_dl_count - idx - 1) * sizeof(DownloadEntry));
    s_dl_count--;
}

void catalog_dl_clear_completed(void)
{
    int w = 0;
    for (int r = 0; r < s_dl_count; r++) {
        if (s_dl[r].status != DL_STATUS_DONE)
            s_dl[w++] = s_dl[r];
    }
    s_dl_count = w;
}

/* ── Storage info ──────────────────────────────────────────────────────────*/

void catalog_get_storage_info(StorageInfo *out)
{
    if (!out) return;
    AppConfig *cfg = config_get();
    strncpy(out->path, cfg->storage_path, URL_MAX - 1);
    out->free_bytes  = 0;
    out->total_bytes = 0;

    /* Query free space via fileXio stat on the volume root */
    iox_stat_t st;
    if (fileXioGetStat(cfg->storage_path, &st) >= 0) {
        /* st.size holds free bytes on some implementations */
        out->free_bytes = (long long)st.size;
    }
}
