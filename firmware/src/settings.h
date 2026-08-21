/* settings.h - device-wide configuration, persisted to /config.txt.
 *
 * Everything here is reachable three ways - the on-device menu, the desktop
 * configurator over USB, and by editing the file - and all three funnel
 * through the same key/value functions so they can never drift apart.
 */
#ifndef HP_SETTINGS_H
#define HP_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* display */
    uint8_t  contrast;
    bool     flip;
    uint16_t screensaver_sec;      /* 0 disables the screensaver */

    /* LED ring */
    hp_led_mode_t led_mode;
    uint8_t  led_brightness;
    uint32_t led_primary;
    uint32_t led_secondary;

    /* haptics */
    float    haptic_strength;
    uint16_t haptic_detents;
    bool     encoder_invert;

    /* system */
    uint8_t  log_level;
    char     last_profile[HP_PATH_LEN];
} hp_settings_t;

void hp_settings_defaults(void);
bool hp_settings_load(void);
bool hp_settings_save(void);

/* Pushes the current values into the display, LED and haptic subsystems.
 * Safe to call as often as you like. */
void hp_settings_apply(void);

/* Marks the settings dirty; hp_settings_task() writes them out once the user
 * has stopped fiddling, so turning a menu value does not hammer the card. */
void hp_settings_touch(void);
void hp_settings_task(void);

hp_settings_t *hp_settings(void);

/* Shared by the file parser, the menu and the USB protocol. */
bool   hp_settings_set(const char *key, const char *value);
size_t hp_settings_serialize(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* HP_SETTINGS_H */
