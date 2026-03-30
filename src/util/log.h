#ifndef LOG_H
#define LOG_H

#include <stdio.h>

/* ── Log levels ──────────────────────────────────────────────────────────── */
#define LOG_LEVEL_DEBUG  0
#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_ERROR  3

#ifndef LOG_LEVEL
#  define LOG_LEVEL  LOG_LEVEL_DEBUG
#endif

/* ── Log macros ──────────────────────────────────────────────────────────── */
#define _LOG(lvl, tag, fmt, ...) \
    printf("[" tag "] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#  define LOGD(fmt, ...) _LOG(DEBUG, "DBG", fmt, ##__VA_ARGS__)
#else
#  define LOGD(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#  define LOGI(fmt, ...) _LOG(INFO,  "INF", fmt, ##__VA_ARGS__)
#else
#  define LOGI(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#  define LOGW(fmt, ...) _LOG(WARN,  "WRN", fmt, ##__VA_ARGS__)
#else
#  define LOGW(fmt, ...) ((void)0)
#endif

#define LOGE(fmt, ...) _LOG(ERROR, "ERR", fmt, ##__VA_ARGS__)

/* ── Assert-style utility ────────────────────────────────────────────────── */
#define LOG_ASSERT(cond, msg) \
    do { if (!(cond)) { LOGE("ASSERT FAILED: " msg); } } while(0)

#endif /* LOG_H */
