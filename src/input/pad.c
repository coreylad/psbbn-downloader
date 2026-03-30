/*
 * pad.c — PS2 controller input using libpad
 *
 * Provides edge-detection (pad_pressed) so screens can react to a single
 * button tap without registering multiple activations per hold.
 */

#include <string.h>
#include <kernel.h>
#include <libpad.h>
#include "pad.h"
#include "../util/log.h"

/* DMA buffer required by libpad (must be 256-byte aligned) */
static uint8_t s_pad_buf[2][256] __attribute__((aligned(256)));

static uint16_t s_prev_buttons = 0;
static uint16_t s_curr_buttons = 0;
static int      s_ready        = 0;

void pad_init(void)
{
    padInit(0);

    if (padPortOpen(0, 0, s_pad_buf[0]) != 1) {
        LOGE("padPortOpen port 0 failed");
        return;
    }
    /* Wait until pad is ready */
    int state, lastpad = -1;
    do {
        state = padGetState(0, 0);
        if (state != lastpad) {
            LOGD("pad state: %d", state);
            lastpad = state;
        }
    } while (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1);

    s_ready = 1;
    LOGI("Controller ready");
}

void pad_poll(void)
{
    if (!s_ready) return;

    struct padButtonStatus btns;
    int ret = padRead(0, 0, &btns);
    if (ret != 0) {
        /* libpad: button bits are ACTIVE-LOW; invert to get pressed mask */
        s_prev_buttons = s_curr_buttons;
        s_curr_buttons = (uint16_t)(~btns.btns & 0xFFFF);
    }
}

int pad_pressed(uint16_t button)
{
    /* Edge: newly pressed this frame (was 0, now 1) */
    return ((s_curr_buttons & button) && !(s_prev_buttons & button)) ? 1 : 0;
}

int pad_held(uint16_t button)
{
    return (s_curr_buttons & button) ? 1 : 0;
}

uint16_t pad_raw(void)
{
    return s_curr_buttons;
}
