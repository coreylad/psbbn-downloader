# PSBBN Downloader — PS2 Homebrew Build System
# Requires ps2dev toolchain + ps2sdk + gsKit (all present in ps2dev/ps2dev:latest)

PS2DEV  ?= /usr/local/ps2dev
PS2SDK  ?= $(PS2DEV)/ps2sdk
GSKIT   ?= $(PS2DEV)/gsKit

EE_BIN  = psbbn-downloader.elf

# ── Source files ────────────────────────────────────────────────────────────
EE_SRCS := \
    src/main.c \
    src/ui/ui.c \
    src/ui/home_screen.c \
    src/ui/genre_screen.c \
    src/ui/detail_screen.c \
    src/ui/download_screen.c \
    src/ui/search_screen.c \
    src/net/http.c \
    src/net/archive.c \
    src/catalog/catalog.c \
    src/util/json.c \
    src/input/pad.c \
    src/util/config.c

# ── Include paths ────────────────────────────────────────────────────────────
EE_INCS := \
    -I$(PS2SDK)/ee/include \
    -I$(PS2SDK)/common/include \
    -I$(PS2SDK)/ports/include \
    -I$(GSKIT)/include \
    -Isrc

# ── Compiler flags ───────────────────────────────────────────────────────────
EE_CFLAGS := -O2 -G0 -Wall -Wextra -Wno-unused-parameter $(EE_INCS)

# ── Linker ───────────────────────────────────────────────────────────────────
EE_LDFLAGS := \
    -L$(PS2SDK)/ee/lib \
    -L$(PS2SDK)/ports/lib \
    -L$(GSKIT)/lib

EE_LIBS := \
    -lgsKit \
    -lgsFont \
    -ldmaKit \
    -lpad \
    -lps2ip \
    -lnetman \
    -lfileXio \
    -lpatches \
    -lkernel \
    -lc \
    -lm

# ── Embedded IRX modules ─────────────────────────────────────────────────────
# These are needed for standalone (non-PSBBN) operation.
# Under PSBBN the platform has already loaded netman/smap/ps2ip.
IRX_DIR      := $(PS2SDK)/iop/irx
IRX_OBJ_DIR  := obj/irx

IRX_MODULES  := ps2ip netman smap usbd usbhdfsd

IRX_OBJS     := $(patsubst %, $(IRX_OBJ_DIR)/%_irx.o, $(IRX_MODULES))

EE_OBJS      += $(IRX_OBJS)

# ── Vendored third-party single-file sources (fetched by `make fetch-deps`) ──
TJPGD_URL := https://raw.githubusercontent.com/ms-rtos/tjpgd/master/src/tjpgd/src/tjpgd.c
TJPGD_H   := https://raw.githubusercontent.com/ms-rtos/tjpgd/master/src/tjpgd/src/tjpgd.h

.PHONY: all clean fetch-deps

all: fetch-deps $(IRX_OBJ_DIR) $(EE_BIN)

# ── Fetch vendored deps (idempotent) ─────────────────────────────────────────
fetch-deps:
	@if [ ! -f src/util/tjpgd.c ]; then \
	    echo "[deps] Downloading TJpgDec..."; \
	    curl -fsSL "$(TJPGD_URL)" -o src/util/tjpgd.c; \
	    curl -fsSL "$(TJPGD_H)"   -o src/util/tjpgd.h; \
	    echo "[deps] TJpgDec downloaded."; \
	else \
	    echo "[deps] TJpgDec already present."; \
	fi

# ── Embed IRX binaries as linkable EE objects ─────────────────────────────────
$(IRX_OBJ_DIR):
	mkdir -p $(IRX_OBJ_DIR)

$(IRX_OBJ_DIR)/%_irx.o: $(IRX_DIR)/%.irx | $(IRX_OBJ_DIR)
	$(EE_OBJCOPY) -I binary -O elf32-littlemips \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    $< $@

# ── Extra SRCS added after optional tjpgd fetch ───────────────────────────────
EE_SRCS += src/util/tjpgd.c

# ── Build rules (ps2sdk standard) ─────────────────────────────────────────────
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

clean:
	rm -f $(EE_BIN) $(EE_BIN:.elf=.map)
	rm -rf obj/ asm/
