#include "hid.h"

#include <string.h>

#include "board.h"
#include "log.h"

#include "pico/stdlib.h"
#include "tusb.h"

#define TAG "hid"

#define QUEUE_LEN 128           /* power of two */
#define MIN_GAP_US 1500         /* keeps consecutive reports distinguishable */

typedef struct {
    uint8_t  report_id;
    uint8_t  len;
    uint8_t  data[8];
    uint16_t delay_ms;          /* wait this long *before* sending           */
} entry_t;

static entry_t  s_queue[QUEUE_LEN];
static uint16_t s_head, s_tail;
static uint32_t s_dropped;
static absolute_time_t s_next_send;

/* Mirrors whatever is currently pressed, so hp_hid_release_all() can let go
 * of exactly what is held rather than guessing. */
static uint8_t s_held_mods;
static uint8_t s_held_keys[HP_STEP_KEYS];

void hp_hid_init(void) {
    s_head = s_tail = 0;
    s_dropped = 0;
    s_next_send = get_absolute_time();
    memset(s_held_keys, 0, sizeof s_held_keys);
    s_held_mods = 0;
}

bool hp_hid_ready(void) { return tud_mounted() && tud_hid_ready(); }

uint16_t hp_hid_queued(void)  { return (uint16_t)(s_head - s_tail); }
uint32_t hp_hid_dropped(void) { return s_dropped; }

static bool push(uint8_t report_id, const void *data, uint8_t len, uint16_t delay_ms) {
    if ((uint16_t)(s_head - s_tail) >= QUEUE_LEN) {
        s_dropped++;
        HP_LOGW(TAG, "report queue full, dropping (total %lu)", (unsigned long)s_dropped);
        return false;
    }
    entry_t *e = &s_queue[s_head & (QUEUE_LEN - 1)];
    e->report_id = report_id;
    e->len = len;
    e->delay_ms = delay_ms;
    memset(e->data, 0, sizeof e->data);
    if (data && len) memcpy(e->data, data, len);
    s_head++;
    return true;
}

/* --- keyboard ----------------------------------------------------------- */

/* Boot-protocol keyboard report: modifier, reserved, then six key slots. */
static void fill_keyboard(uint8_t *out, uint8_t modifiers, const uint8_t keys[HP_STEP_KEYS]) {
    memset(out, 0, 8);
    out[0] = modifiers;
    if (keys) {
        for (int i = 0; i < HP_STEP_KEYS; i++) out[2 + i] = keys[i];
    }
}

bool hp_hid_key(uint8_t modifiers, const uint8_t keys[HP_STEP_KEYS]) {
    uint8_t report[8];

    /* Modifiers land in their own report first: applications that watch for
     * Ctrl going down before the letter arrives (Office, Adobe) miss the
     * shortcut otherwise. */
    if (modifiers) {
        fill_keyboard(report, modifiers, NULL);
        if (!push(HP_REPORT_ID_KEYBOARD, report, 8, 0)) return false;
    }

    fill_keyboard(report, modifiers, keys);
    if (!push(HP_REPORT_ID_KEYBOARD, report, 8, 0)) return false;

    fill_keyboard(report, 0, NULL);
    return push(HP_REPORT_ID_KEYBOARD, report, 8, 0);
}

bool hp_hid_key_hold(uint8_t modifiers, const uint8_t keys[HP_STEP_KEYS]) {
    s_held_mods |= modifiers;
    if (keys) {
        for (int i = 0; i < HP_STEP_KEYS; i++) {
            if (!keys[i]) continue;
            for (int j = 0; j < HP_STEP_KEYS; j++) {
                if (!s_held_keys[j]) { s_held_keys[j] = keys[i]; break; }
                if (s_held_keys[j] == keys[i]) break;
            }
        }
    }
    uint8_t report[8];
    fill_keyboard(report, s_held_mods, s_held_keys);
    return push(HP_REPORT_ID_KEYBOARD, report, 8, 0);
}

bool hp_hid_release_all(void) {
    s_held_mods = 0;
    memset(s_held_keys, 0, sizeof s_held_keys);
    uint8_t report[8];
    fill_keyboard(report, 0, NULL);
    return push(HP_REPORT_ID_KEYBOARD, report, 8, 0);
}

/* --- consumer ----------------------------------------------------------- */

bool hp_hid_consumer(uint16_t usage) {
    uint16_t down = usage, up = 0;
    if (!push(HP_REPORT_ID_CONSUMER, &down, 2, 0)) return false;
    return push(HP_REPORT_ID_CONSUMER, &up, 2, 0);
}

/* --- mouse -------------------------------------------------------------- */

bool hp_hid_mouse(uint8_t modifiers, uint8_t buttons, int8_t dx, int8_t dy,
                  int8_t wheel, int8_t pan) {
    uint8_t kb[8];

    /* Ctrl+scroll (zoom) only works if the modifier is already down when the
     * wheel event arrives, so it gets its own report before and after. */
    if (modifiers) {
        fill_keyboard(kb, s_held_mods | modifiers, s_held_keys);
        if (!push(HP_REPORT_ID_KEYBOARD, kb, 8, 0)) return false;
    }

    uint8_t report[5] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel, (uint8_t)pan };
    if (!push(HP_REPORT_ID_MOUSE, report, 5, 0)) return false;

    if (buttons) {
        uint8_t up[5] = { 0, 0, 0, 0, 0 };
        if (!push(HP_REPORT_ID_MOUSE, up, 5, 0)) return false;
    }

    if (modifiers) {
        fill_keyboard(kb, s_held_mods, s_held_keys);
        if (!push(HP_REPORT_ID_KEYBOARD, kb, 8, 0)) return false;
    }
    return true;
}

/* --- text --------------------------------------------------------------- */

bool hp_hid_text(const char *text) {
    for (const char *p = text; *p; p++) {
        uint8_t key, mod;
        if (!hp_char_to_key(*p, &key, &mod)) {
            HP_LOGW(TAG, "cannot type '%c' on a US layout", *p);
            continue;
        }
        uint8_t keys[HP_STEP_KEYS] = { key, 0, 0 };
        if (!hp_hid_key(mod, keys)) return false;
    }
    return true;
}

bool hp_hid_delay(uint16_t ms) {
    /* A zero-length report carries the pause; the sender skips transmitting
     * it and just honours the delay. */
    return push(0, NULL, 0, ms);
}

void hp_hid_flush(void) {
    s_tail = s_head;
    s_held_mods = 0;
    memset(s_held_keys, 0, sizeof s_held_keys);

    if (hp_hid_ready()) {
        uint8_t report[8];
        fill_keyboard(report, 0, NULL);
        tud_hid_report(HP_REPORT_ID_KEYBOARD, report, 8);
    }
}

/* --- the sender --------------------------------------------------------- */

void hp_hid_task(void) {
    if (s_head == s_tail) return;
    if (absolute_time_diff_us(get_absolute_time(), s_next_send) > 0) return;

    if (!hp_hid_ready()) {
        /* Not mounted, or the previous report has not been collected yet.
         * Leave everything queued and try again next time round. */
        return;
    }

    entry_t *e = &s_queue[s_tail & (QUEUE_LEN - 1)];

    if (e->delay_ms) {
        s_next_send = make_timeout_time_ms(e->delay_ms);
        s_tail++;
        return;
    }

    if (!tud_hid_report(e->report_id, e->data, e->len)) {
        /* Genuinely busy: keep the entry and retry rather than losing it. */
        return;
    }

    s_tail++;
    s_next_send = make_timeout_time_us(MIN_GAP_US);
}

/* --- TinyUSB callbacks --------------------------------------------------- */

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize) {
    /* The host sets keyboard LEDs this way; we have none, so it is ignored. */
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
