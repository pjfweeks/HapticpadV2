#include "wheel.h"

#include <stdlib.h>

#include "hid.h"
#include "ipc.h"
#include "log.h"
#include "macro.h"
#include "profile.h"
#include "ui.h"

#define TAG "wheel"

/* Emitting every pending tick in one pass would let a hard flick monopolise
 * the report queue, so we drain a bounded number per iteration and keep the
 * rest for the next one. */
#define MAX_EMIT_PER_TASK 8

static int32_t  s_pending;          /* signed detents waiting to be sent */
static uint32_t s_cw, s_ccw;

void hp_wheel_init(void) { s_pending = 0; s_cw = s_ccw = 0; }

uint32_t hp_wheel_ticks_cw(void)  { return s_cw; }
uint32_t hp_wheel_ticks_ccw(void) { return s_ccw; }

void hp_wheel_task(void) {
    int8_t dir;
    while (hp_ipc_tick_pop(&dir)) {
        if (dir > 0) s_cw++; else s_ccw++;

        /* While a menu is open the wheel drives the menu and nothing reaches
         * the host - the same knob, a different meaning. */
        if (hp_ui_wheel(dir)) continue;

        s_pending += dir;
    }

    if (s_pending == 0) return;
    if (hp_hid_queued() > HP_HID_FEED_LIMIT) return;

    const hp_profile_t *p = hp_profile_current();
    int dirsign = s_pending > 0 ? 1 : -1;
    const hp_step_t *step = dirsign > 0 ? &p->wheel_cw : &p->wheel_ccw;

    if (step->type == HP_STEP_NONE) { s_pending = 0; return; }

    int32_t count = s_pending > 0 ? s_pending : -s_pending;

    /* Scroll and pan are the common case, and they carry a magnitude - so a
     * fast spin becomes one report of N units instead of N reports.  That is
     * both smoother on screen and far kinder to the queue. */
    if (step->type == HP_STEP_MOUSE && (step->mouse_wheel || step->mouse_pan)) {
        int32_t units = count > 100 ? 100 : count;
        int8_t wheel = (int8_t)(step->mouse_wheel * units);
        int8_t pan   = (int8_t)(step->mouse_pan * units);
        if (hp_hid_mouse(step->modifiers, 0, 0, 0, wheel, pan)) {
            s_pending -= dirsign * units;
        }
        return;
    }

    int32_t emit = count > MAX_EMIT_PER_TASK ? MAX_EMIT_PER_TASK : count;
    for (int32_t i = 0; i < emit; i++) {
        if (!hp_macro_run_step(step)) break;
        s_pending -= dirsign;
    }
}
