/*
 * http.c — HTTP/1.1 client over the PS2 lwIP socket API (ps2ip)
 *
 * Features:
 *  • DNS resolution via ps2ip getaddrinfo()
 *  • GET with range support (resume)
 *  • Chunked transfer-encoding decode
 *  • Optional proxy (HTTP 1.1 proxy, no CONNECT tunnelling)
 *  • Configurable timeout
 *
 * NOTE: HTTPS is not natively supported on PS2 (no TLS stack in ps2sdk).
 *       Use a local HTTP proxy (e.g. nginx on a Pi) that terminates TLS
 *       and re-serves content over plain HTTP to the PS2.
 *       Set proxy_host/proxy_port in HttpOptions if needed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <ps2ip.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "http.h"
#include "../util/log.h"

#define HTTP_RECV_BUF   4096
#define HTTP_HEADER_MAX 4096
#define DEFAULT_TIMEOUT 30

/* ── Module state ─────────────────────────────────────────────────────────── */
static int s_init = 0;

void http_init(void)  { s_init = 1; LOGI("HTTP client ready"); }
void http_shutdown(void) { s_init = 0; }

/* ── URL helpers ──────────────────────────────────────────────────────────── */

void http_urlencode(const char *src, char *dst, int dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; src[i] && j < dst_len - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

/* ── TCP helpers ─────────────────────────────────────────────────────────── */

static int tcp_connect(const char *host, int port, int timeout_sec)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        LOGE("DNS failed for %s", host);
        return HTTP_ERR_RESOLVE;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return HTTP_ERR_CONNECT;
    }

    /* Set socket timeout */
    struct timeval tv;
    tv.tv_sec  = timeout_sec > 0 ? timeout_sec : DEFAULT_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        LOGE("connect to %s:%d failed", host, port);
        disconnect(fd);
        freeaddrinfo(res);
        return HTTP_ERR_CONNECT;
    }

    freeaddrinfo(res);
    return fd;
}

static int tcp_send_fmt(int fd, const char *fmt, ...)
{
    char buf[1024];
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    if (n <= 0) return -1;
    if (send(fd, buf, n, 0) != n) return HTTP_ERR_SEND;
    return 0;
}

/* ── Header parsing ──────────────────────────────────────────────────────── */

/* Read headers into buf (up to HTTP_HEADER_MAX bytes).
   Returns number of bytes read (including CRLFCRLF) or <0 on error. */
static int read_headers(int fd, char *buf, int buf_len)
{
    int total = 0;
    while (total < buf_len - 1) {
        int r = recv(fd, buf + total, 1, 0);
        if (r <= 0) return HTTP_ERR_RECV;
        total++;
        buf[total] = '\0';
        if (total >= 4 &&
            buf[total-4] == '\r' && buf[total-3] == '\n' &&
            buf[total-2] == '\r' && buf[total-1] == '\n')
            break;
    }
    return total;
}

static int parse_status(const char *headers)
{
    /* "HTTP/1.x NNN ..." */
    const char *p = headers;
    while (*p && *p != ' ') p++;
    if (*p) p++;
    return atoi(p);
}

static long long parse_content_length(const char *headers)
{
    const char *p = strstr(headers, "Content-Length:");
    if (!p) p = strstr(headers, "content-length:");
    if (!p) return -1LL;
    p += 15; /* skip "Content-Length:" */
    while (*p == ' ') p++;
    return (long long)atoll(p);
}

static int is_chunked(const char *headers)
{
    return (strstr(headers, "chunked") != NULL);
}

/* ── Chunked transfer decoder ─────────────────────────────────────────────── */

static int recv_chunked(int fd, HttpBodyCb cb, void *user,
                        uint8_t **out_buf, int *out_len)
{
    /* Accumulate into heap if cb == NULL, else stream via cb */
    int capacity = 0, total = 0;
    uint8_t *accum = NULL;

    for (;;) {
        /* Read chunk size line */
        char szline[32];
        int  si = 0;
        while (si < (int)sizeof(szline) - 1) {
            char c;
            int r = recv(fd, &c, 1, 0);
            if (r <= 0) { free(accum); return HTTP_ERR_RECV; }
            if (c == '\n') break;
            if (c != '\r') szline[si++] = c;
        }
        szline[si] = '\0';
        int chunk_len = (int)strtol(szline, NULL, 16);
        if (chunk_len == 0) break;   /* final chunk */

        /* Read chunk body */
        int remaining = chunk_len;
        while (remaining > 0) {
            uint8_t tmp[HTTP_RECV_BUF];
            int want = remaining < HTTP_RECV_BUF ? remaining : HTTP_RECV_BUF;
            int got  = recv(fd, tmp, want, 0);
            if (got <= 0) { free(accum); return HTTP_ERR_RECV; }
            remaining -= got;
            total     += got;

            if (cb) {
                if (cb(tmp, got, user) != 0) { free(accum); return HTTP_ERR_RECV; }
            } else {
                /* Grow accumulation buffer */
                if (total > capacity) {
                    capacity = total + HTTP_RECV_BUF;
                    uint8_t *nb = realloc(accum, (size_t)capacity);
                    if (!nb) { free(accum); return HTTP_ERR_NOMEM; }
                    accum = nb;
                }
                memcpy(accum + total - got, tmp, (size_t)got);
            }
        }
        /* Skip trailing CRLF */
        char crlf[2];
        recv(fd, crlf, 2, 0);
    }

    if (!cb && out_buf) {
        *out_buf = accum;
        *out_len = total;
    } else {
        free(accum);
    }
    return HTTP_OK;
}

/* ── Core GET implementation ──────────────────────────────────────────────── */

static int do_get(const char *connect_host, int connect_port,
                  const char *request_host, const char *path,
                  const char *range_hdr,
                  HttpBodyCb cb, void *user,
                  HttpResponse *resp,
                  const HttpOptions *opts)
{
    int timeout = (opts && opts->timeout_sec > 0) ? opts->timeout_sec
                                                   : DEFAULT_TIMEOUT;

    int fd = tcp_connect(connect_host, connect_port, timeout);
    if (fd < 0) return fd;

    /* Build request */
    int ret = 0;
    ret += tcp_send_fmt(fd, "GET %s HTTP/1.1\r\n", path);
    ret += tcp_send_fmt(fd, "Host: %s\r\n", request_host);
    ret += tcp_send_fmt(fd, "User-Agent: PSBBN-Downloader/1.0\r\n");
    ret += tcp_send_fmt(fd, "Connection: close\r\n");
    ret += tcp_send_fmt(fd, "Accept: */*\r\n");
    if (range_hdr)
        ret += tcp_send_fmt(fd, "Range: %s\r\n", range_hdr);
    ret += tcp_send_fmt(fd, "\r\n");

    if (ret != 0) { disconnect(fd); return HTTP_ERR_SEND; }

    /* Read response headers */
    char *hdr = (char *)malloc(HTTP_HEADER_MAX);
    if (!hdr) { disconnect(fd); return HTTP_ERR_NOMEM; }
    memset(hdr, 0, HTTP_HEADER_MAX);

    if (read_headers(fd, hdr, HTTP_HEADER_MAX) < 0) {
        free(hdr); disconnect(fd); return HTTP_ERR_RECV;
    }

    int status = parse_status(hdr);
    LOGD("HTTP %d  %s%s", status, request_host, path);

    if (resp) {
        resp->status         = status;
        resp->content_length = parse_content_length(hdr);
        resp->body           = NULL;
        resp->body_len       = 0;
    }

    if (status < 200 || status >= 400) {
        LOGE("HTTP error %d", status);
        free(hdr);
        disconnect(fd);
        return HTTP_ERR_STATUS;
    }

    /* Receive body */
    if (is_chunked(hdr)) {
        uint8_t **out_b = resp ? &resp->body    : NULL;
        int      *out_l = resp ? &resp->body_len: NULL;
        ret = recv_chunked(fd, cb, user, out_b, out_l);
    } else {
        long long cl = parse_content_length(hdr);
        if (cb) {
            /* Stream */
            uint8_t tmp[HTTP_RECV_BUF];
            long long received = 0;
            while (cl < 0 || received < cl) {
                int want = HTTP_RECV_BUF;
                int got  = recv(fd, tmp, want, 0);
                if (got <= 0) break;
                received += got;
                if (cb(tmp, got, user) != 0) break;
            }
        } else if (resp) {
            /* Accumulate */
            if (cl > 0) {
                resp->body = (uint8_t *)malloc((size_t)cl + 1);
                if (!resp->body) { free(hdr); disconnect(fd); return HTTP_ERR_NOMEM; }
                int received = 0;
                while (received < (int)cl) {
                    int got = recv(fd, resp->body + received,
                                   (int)cl - received, 0);
                    if (got <= 0) break;
                    received += got;
                }
                resp->body[received]  = '\0';
                resp->body_len        = received;
            }
        }
    }

    free(hdr);
    disconnect(fd);
    return ret;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int http_get(const char *host, int port, const char *path,
             HttpResponse *resp, const HttpOptions *opts)
{
    const char *ch = (opts && opts->proxy_host) ? opts->proxy_host : host;
    int         cp = (opts && opts->proxy_host) ? opts->proxy_port : port;

    /* When using a proxy, specify the full URL in the request path */
    char full_path[URL_MAX];
    if (opts && opts->proxy_host)
        snprintf(full_path, sizeof(full_path), "http://%s:%d%s", host, port, path);
    else
        strncpy(full_path, path, sizeof(full_path) - 1);

    return do_get(ch, cp, host, full_path, NULL, NULL, NULL, resp, opts);
}

int http_get_range(const char *host, int port, const char *path,
                   long long range_start, long long range_end,
                   HttpBodyCb cb, void *user,
                   long long *total_out,
                   const HttpOptions *opts)
{
    char range_hdr[48];
    if (range_end < 0)
        snprintf(range_hdr, sizeof(range_hdr), "bytes=%lld-", range_start);
    else
        snprintf(range_hdr, sizeof(range_hdr), "bytes=%lld-%lld",
                 range_start, range_end);

    HttpResponse dummy;
    memset(&dummy, 0, sizeof(dummy));

    const char *ch = (opts && opts->proxy_host) ? opts->proxy_host : host;
    int         cp = (opts && opts->proxy_host) ? opts->proxy_port : port;

    char full_path[URL_MAX];
    if (opts && opts->proxy_host)
        snprintf(full_path, sizeof(full_path), "http://%s:%d%s", host, port, path);
    else
        strncpy(full_path, path, sizeof(full_path) - 1);

    int ret = do_get(ch, cp, host, full_path, range_hdr, cb, user, &dummy, opts);

    if (total_out)
        *total_out = dummy.content_length;

    http_response_free(&dummy);
    return ret;
}

void http_response_free(HttpResponse *resp)
{
    if (resp && resp->body) {
        free(resp->body);
        resp->body     = NULL;
        resp->body_len = 0;
    }
}
