/*
 * archive.c — archive.org Advanced Search API + ISO download
 *
 * Search response format (JSON):
 * {
 *   "response": {
 *     "numFound": 1234,
 *     "docs": [
 *       {
 *         "identifier": "...",
 *         "title": "...",
 *         "description": "...",
 *         "subject": ["str", ...] or "str",
 *         "item_size": 1234567,
 *         "downloads": 999
 *       },
 *       ...
 *     ]
 *   }
 * }
 *
 * Cover art URL pattern:
 *   http://archive.org/services/img/{identifier}
 *
 * ISO download URL:
 *   http://archive.org/download/{identifier}/{filename}
 *
 * For large ISOs we use HTTP range requests for resume capability.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "archive.h"
#include "http.h"
#include "../catalog/catalog.h"
#include "../catalog/genres.h"
#include "../util/json.h"
#include "../util/log.h"
#include "../util/config.h"
#include "main.h"

/* ── Internal state ──────────────────────────────────────────────────────── */
static GameEntry  s_results[MAX_SEARCH_RESULTS];
static int        s_result_count  = 0;
static int        s_is_fetching   = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static HttpOptions make_opts(void)
{
    AppConfig *cfg = config_get();
    HttpOptions o  = {0};
    if (cfg->proxy_host[0]) {
        o.proxy_host = cfg->proxy_host;
        o.proxy_port = cfg->proxy_port ? cfg->proxy_port : 8080;
    }
    o.timeout_sec = 30;
    return o;
}

/* Build the archive.org Advanced Search URL path. */
static void build_search_path(const char *q_encoded, int page, int rows,
                               char *out, int out_len)
{
    snprintf(out, (size_t)out_len,
             "/advancedsearch.php?q=%s"
             "&fl[]=identifier"
             "&fl[]=title"
             "&fl[]=description"
             "&fl[]=subject"
             "&fl[]=item_size"
             "&fl[]=downloads"
             "&rows=%d"
             "&page=%d"
             "&output=json",
             q_encoded, rows, page + 1);
}

/* ── JSON parsing ─────────────────────────────────────────────────────────── */

/* Parse one docs[] entry object starting at *pp.
   Returns 1 if a complete entry was extracted. */
static int parse_doc(const char *json, JsonCursor *cur, GameEntry *out)
{
    memset(out, 0, sizeof(*out));

    /* We expect an object: { "key": value, ... } */
    if (!json_enter_object(json, cur)) return 0;

    char key[32], val[DESC_MAX];

    while (json_next_key(json, cur, key, sizeof(key))) {
        if (strcmp(key, "identifier") == 0) {
            json_read_string(json, cur, out->identifier, IDENT_MAX);
        } else if (strcmp(key, "title") == 0) {
            json_read_string(json, cur, out->title, TITLE_MAX);
        } else if (strcmp(key, "description") == 0) {
            /* description may be a string or array; take first element */
            if (json_peek(json, cur) == JSON_T_ARRAY) {
                json_enter_array(json, cur);
                json_read_string(json, cur, out->description, DESC_MAX);
                json_skip_rest(json, cur);
            } else {
                json_read_string(json, cur, out->description, DESC_MAX);
            }
        } else if (strcmp(key, "subject") == 0) {
            /* subject may be a string or array */
            char subjects[256] = {0};
            if (json_peek(json, cur) == JSON_T_ARRAY) {
                json_enter_array(json, cur);
                char sub[64];
                while (json_read_string(json, cur, sub, sizeof(sub))) {
                    if (subjects[0]) strncat(subjects, " ", sizeof(subjects)-strlen(subjects)-1);
                    strncat(subjects, sub, sizeof(subjects)-strlen(subjects)-1);
                }
            } else {
                json_read_string(json, cur, subjects, sizeof(subjects));
            }
            out->genre = genre_from_subject(subjects);
        } else if (strcmp(key, "item_size") == 0) {
            if (json_read_string(json, cur, val, sizeof(val)))
                out->size_bytes = (long long)atoll(val);
        } else if (strcmp(key, "downloads") == 0) {
            if (json_read_string(json, cur, val, sizeof(val)))
                out->download_count = atoi(val);
        } else {
            json_skip_value(json, cur);
        }
    }

    if (!out->identifier[0]) return 0;
    if (!out->title[0]) strncpy(out->title, out->identifier, TITLE_MAX - 1);
    return 1;
}

static int parse_search_response(const uint8_t *body, int len)
{
    if (!body || len <= 0) return 0;

    const char *json = (const char *)body;
    JsonCursor  cur  = {0};
    int count = 0;

    /* Navigate: { "response": { "docs": [ ... ] } } */
    if (!json_enter_object(json, &cur))  return 0;
    char k[32];
    while (json_next_key(json, &cur, k, sizeof(k))) {
        if (strcmp(k, "response") == 0) {
            if (!json_enter_object(json, &cur)) break;
            while (json_next_key(json, &cur, k, sizeof(k))) {
                if (strcmp(k, "docs") == 0) {
                    if (!json_enter_array(json, &cur)) break;
                    while (count < MAX_SEARCH_RESULTS &&
                           json_peek(json, &cur) == JSON_T_OBJECT) {
                        if (!parse_doc(json, &cur, &s_results[count]))
                            break;
                        count++;
                    }
                    break;
                } else {
                    json_skip_value(json, &cur);
                }
            }
            break;
        } else {
            json_skip_value(json, &cur);
        }
    }

    return count;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

const GameEntry *archive_get_results(int *n_out)
{
    if (n_out) *n_out = s_result_count;
    return s_results;
}

int archive_is_fetching(void) { return s_is_fetching; }

static void do_search(const char *q_raw, int page, int rows)
{
    s_is_fetching  = 1;
    s_result_count = 0;

    char q_enc[512];
    http_urlencode(q_raw, q_enc, sizeof(q_enc));

    char path[768];
    build_search_path(q_enc, page, rows, path, sizeof(path));

    LOGI("archive search: %s", q_raw);

    HttpResponse resp = {0};
    HttpOptions  opts = make_opts();

    int ret = http_get(ARCHIVE_HOST, ARCHIVE_PORT, path, &resp, &opts);
    if (ret == HTTP_OK && resp.body) {
        s_result_count = parse_search_response(resp.body, resp.body_len);
        LOGI("archive: %d results", s_result_count);

        /* Persist results into catalog */
        for (int i = 0; i < s_result_count; i++)
            catalog_inject_entry(&s_results[i]);
    } else {
        LOGE("archive search failed: %d", ret);
    }

    http_response_free(&resp);
    s_is_fetching = 0;
}

void archive_search_genre(Genre genre, int page, int rows)
{
    char q[256];
    if (genre == GENRE_ALL || GENRE_SEARCH_TERMS[genre][0] == '\0') {
        snprintf(q, sizeof(q),
                 "mediatype:software subject:\"PlayStation 2\"");
    } else {
        snprintf(q, sizeof(q),
                 "mediatype:software subject:\"PlayStation 2\" "
                 "subject:\"%s\"",
                 GENRE_SEARCH_TERMS[genre]);
    }
    do_search(q, page, rows);
}

void archive_search_text(const char *query, int page, int rows)
{
    char q[256];
    snprintf(q, sizeof(q),
             "mediatype:software subject:\"PlayStation 2\" title:\"%s\"",
             query);
    do_search(q, page, rows);
}

/* ── Cover art ───────────────────────────────────────────────────────────── */

int archive_fetch_cover(const char *identifier)
{
    if (!identifier || !identifier[0]) return -1;

    char path[256];
    snprintf(path, sizeof(path), "/services/img/%s", identifier);

    HttpResponse resp = {0};
    HttpOptions  opts = make_opts();

    int ret = http_get(ARCHIVE_HOST, ARCHIVE_PORT, path, &resp, &opts);
    if (ret != HTTP_OK || !resp.body || resp.body_len <= 0) {
        http_response_free(&resp);
        return -1;
    }

    /* Store JPEG in catalog cover cache */
    catalog_store_cover(identifier, resp.body, resp.body_len);
    http_response_free(&resp);
    return 0;
}

/* ── ISO Download ─────────────────────────────────────────────────────────── */

typedef struct {
    int         fd;
    long long  *bytes_out;
    volatile int *cancel;
} DlCtx;

static int dl_write_cb(const uint8_t *chunk, int len, void *user)
{
    DlCtx *ctx = (DlCtx *)user;

    if (ctx->cancel && *ctx->cancel) return 1;   /* abort */

    /* Write to file via fileXio */
    int written = 0;
    while (written < len) {
        int r = fileXioWrite(ctx->fd, chunk + written, len - written);
        if (r <= 0) {
            LOGE("fileXioWrite failed: %d", r);
            return 1; /* abort */
        }
        written += r;
    }
    if (ctx->bytes_out) *ctx->bytes_out += written;
    return 0;
}

int archive_download_iso(const char *identifier,
                         const char *filename,
                         int         dest_fd,
                         long long   resume_offset,
                         long long  *bytes_received_out,
                         long long  *total_size_out,
                         volatile int *cancel_flag)
{
    char path[URL_MAX];
    snprintf(path, sizeof(path), "/download/%s/%s", identifier, filename);

    DlCtx ctx;
    ctx.fd         = dest_fd;
    ctx.bytes_out  = bytes_received_out;
    ctx.cancel     = cancel_flag;
    if (bytes_received_out) *bytes_received_out = 0;

    HttpOptions opts = make_opts();
    opts.timeout_sec = 120;   /* large files need longer timeouts */

    LOGI("ISO download: %s (resume=%lld)", path, resume_offset);

    return http_get_range(ARCHIVE_HOST, ARCHIVE_PORT, path,
                          resume_offset, -1LL,
                          dl_write_cb, &ctx,
                          total_size_out,
                          &opts);
}
