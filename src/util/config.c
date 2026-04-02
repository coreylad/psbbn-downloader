/*
 * config.c — load/save key=value settings file
 *
 * Format (one setting per line, '#' comments ignored):
 *
 *   use_dhcp=1
 *   static_ip=192.168.1.100
 *   proxy_host=192.168.1.1
 *   proxy_port=8080
 *   storage_path=mass:/PS2ISO
 *   pal_mode=0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fileio.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include "config.h"
#include "log.h"

static AppConfig s_cfg;

/* ── Defaults ─────────────────────────────────────────────────────────────── */
static void set_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.use_dhcp    = 1;
    strncpy(s_cfg.static_ip,  "192.168.1.100", 15);
    strncpy(s_cfg.static_nm,  "255.255.255.0", 15);
    strncpy(s_cfg.static_gw,  "192.168.1.1",   15);
    strncpy(s_cfg.static_dns, "8.8.8.8",        15);
    s_cfg.proxy_port  = 0;
    strncpy(s_cfg.storage_path, "mass:/PS2ISO", 127);
    s_cfg.pal_mode    = 0;
}

/* ── Parser ───────────────────────────────────────────────────────────────── */
static void apply_line(char *line)
{
    /* Strip newline */
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    nl = strchr(line, '\r');
    if (nl) *nl = '\0';

    /* Comment or blank */
    if (line[0] == '#' || line[0] == '\0') return;

    char *eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    const char *key = line;
    const char *val = eq + 1;

    if (strcmp(key, "use_dhcp")     == 0) s_cfg.use_dhcp    = atoi(val);
    else if (strcmp(key, "static_ip")  == 0) strncpy(s_cfg.static_ip,  val, 15);
    else if (strcmp(key, "static_nm")  == 0) strncpy(s_cfg.static_nm,  val, 15);
    else if (strcmp(key, "static_gw")  == 0) strncpy(s_cfg.static_gw,  val, 15);
    else if (strcmp(key, "static_dns") == 0) strncpy(s_cfg.static_dns, val, 15);
    else if (strcmp(key, "proxy_host") == 0) strncpy(s_cfg.proxy_host, val, 63);
    else if (strcmp(key, "proxy_port") == 0) s_cfg.proxy_port = atoi(val);
    else if (strcmp(key, "storage_path") == 0) strncpy(s_cfg.storage_path, val, 127);
    else if (strcmp(key, "pal_mode")   == 0) s_cfg.pal_mode   = atoi(val);
}

static int load_from(const char *path)
{
    int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
    if (fd < 0) return 0;

    char  buf[256];
    char  line[128];
    int   li = 0;

    while (1) {
        int r = fileXioRead(fd, buf, sizeof(buf));
        if (r <= 0) break;
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                line[li] = '\0';
                apply_line(line);
                li = 0;
            } else if (li < (int)sizeof(line) - 1) {
                line[li++] = buf[i];
            }
        }
    }
    if (li > 0) { line[li] = '\0'; apply_line(line); }

    fileXioClose(fd);
    return 1;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void config_init(void)
{
    set_defaults();

    /* Try USB first then MC */
    if (load_from(CFG_PATH_USB)) {
        strncpy(s_cfg.cfg_file_path, CFG_PATH_USB, 127);
        LOGI("Config loaded from USB");
    } else if (load_from(CFG_PATH_MC)) {
        strncpy(s_cfg.cfg_file_path, CFG_PATH_MC, 127);
        LOGI("Config loaded from MC");
    } else {
        /* Default save path = USB */
        strncpy(s_cfg.cfg_file_path, CFG_PATH_USB, 127);
        LOGI("No config file — using defaults");
    }
}

AppConfig *config_get(void) { return &s_cfg; }

void config_save(void)
{
    /* Ensure directory exists */
    fileXioMkdir("mass:/PSBBN", 0777);
    fileXioMkdir("mc0:/PSBBN",  0777);

    int fd = fileXioOpen(s_cfg.cfg_file_path,
                         FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0644);
    if (fd < 0) {
        LOGE("Cannot write config to %s", s_cfg.cfg_file_path);
        return;
    }

    char buf[1024];
    int  n = snprintf(buf, sizeof(buf),
        "# PSBBN Downloader configuration\n"
        "use_dhcp=%d\n"
        "static_ip=%s\n"
        "static_nm=%s\n"
        "static_gw=%s\n"
        "static_dns=%s\n"
        "proxy_host=%s\n"
        "proxy_port=%d\n"
        "storage_path=%s\n"
        "pal_mode=%d\n",
        s_cfg.use_dhcp,
        s_cfg.static_ip, s_cfg.static_nm,
        s_cfg.static_gw, s_cfg.static_dns,
        s_cfg.proxy_host, s_cfg.proxy_port,
        s_cfg.storage_path,
        s_cfg.pal_mode
    );

    fileXioWrite(fd, buf, n);
    fileXioClose(fd);
    LOGI("Config saved to %s", s_cfg.cfg_file_path);
}
