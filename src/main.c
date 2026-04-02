/*
 * PSBBN Downloader — main.c
 * Entry point: hardware init, IRX loading, PSBBN detection, main loop.
 *
 * Build target: PS2 EE (mips r5900)
 * Toolchain   : ps2dev / ps2sdk
 */

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <ps2ip.h>
#include <netman.h>

#include "main.h"
#include "util/log.h"
#include "util/config.h"
#include "input/pad.h"
#include "ui/ui.h"
#include "ui/home_screen.h"
#include "ui/genre_screen.h"
#include "ui/detail_screen.h"
#include "ui/download_screen.h"
#include "ui/search_screen.h"
#include "net/http.h"
#include "catalog/catalog.h"

/* ── Global app state (accessible app-wide via extern) ──────────────────── */
AppState g_state = {
    .screen           = SCREEN_HOME,
    .prev_screen      = SCREEN_HOME,
    .running          = 1,
    .net_status       = NET_DISCONNECTED,
    .net_ip           = {0},
    .selected_genre   = 0,   /* GENRE_ALL */
    .selected_game    = 0,
    .list_scroll      = 0,
    .search_query     = {0},
    .search_cursor    = 0,
    .active_downloads = 0,
};

/* ── Embedded IRX symbols (linked via objcopy from .irx binaries) ────────── */
extern unsigned char ps2ip_irx[];
extern unsigned int  ps2ip_irx_size;
extern unsigned char netman_irx[];
extern unsigned int  netman_irx_size;
extern unsigned char smap_irx[];
extern unsigned int  smap_irx_size;
extern unsigned char usbd_irx[];
extern unsigned int  usbd_irx_size;
extern unsigned char usbhdfsd_irx[];
extern unsigned int  usbhdfsd_irx_size;

/* ── IRX loading ─────────────────────────────────────────────────────────── */

/* Returns 1 if the network is already up (PSBBN environment). */
static int net_already_initialized(void)
{
    /* If ps2ip is available and we can get a non-zero IP, network is up. */
    t_ip_info info;
    if (ps2ip_getconfig("sm0", &info) == 0) {
        if (info.ipaddr.s_addr != 0) {
            snprintf(g_state.net_ip, sizeof(g_state.net_ip),
                     "%d.%d.%d.%d",
                     (int)(info.ipaddr.s_addr      & 0xFF),
                     (int)(info.ipaddr.s_addr >> 8  & 0xFF),
                     (int)(info.ipaddr.s_addr >> 16 & 0xFF),
                     (int)(info.ipaddr.s_addr >> 24 & 0xFF));
            LOGI("PSBBN network detected: %s", g_state.net_ip);
            return 1;
        }
    }
    return 0;
}

static void load_irx_modules(void)
{
    int ret;

    LOGI("Loading IOP modules...");

    SifInitRpc(0);

    /* Reset IOP only when running standalone (not under PSBBN/OPL). */
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync())        {}

    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    sbv_patch_enable_lmb();

    /* Network modules */
    ret = SifExecModuleBuffer(netman_irx, netman_irx_size, 0, NULL, NULL);
    if (ret < 0) { LOGE("netman load failed: %d", ret); }

    ret = SifExecModuleBuffer(smap_irx, smap_irx_size, 0, NULL, NULL);
    if (ret < 0) { LOGE("smap load failed: %d", ret); }

    ret = SifExecModuleBuffer(ps2ip_irx, ps2ip_irx_size, 0, NULL, NULL);
    if (ret < 0) { LOGE("ps2ip load failed: %d", ret); }

    /* USB mass storage */
    ret = SifExecModuleBuffer(usbd_irx, usbd_irx_size, 0, NULL, NULL);
    if (ret < 0) { LOGW("usbd load failed: %d (USB may be unavailable)", ret); }

    ret = SifExecModuleBuffer(usbhdfsd_irx, usbhdfsd_irx_size, 0, NULL, NULL);
    if (ret < 0) { LOGW("usbhdfsd load failed: %d", ret); }

    LOGI("IOP modules loaded.");
}

/* Parse dotted IPv4 into ip4_addr without depending on lwIP headers. */
static int parse_ipv4(const char *s, struct ip4_addr *out)
{
    unsigned int a, b, c, d;
    if (!s || !out) return 0;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    IP4_ADDR(out, a, b, c, d);
    return 1;
}

/* ── Network init ────────────────────────────────────────────────────────── */
static void net_init(void)
{
    const AppConfig *cfg = config_get();
    struct ip4_addr ip = {0}, nm = {0}, gw = {0};
    t_ip_info info;

    if (net_already_initialized()) {
        g_state.net_status = NET_CONNECTED;
        return;
    }

    g_state.net_status = NET_CONNECTING;
    LOGI("Initialising network...");

    NetManInit();

    if (ps2ipInit(&ip, &nm, &gw) != 0) {
        LOGE("ps2ipInit failed");
        g_state.net_status = NET_ERROR;
        return;
    }

    /* DHCP or static IP from config */
    memset(&info, 0, sizeof(info));
    strcpy(info.netif_name, "sm0");

    if (cfg->use_dhcp) {
        LOGI("DHCP mode");
        info.dhcp_enabled = 1;
        if (ps2ip_setconfig(&info) == 0) {
            LOGE("DHCP setup failed");
            g_state.net_status = NET_ERROR;
            return;
        }
    } else {
        info.dhcp_enabled   = 0;
        if (!parse_ipv4(cfg->static_ip, &ip) ||
            !parse_ipv4(cfg->static_nm, &nm) ||
            !parse_ipv4(cfg->static_gw, &gw)) {
            LOGE("Invalid static IP configuration");
            g_state.net_status = NET_ERROR;
            return;
        }

        info.ipaddr.s_addr  = ip.addr;
        info.netmask.s_addr = nm.addr;
        info.gw.s_addr      = gw.addr;
        LOGI("Static IP: %s", cfg->static_ip);
        if (ps2ip_setconfig(&info) == 0) {
            LOGE("Static IP setup failed");
            g_state.net_status = NET_ERROR;
            return;
        }
    }

    /* Wait up to 10 s for link */
    for (int i = 0; i < 100; i++) {
        t_ip_info netinfo;
        if (ps2ip_getconfig("sm0", &netinfo) == 0 && netinfo.ipaddr.s_addr != 0) {
            snprintf(g_state.net_ip, sizeof(g_state.net_ip),
                     "%d.%d.%d.%d",
                     (int)(netinfo.ipaddr.s_addr       & 0xFF),
                     (int)((netinfo.ipaddr.s_addr >> 8) & 0xFF),
                     (int)((netinfo.ipaddr.s_addr >>16) & 0xFF),
                     (int)((netinfo.ipaddr.s_addr >>24) & 0xFF));
            g_state.net_status = NET_CONNECTED;
            LOGI("Network ready: %s", g_state.net_ip);
            return;
        }
        nopdelay();   /* ~100 ms */
    }

    LOGE("Network timeout — no IP assigned");
    g_state.net_status = NET_ERROR;
}

/* ── Screen dispatch table ───────────────────────────────────────────────── */
typedef struct {
    void (*init)(void);
    void (*update)(void);
    void (*render)(void);
    void (*destroy)(void);
} ScreenOps;

static const ScreenOps k_screens[SCREEN_COUNT] = {
    [SCREEN_HOME]     = { home_screen_init,     home_screen_update,     home_screen_render,     home_screen_destroy     },
    [SCREEN_GENRE]    = { genre_screen_init,    genre_screen_update,    genre_screen_render,    genre_screen_destroy    },
    [SCREEN_DETAIL]   = { detail_screen_init,   detail_screen_update,   detail_screen_render,   detail_screen_destroy   },
    [SCREEN_DOWNLOAD] = { download_screen_init, download_screen_update, download_screen_render, download_screen_destroy },
    [SCREEN_SEARCH]   = { search_screen_init,   search_screen_update,   search_screen_render,   search_screen_destroy   },
};

static int s_pending_screen = -1;   /* queued transition */

void app_switch_screen(AppScreen screen)
{
    s_pending_screen = (int)screen;
}

static void do_screen_transition(AppScreen next)
{
    if (k_screens[g_state.screen].destroy)
        k_screens[g_state.screen].destroy();

    g_state.prev_screen = g_state.screen;
    g_state.screen      = next;

    if (k_screens[next].init)
        k_screens[next].init();
}

/* ── Entry point ─────────────────────────────────────────────────────────── */
int main(int argc __attribute__((unused)),
         char *argv[] __attribute__((unused)))
{
    /* 1. Config (must be first — everything else reads it) */
    config_init();

    /* 2. IOP / IRX bootstrap */
    if (!net_already_initialized()) {
        load_irx_modules();
    }

    /* 3. Graphics + DMA */
    ui_init();

    /* 4. Controller input */
    pad_init();

    /* 5. Network */
    net_init();

    /* 6. HTTP layer */
    http_init();

    /* 7. Game catalog (loads persisted data from storage) */
    catalog_init();

    /* 8. Kick off the home screen */
    k_screens[SCREEN_HOME].init();

    LOGI(APP_NAME " v" APP_VERSION " started");

    /* ── Main loop ─────────────────────────────────────────────────────── */
    while (g_state.running) {

        /* Handle queued screen transition */
        if (s_pending_screen >= 0 && s_pending_screen < SCREEN_COUNT) {
            do_screen_transition((AppScreen)s_pending_screen);
            s_pending_screen = -1;
        }

        /* Poll input */
        pad_poll();

        /* Update active screen */
        if (k_screens[g_state.screen].update)
            k_screens[g_state.screen].update();

        /* Render active screen */
        ui_begin_frame();
        if (k_screens[g_state.screen].render)
            k_screens[g_state.screen].render();
        ui_end_frame();
    }

    /* ── Shutdown ──────────────────────────────────────────────────────── */
    LOGI("Shutting down...");
    catalog_shutdown();
    http_shutdown();
    ui_shutdown();
    config_save();
    SifExitRpc();

    return 0;
}
