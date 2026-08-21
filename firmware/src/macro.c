#include "macro.h"

#include <string.h>

#include "hid.h"
#include "log.h"
#include "profile.h"

#define TAG "macro"

/* Feeding stops once the queue is this full, leaving headroom for the wheel
 * and the menu to stay responsive while a long macro plays out. */
#define QUEUE_HEADROOM 32

static int      s_button = -1;
static uint8_t  s_step;
static uint16_t s_text_pos;
static bool     s_active;

/* A single step submitted directly rather than from a profile button. */
static hp_step_t s_adhoc;
static bool      s_adhoc_pending;

void hp_macro_init(void) {
    s_button = -1;
    s_active = false;
    s_adhoc_pending = false;
}

bool hp_macro_running(void) { return s_active || s_adhoc_pending; }
int  hp_macro_current_button(void) { return s_button; }

void hp_macro_abort(void) {
    if (s_active || s_adhoc_pending) {
        hp_hid_flush();
        hp_hid_release_all();
    }
    s_active = false;
    s_adhoc_pending = false;
    s_button = -1;
    s_step = 0;
    s_text_pos = 0;
}

bool hp_macro_start(int button) {
    if (button < 0 || button >= HP_MACRO_COUNT) return false;

    const hp_profile_t *p = hp_profile_current();
    if (p->buttons[button].step_count == 0) {
        HP_LOGD(TAG, "button %d has no action", button + 1);
        return false;
    }

    if (s_active) hp_macro_abort();

    s_button = button;
    s_step = 0;
    s_text_pos = 0;
    s_active = true;

    HP_LOGI(TAG, "button %d -> '%s' (%d step%s)", button + 1,
            p->buttons[button].label, p->buttons[button].step_count,
            p->buttons[button].step_count == 1 ? "" : "s");

    hp_macro_task();            /* get the first report moving immediately */
    return true;
}

bool hp_macro_run_step(const hp_step_t *step) {
    if (!step || step->type == HP_STEP_NONE) return false;
    if (s_adhoc_pending) return false;      /* previous one still stuck */

    s_adhoc = *step;
    s_adhoc_pending = true;
    hp_macro_task();
    return !s_adhoc_pending;                /* false means the queue was full */
}

/* Emits one step.  Text steps are emitted a chunk at a time and report how
 * far they got, so a 64-character string cannot overrun the queue. */
static bool emit_step(const hp_step_t *step, uint16_t *text_pos) {
    switch (step->type) {
        case HP_STEP_KEY:
            return hp_hid_key(step->modifiers, step->keys);

        case HP_STEP_HOLD:
            return hp_hid_key_hold(step->modifiers, step->keys);

        case HP_STEP_RELEASE:
            return hp_hid_release_all();

        case HP_STEP_CONSUMER:
            return hp_hid_consumer(step->consumer);

        case HP_STEP_MOUSE:
            return hp_hid_mouse(step->modifiers, step->mouse_buttons,
                                step->mouse_dx, step->mouse_dy,
                                step->mouse_wheel, step->mouse_pan);

        case HP_STEP_WAIT:
            return true;        /* the delay below is the whole point */

        case HP_STEP_TEXT: {
            while (step->text[*text_pos]) {
                if (hp_hid_queued() > HP_HID_FEED_LIMIT) return false;   /* resume later */
                uint8_t key, mod;
                if (hp_char_to_key(step->text[*text_pos], &key, &mod)) {
                    uint8_t keys[HP_STEP_KEYS] = { key, 0, 0 };
                    if (!hp_hid_key(mod, keys)) return false;
                }
                (*text_pos)++;
            }
            *text_pos = 0;
            return true;
        }

        default:
            return true;
    }
}

void hp_macro_task(void) {
    if (s_adhoc_pending) {
        uint16_t pos = 0;
        if (emit_step(&s_adhoc, &pos)) {
            if (s_adhoc.delay_ms) hp_hid_delay(s_adhoc.delay_ms);
            s_adhoc_pending = false;
        }
        return;
    }

    if (!s_active) return;

    const hp_profile_t *p = hp_profile_current();
    const hp_button_t *btn = &p->buttons[s_button];

    while (s_step < btn->step_count) {
        if (hp_hid_queued() > HP_HID_FEED_LIMIT) return;    /* come back later */

        const hp_step_t *step = &btn->steps[s_step];
        if (!emit_step(step, &s_text_pos)) return;          /* queue filled up */

        if (step->delay_ms) hp_hid_delay(step->delay_ms);
        s_step++;
    }

    s_active = false;
    s_button = -1;
    s_step = 0;
}
