#ifndef CATALOG_H
#define CATALOG_H

#include <stdint.h>
#include "genres.h"
#include "../main.h"

/* ── Game entry ──────────────────────────────────────────────────────────── */
typedef struct {
    char      identifier[IDENT_MAX];
    char      title[TITLE_MAX];
    char      description[DESC_MAX];
    Genre     genre;
    long long size_bytes;
    int       download_count;
} GameEntry;

/* ── Download status ─────────────────────────────────────────────────────── */
typedef enum {
    DL_STATUS_QUEUED = 0,
    DL_STATUS_ACTIVE,
    DL_STATUS_PAUSED,
    DL_STATUS_DONE,
    DL_STATUS_ERROR
} DlStatus;

typedef struct {
    char      identifier[IDENT_MAX];
    char      title[TITLE_MAX];
    char      dest_path[URL_MAX];
    DlStatus  status;
    long long total_bytes;
    long long recv_bytes;
    int       bytes_per_sec;
} DownloadEntry;

/* ── Storage info ─────────────────────────────────────────────────────────── */
typedef struct {
    char      path[URL_MAX];
    long long free_bytes;
    long long total_bytes;
} StorageInfo;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */
void catalog_init(void);
void catalog_shutdown(void);

/* ── Game list ───────────────────────────────────────────────────────────── */
/* Returns pointer into internal array; count set via *n_out.
   Valid until the next catalog_inject_entry() or catalog_sort_genre(). */
GameEntry *catalog_get_genre(Genre genre, int *n_out);

/* Upsert a game entry (from API response). */
void catalog_inject_entry(const GameEntry *entry);

/* Sort the list for a given genre: by_downloads=0 → name, 1 → downloads */
void catalog_sort_genre(Genre genre, int by_downloads);

/* Find index of entry with given identifier in the current genre list.
   Returns -1 if not found. */
int catalog_find_by_id(const char *identifier);

/* ── Cover art cache ─────────────────────────────────────────────────────── */
/* Store JPEG bytes for later retrieval (heap-allocated internally). */
void catalog_store_cover(const char *identifier,
                         const uint8_t *jpg, int len);
/* Retrieve cached cover bytes; returns 0 on miss. */
int  catalog_get_cover(const char *identifier,
                       const uint8_t **jpg_out, int *len_out);

/* ── Download queue ─────────────────────────────────────────────────────── */
void  catalog_enqueue_download(const char *identifier);
int   catalog_dl_count(void);
int   catalog_dl_active_count(void);
DownloadEntry *catalog_dl_get(int idx);
void  catalog_dl_toggle_pause(int idx);
void  catalog_dl_cancel(int idx);
void  catalog_dl_clear_completed(void);

/* ── Storage info ────────────────────────────────────────────────────────── */
void catalog_get_storage_info(StorageInfo *out);

#endif /* CATALOG_H */
