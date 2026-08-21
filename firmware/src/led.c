#include "led.h"

#include <string.h>

#include "board.h"
#include "ipc.h"
#include "log.h"

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "ws2812.pio.h"

#define TAG "led"
#define FRAME_US 16000u          /* ~60 fps */

static hp_led_mode_t s_mode = HP_LED_RAINBOW;
static uint8_t  s_brightness = 128;
static uint32_t s_primary   = 0x00FF00;
static uint32_t s_secondary = 0x0000FF;
static uint32_t s_profile;
static bool     s_suspended;
static uint32_t s_phase;
static absolute_time_t s_next;

static const char *const k_mode_names[HP_LED_MODE_COUNT] = {
    "off", "solid", "rainbow", "breath", "bands", "halo", "reactive", "profile"
};

/* --- colour helpers ----------------------------------------------------- */

static uint32_t scale_rgb(uint32_t rgb, uint8_t scale) {
    uint32_t r = ((rgb >> 16 & 0xFF) * scale) >> 8;
    uint32_t g = ((rgb >>  8 & 0xFF) * scale) >> 8;
    uint32_t b = ((rgb       & 0xFF) * scale) >> 8;
    return r << 16 | g << 8 | b;
}

/* Integer HSV with V fixed at full: enough for ring effects and far cheaper
 * than floating point at 60 fps. */
static uint32_t hue_to_rgb(uint8_t hue) {
    uint8_t region = hue / 43;
    uint8_t rem = (uint8_t)((hue - region * 43) * 6);
    uint8_t p = 0, q = (uint8_t)(255 - rem), t = rem;
    switch (region) {
        case 0:  return (uint32_t)255 << 16 | (uint32_t)t << 8 | p;
        case 1:  return (uint32_t)q   << 16 | (uint32_t)255 << 8 | p;
        case 2:  return (uint32_t)p   << 16 | (uint32_t)255 << 8 | t;
        case 3:  return (uint32_t)p   << 16 | (uint32_t)q << 8 | 255;
        case 4:  return (uint32_t)t   << 16 | (uint32_t)p << 8 | 255;
        default: return (uint32_t)255 << 16 | (uint32_t)p << 8 | q;
    }
}

static void put_pixel(uint32_t rgb) {
    /* WS2812 expects GRB, MSB first, in the top 24 bits of the word. */
    uint32_t grb = (rgb >> 8 & 0xFF) << 16 | (rgb >> 16 & 0xFF) << 8 | (rgb & 0xFF);
    pio_sm_put_blocking(HP_PIO_LED, HP_PIO_LED_SM, grb << 8u);
}

/* --- public ------------------------------------------------------------- */

void hp_led_init(void) {
    uint offset = pio_add_program(HP_PIO_LED, &ws2812_program);
    ws2812_program_init(HP_PIO_LED, HP_PIO_LED_SM, offset, HP_LED_PIN, 800000.0f);
    s_next = get_absolute_time();
    HP_LOGI(TAG, "%d WS2812 on GP%d via PIO", HP_LED_COUNT, HP_LED_PIN);
}

void hp_led_set_mode(hp_led_mode_t mode) {
    if (mode < HP_LED_MODE_COUNT) s_mode = mode;
}
hp_led_mode_t hp_led_mode(void) { return s_mode; }

const char *hp_led_mode_name(hp_led_mode_t mode) {
    return mode < HP_LED_MODE_COUNT ? k_mode_names[mode] : "?";
}

bool hp_led_mode_from_name(const char *name, hp_led_mode_t *out) {
    for (int i = 0; i < HP_LED_MODE_COUNT; i++) {
        if (strcasecmp(name, k_mode_names[i]) == 0) {
            *out = (hp_led_mode_t)i;
            return true;
        }
    }
    return false;
}

void hp_led_set_brightness(uint8_t b) { s_brightness = b; }
uint8_t hp_led_brightness(void)       { return s_brightness; }
void hp_led_set_primary(uint32_t rgb)   { s_primary = rgb; }
void hp_led_set_secondary(uint32_t rgb) { s_secondary = rgb; }
void hp_led_set_profile_color(uint32_t rgb) { s_profile = rgb; }
void hp_led_set_suspended(bool s)       { s_suspended = s; }

void hp_led_task(void) {
    if (absolute_time_diff_us(get_absolute_time(), s_next) > 0) return;
    s_next = delayed_by_us(get_absolute_time(), FRAME_US);
    s_phase++;

    if (s_suspended || s_mode == HP_LED_OFF) {
        for (int i = 0; i < HP_LED_COUNT; i++) put_pixel(0);
        return;
    }

    hp_haptic_state_t st;
    hp_ipc_state_get(&st);

    uint32_t base = (s_mode == HP_LED_PROFILE && s_profile) ? s_profile : s_primary;

    for (int i = 0; i < HP_LED_COUNT; i++) {
        uint32_t rgb = 0;
        switch (s_mode) {
            case HP_LED_SOLID:
            case HP_LED_PROFILE:
                rgb = base;
                break;

            case HP_LED_RAINBOW:
                rgb = hue_to_rgb((uint8_t)((s_phase * 2 + i * (256 / HP_LED_COUNT)) & 0xFF));
                break;

            case HP_LED_BREATH: {
                /* Triangle wave, floored so the ring never goes fully dark. */
                uint8_t t = (uint8_t)((s_phase * 2) & 0xFF);
                uint8_t v = t < 128 ? (uint8_t)(t * 2) : (uint8_t)((255 - t) * 2);
                rgb = scale_rgb(base, (uint8_t)(30 + (v * 225) / 255));
                break;
            }

            case HP_LED_BANDS: {
                int pos = (i + (int)(s_phase / 4)) % HP_LED_COUNT;
                rgb = (pos < HP_LED_COUNT / 2) ? base : s_secondary;
                break;
            }

            case HP_LED_HALO: {
                /* Brightest LED tracks the wheel; falls off to either side. */
                float turns = st.angle / 6.2831853f;
                int head = (int)((turns - (float)(int)turns) * HP_LED_COUNT);
                if (head < 0) head += HP_LED_COUNT;
                int d = i - head;
                if (d < 0) d += HP_LED_COUNT;
                if (d > HP_LED_COUNT / 2) d = HP_LED_COUNT - d;
                uint8_t v = d >= 4 ? 12 : (uint8_t)(255 - d * 60);
                rgb = scale_rgb(base, v);
                break;
            }

            case HP_LED_REACTIVE: {
                float v = st.velocity < 0 ? -st.velocity : st.velocity;
                if (v > 40.0f) v = 40.0f;
                uint8_t lvl = (uint8_t)(40 + (v / 40.0f) * 215.0f);
                rgb = scale_rgb(base, lvl);
                break;
            }

            default:
                break;
        }
        put_pixel(scale_rgb(rgb, s_brightness));
    }
}
