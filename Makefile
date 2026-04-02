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
    -lgskit \
    -ldmakit \
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
IRX_ASM_DIR  := obj/irx

IRX_MODULES  := ps2ip netman smap usbd usbhdfsd

# Each IRX binary is wrapped in a tiny EE-native assembly stub so it compiles
# with the correct MIPS R5900 ABI (objcopy -O elf32-littlemips would produce
# MIPS I ABI objects that the EE linker rejects).
IRX_ASM_SRCS := $(patsubst %, $(IRX_ASM_DIR)/%_irx.S, $(IRX_MODULES))

EE_SRCS      += $(IRX_ASM_SRCS)

# ── Vendored third-party single-file sources (fetched by `make fetch-deps`) ──
TJPGD_URL := https://raw.githubusercontent.com/ms-rtos/tjpgd/master/src/tjpgd/src/tjpgd.c
TJPGD_H   := https://raw.githubusercontent.com/ms-rtos/tjpgd/master/src/tjpgd/src/tjpgd.h

# ── Extra SRCS added after optional tjpgd fetch ───────────────────────────────
EE_SRCS += src/util/tjpgd.c

# ── Derive EE_OBJS from EE_SRCS (required: Makefile.eeglobal uses EE_OBJS as-is)
EE_OBJS := $(patsubst %.c,%.o,$(filter %.c,$(EE_SRCS))) $(patsubst %.S,%.o,$(filter %.S,$(EE_SRCS)))

.PHONY: all clean fetch-deps

all: fetch-deps $(IRX_ASM_DIR) $(EE_BIN)

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

# ── Create IRX assembly output directory ─────────────────────────────────────
$(IRX_ASM_DIR):
	mkdir -p $(IRX_ASM_DIR)

# ── Generate EE-compatible assembly stubs that embed each IRX binary ─────────
# Symbols produced: <module>_irx (byte array) and <module>_irx_size (uint32)
$(IRX_ASM_DIR)/%_irx.S: $(IRX_DIR)/%.irx | $(IRX_ASM_DIR)
	@printf '\t.section .rodata\n\t.balign 64\n\t.global %s_irx\n%s_irx:\n\t.incbin "%s"\n%s_irx_end:\n\t.balign 4\n\t.global %s_irx_size\n%s_irx_size:\n\t.int %s_irx_end - %s_irx\n' \
	    '$*' '$*' '$(abspath $<)' '$*' '$*' '$*' '$*' '$*' > $@

# ── Build rules (ps2sdk standard) ─────────────────────────────────────────────
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

clean:
	rm -f $(EE_BIN) $(EE_BIN:.elf=.map)
	rm -f $(EE_OBJS)
	rm -rf obj/ asm/
