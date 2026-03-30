#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stddef.h>

/* ── Return codes ─────────────────────────────────────────────────────────── */
#define HTTP_OK            0
#define HTTP_ERR_RESOLVE  -1
#define HTTP_ERR_CONNECT  -2
#define HTTP_ERR_SEND     -3
#define HTTP_ERR_RECV     -4
#define HTTP_ERR_STATUS   -5
#define HTTP_ERR_NOMEM    -6
#define HTTP_ERR_ARGS     -7
#define HTTP_ERR_TIMEOUT  -8

/* ── Response ────────────────────────────────────────────────────────────── */
typedef struct {
    int     status;           /* HTTP status code, e.g. 200               */
    uint8_t *body;            /* heap-allocated body; caller must free()  */
    int     body_len;
    /* Populated headers we care about */
    long long content_length; /* -1 if unknown (chunked transfer)         */
} HttpResponse;

/* ── Request options ─────────────────────────────────────────────────────── */
typedef struct {
    const char *proxy_host;   /* NULL = direct                            */
    int         proxy_port;
    int         timeout_sec;  /* 0 = 30 s default                         */
} HttpOptions;

/* ── Download-to-file callback ───────────────────────────────────────────── */
/* Called repeatedly with chunks of the response body.                        */
/* Return 0 to continue, non-zero to abort.                                   */
typedef int (*HttpBodyCb)(const uint8_t *chunk, int len, void *user);

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Must be called once before using any http_* functions. */
void http_init(void);
void http_shutdown(void);

/*
 * http_get — perform a blocking HTTP GET and return the entire response body
 * in a heap buffer.  Suitable for small payloads (catalog JSON, cover JPEG).
 *
 * host   — e.g. "archive.org"
 * path   — e.g. "/advancedsearch.php?q=..."
 * resp   — out; caller must free resp->body
 * opts   — NULL = defaults (direct, 30 s timeout)
 */
int http_get(const char *host, int port, const char *path,
             HttpResponse *resp, const HttpOptions *opts);

/*
 * http_get_range — partial GET (Resume support) for large ISO downloads.
 * range_start / range_end: byte offsets (range_end = -1 → end of file)
 * Delivers body segments via the callback; does NOT allocate a heap buffer.
 */
int http_get_range(const char *host, int port, const char *path,
                   long long range_start, long long range_end,
                   HttpBodyCb cb, void *user,
                   long long *total_out,
                   const HttpOptions *opts);

/* Free an HttpResponse (just frees body if non-NULL). */
void http_response_free(HttpResponse *resp);

/* URL encode a string into dst (dst must be large enough). */
void http_urlencode(const char *src, char *dst, int dst_len);

#endif /* HTTP_H */
