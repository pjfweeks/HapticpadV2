/* led.h - WS2812 ring effects, driven from PIO.
 *
 * hp_led_task() is cheap and non-blocking: it recomputes the ring at a fixed
 * frame rate and hands the pixels to the state machine, which clocks them out
 * on its own.  Nothing here ever disables interrupts.
 */
#ifndef HP_LED_H
#define HP_LED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_LED_OFF = 0,
    HP_LED_SOLID,       /* primary colour, steady                        */
    HP_LED_RAINBOW,     /* hue sweep around the ring                     */
    HP_LED_BREATH,      /* primary colour, slow fade in and out          */
    HP_LED_BANDS,       /* primary/secondary split, rotating             */
    HP_LED_HALO,        /* a bright arc that follows the wheel position  */
    HP_LED_REACTIVE,    /* brightness tracks how fast the wheel is spun  */
    HP_LED_PROFILE,     /* the active profile's own colour               */
    HP_LED_MODE_COUNT
} hp_led_mode_t;

void hp_led_init(void);
void hp_led_task(void);          /* call every main-loop iteration */

void hp_led_set_mode(hp_led_mode_t mode);
hp_led_mode_t hp_led_mode(void);
const char *hp_led_mode_name(hp_led_mode_t mode);
bool hp_led_mode_from_name(const char *name, hp_led_mode_t *out);

void hp_led_set_brightness(uint8_t brightness);
uint8_t hp_led_brightness(void);

void hp_led_set_primary(uint32_t rgb);
void hp_led_set_secondary(uint32_t rgb);
/* The profile colour overrides `primary` while HP_LED_PROFILE is selected;
 * 0 means "the profile did not specify one". */
void hp_led_set_profile_color(uint32_t rgb);

/* Blanks the ring without losing the configured mode (screensaver). */
void hp_led_set_suspended(bool suspended);

#ifdef __cplusplus
}
#endif

#endif /* HP_LED_H */
