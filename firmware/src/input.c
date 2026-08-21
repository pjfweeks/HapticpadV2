#include "input.h"

#include <string.h>

#include "log.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define TAG "btn"

#define LONG_PRESS_MS 700

static const uint8_t k_pins[HP_BTN_COUNT] = HP_BTN_PINS;

typedef struct {
    bool     stable;        /* debounced level: true = pressed        */
    bool     candidate;     /* level we are currently timing          */
    uint32_t changed_us;    /* when the candidate level first appeared */
    uint32_t down_ms;       /* when the press was accepted            */
    uint32_t next_repeat_ms;
    bool     long_sent;
} btn_state_t;

static btn_state_t s_state[HP_BTN_COUNT];
static hp_input_cb s_cb;
static absolute_time_t s_next_scan;

void hp_input_init(void) {
    for (int i = 0; i < HP_BTN_COUNT; i++) {
        gpio_init(k_pins[i]);
        gpio_set_dir(k_pins[i], GPIO_IN);
        gpio_pull_up(k_pins[i]);         /* switches pull the pin to ground */
        memset(&s_state[i], 0, sizeof s_state[i]);
    }
    s_next_scan = get_absolute_time();
    HP_LOGI(TAG, "%d buttons initialised", HP_BTN_COUNT);
}

void hp_input_set_handler(hp_input_cb cb) { s_cb = cb; }

bool hp_input_is_down(int index) {
    return (index >= 0 && index < HP_BTN_COUNT) && s_state[index].stable;
}

uint32_t hp_input_held_ms(int index) {
    if (!hp_input_is_down(index)) return 0;
    return to_ms_since_boot(get_absolute_time()) - s_state[index].down_ms;
}

static void emit(int index, hp_btn_event_t event) {
    if (s_cb) s_cb(index, event);
}

void hp_input_task(void) {
    if (absolute_time_diff_us(get_absolute_time(), s_next_scan) > 0) return;
    s_next_scan = make_timeout_time_us(HP_BTN_SCAN_US);

    uint32_t now_us = time_us_32();
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < HP_BTN_COUNT; i++) {
        btn_state_t *b = &s_state[i];
        bool raw = !gpio_get(k_pins[i]);          /* active low */

        if (raw != b->candidate) {
            /* Level moved: restart the settling window.  Nothing is reported
             * until it has held still for the full debounce time. */
            b->candidate = raw;
            b->changed_us = now_us;
            continue;
        }

        if (b->candidate != b->stable && (now_us - b->changed_us) >= HP_BTN_DEBOUNCE_US) {
            b->stable = b->candidate;
            if (b->stable) {
                b->down_ms = now_ms;
                b->next_repeat_ms = now_ms + HP_BTN_REPEAT_MS;
                b->long_sent = false;
                emit(i, HP_BTN_DOWN);
            } else {
                emit(i, HP_BTN_UP);
            }
            continue;
        }

        if (!b->stable) continue;

        if (!b->long_sent && now_ms - b->down_ms >= LONG_PRESS_MS) {
            b->long_sent = true;
            emit(i, HP_BTN_LONG);
        }
        if ((int32_t)(now_ms - b->next_repeat_ms) >= 0) {
            b->next_repeat_ms = now_ms + HP_BTN_REPEAT_RATE_MS;
            emit(i, HP_BTN_REPEAT);
        }
    }
}
