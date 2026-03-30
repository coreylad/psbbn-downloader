#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h — persistent application settings
 *
 * Settings are stored as a simple key=value text file on the first
 * available storage device:
 *   mass:/PSBBN/psbbn-dl.cfg   (USB mass storage)
 *   mc0:/PSBBN/psbbn-dl.cfg    (Memory card)
 *
 * All values have sane defaults so the app works with no config file.
 */

#define CFG_PATH_USB   "mass:/PSBBN/psbbn-dl.cfg"
#define CFG_PATH_MC    "mc0:/PSBBN/psbbn-dl.cfg"

typedef struct {
    /* ── Network ───────────────────────────────────────────────── */
    int  use_dhcp;               /* 1 = DHCP (default), 0 = static  */
    char static_ip[16];
    char static_nm[16];
    char static_gw[16];
    char static_dns[16];

    /* HTTP proxy (optional, for TLS termination) */
    char proxy_host[64];
    int  proxy_port;             /* 0 = disabled                     */

    /* ── Storage ───────────────────────────────────────────────── */
    char storage_path[128];      /* where ISOs are saved             */

    /* ── Display ───────────────────────────────────────────────── */
    int  pal_mode;               /* 0 = NTSC, 1 = PAL                */

    /* ── Internal (not persisted) ─────────────────────────────── */
    char cfg_file_path[128];     /* resolved path used for save      */
} AppConfig;

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Initialise with defaults, then load from storage if a file exists. */
void config_init(void);

/* Get a pointer to the global config (always valid after config_init). */
AppConfig *config_get(void);

/* Write current settings to storage. */
void config_save(void);

#endif /* CONFIG_H */
