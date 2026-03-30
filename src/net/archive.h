#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "../catalog/catalog.h"
#include "../catalog/genres.h"

/* archive.org API wrapper — non-blocking searches + streaming ISO download */

/* ── API host ─────────────────────────────────────────────────────────────── */
#define ARCHIVE_HOST      "archive.org"
#define ARCHIVE_PORT       80

/* ── Kick off an async genre search.  Results available via archive_get_results(). */
void archive_search_genre(Genre genre, int page, int rows);

/* ── Keyword/title free-text search. */
void archive_search_text(const char *query, int page, int rows);

/* ── Returns the last result set.  n_out = count.
      Valid until the next call to archive_search_*. */
const GameEntry *archive_get_results(int *n_out);

/* ── Returns 1 while a fetch is in flight. */
int archive_is_fetching(void);

/* ── Fetch cover art JPEG for identifier into catalog cover cache.
      Blocking; call from a background context if possible. */
int archive_fetch_cover(const char *identifier);

/* ── Start streaming an ISO to storage.
      This is the heavy download path — calls http_get_range internally
      and writes directly to the open file descriptor fd.
      Returns HTTP_OK (0) on success, negative on failure. */
int archive_download_iso(const char *identifier,
                         const char *filename,
                         int         dest_fd,
                         long long   resume_offset,
                         long long  *bytes_received_out,
                         long long  *total_size_out,
                         volatile int *cancel_flag);

#endif /* ARCHIVE_H */
