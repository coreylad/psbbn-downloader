# PSBBN Downloader

A PS2 homebrew application that runs natively on the **PlayStation BB Navigator (PSBBN)** platform and provides a slick UI for browsing, discovering, and downloading PS2 game ISOs directly from **archive.org** — with cover art, genre segregation, a virtual keyboard search, and a full download manager with resume support.

---

## Features

| Feature | Detail |
|---|---|
| **Genre grid** | 14 genre categories (Action, RPG, Sports, Racing, Shooter, Fighting, Adventure, Puzzle, Sim, Strategy, Horror, Platform, Music, Other) each with a distinct colour accent |
| **Game browser** | Scrollable list with cover-art carousel, file sizes, download counts, sort by name or popularity |
| **Cover art** | JPEG art fetched from `archive.org/services/img/{id}`, decoded on-PS2 via TJpgDec, cached in VRAM |
| **Game detail** | Full-page view with scrollable description, size, genre badge, and two-press download confirm |
| **Virtual keyboard** | 11×4 D-pad-navigated keyboard with backspace, space, and instant search submission |
| **Live search** | Free-text search against the archive.org Advanced Search API with real-time results |
| **Download manager** | Queue, pause/resume, cancel; HTTP range requests for resumable multi-gigabyte ISO transfers; write directly to USB/HDD |
| **PSBBN compatible** | Detects if PSBBN has already initialised the network stack and skips IRX loading to avoid conflicts |
| **Configurable** | `psbbn-dl.cfg` on USB or memory card: DHCP/static IP, HTTP proxy (for TLS offload), storage path, PAL/NTSC |

---

## Requirements

### Hardware
- PlayStation 2 with **network adapter** (expansion bay model SCPH-10350 or PCMCIA for slim)
- USB hard drive or HDD bay for saving ISOs (USB is `mass:/`, HDD is `hdd0:`)
- Optional: local HTTP proxy (Raspberry Pi, router) for HTTPS → HTTP bridging

### Software
- [FreeMCBoot](https://github.com/AKuHAK/FreeMCBoot-Installer) or [OpenPS2Loader](https://github.com/ps2homebrew/Open-PS2-Loader) to launch the ELF
- PSBBN environment (optional — the app boots standalone too)

---

## Building

### Using GitHub Actions (recommended)
Every push to `main` or `develop` triggers a build via the official `ps2dev/ps2dev:latest` Docker image. Download the `.elf` from the **Actions → Artifacts** tab.

### Local build
```bash
# Pull the ps2dev Docker image
docker pull ps2dev/ps2dev:latest

# Build inside the container
docker run --rm -v "$PWD:/src" -w /src ps2dev/ps2dev:latest make all -j4
```

The output is `psbbn-downloader.elf`.

---

## Installation

1. Copy `psbbn-downloader.elf` to your USB drive or memory card.
2. Launch it from FreeMCBoot / OPL / PSBBN app launcher.
3. On first run, the app uses DHCP automatically. Create `mass:/PSBBN/psbbn-dl.cfg` to override:

```ini
# PSBBN Downloader configuration
use_dhcp=1
storage_path=mass:/PS2ISO
proxy_host=          # optional: IP of your local HTTP proxy
proxy_port=8080
```

---

## Controls

| Button | Home | Genre List | Detail | Search | Downloads |
|--------|------|-----------|--------|--------|-----------|
| D-pad | Move genre cursor | Navigate list | Scroll desc | Keyboard / list | Select entry |
| **X** | Enter genre | Open detail | Download | Type key / open | Pause/Resume |
| **O** | — | Back | Back | Back | Back |
| **□** | — | Toggle sort | — | — | Cancel |
| **△** | Open Search | Quick-DL | — | Submit search | Clear done |
| **L1/R1** | — | Page up/down | — | Switch focus | — |
| **Start** | Downloads | — | — | — | — |

---

## Architecture

```
src/
├── main.c / main.h          — entry point, IRX bootstrap, screen state machine
├── ui/
│   ├── ui.c / ui.h          — gsKit rendering layer (primitives, widgets, font)
│   ├── home_screen.c        — genre tile grid with animated selection
│   ├── genre_screen.c       — game list + cover carousel
│   ├── detail_screen.c      — full game info + download confirm
│   ├── download_screen.c    — queue manager with progress bars
│   └── search_screen.c      — virtual keyboard + live results
├── net/
│   ├── http.c / http.h      — HTTP/1.1 client (range requests, chunked decode)
│   └── archive.c / archive.h— archive.org Advanced Search + cover + ISO stream
├── catalog/
│   ├── catalog.c / catalog.h— in-memory game store + download queue
│   └── genres.h             — genre enum, names, colours, search terms
├── input/
│   └── pad.c / pad.h        — libpad wrapper (edge detection, debounce)
└── util/
    ├── config.c / config.h  — persistent settings (key=value file)
    ├── json.c / json.h      — cursor-based JSON parser (no malloc, no tree)
    ├── log.h                — LOGD / LOGI / LOGW / LOGE macros
    └── tjpgd.c / tjpgd.h   — TJpgDec (fetched at build time, public domain)
```

---

## HTTPS / Proxy note

PS2's ps2ip (lwIP) has no TLS stack.  
If archive.org redirects HTTP → HTTPS in your region, run a simple local proxy:

```bash
# nginx snippet (on a Pi or router running Linux)
location /archive/ {
    proxy_pass https://archive.org/;
    proxy_ssl_server_name on;
}
```

Then set `proxy_host=<pi-ip>` and `proxy_port=80` in `psbbn-dl.cfg`.

---

## License

MIT — see [LICENSE](LICENSE).

