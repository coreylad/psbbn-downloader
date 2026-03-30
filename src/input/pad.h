#ifndef PAD_H
#define PAD_H

#include <stdint.h>

/*
 * pad.h — controller input abstraction with single-frame edge detection
 * and configurable auto-repeat.
 *
 * Uses PS2 libpad under the hood.
 *
 * Button constants match libpad bit masks so existing code can compare
 * directly.
 */

/* ── Button bitmasks (match libpad PAD_* definitions) ───────────────────── */
#define PAD_SELECT    0x0001
#define PAD_L3        0x0002
#define PAD_R3        0x0004
#define PAD_START     0x0008
#define PAD_UP        0x0010
#define PAD_RIGHT     0x0020
#define PAD_DOWN      0x0040
#define PAD_LEFT      0x0080
#define PAD_L2        0x0100
#define PAD_R2        0x0200
#define PAD_L1        0x0400
#define PAD_R1        0x0800
#define PAD_TRIANGLE  0x1000
#define PAD_CIRCLE    0x2000
#define PAD_CROSS     0x4000
#define PAD_SQUARE    0x8000

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Initialise libpad; call once at startup. */
void pad_init(void);

/* Poll the hardware; call once per frame before checking button state. */
void pad_poll(void);

/* Returns 1 on the frame the button was first pressed (rising edge). */
int  pad_pressed(uint16_t button);

/* Returns 1 while the button is held down. */
int  pad_held(uint16_t button);

/* Returns current raw button word (bitmask of all active buttons). */
uint16_t pad_raw(void);

#endif /* PAD_H */
