#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ini.h"
#include "ipc.h"
#include "log.h"
#include "encoder.h"
#include "profile.h"
#include "ssd1309.h"
#include "storage.h"

#define TAG "cfg"

#define CONFIG_PATH  "/config.txt"
#define SAVE_DELAY_MS 2000

static hp_settings_t s_cfg;
static bool  s_dirty;
static uint32_t s_dirty_at;

hp_settings_t *hp_settings(void) { return &s_cfg; }

void hp_settings_defaults(void) {
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.contrast        = 223;
    s_cfg.flip            = HP_DISP_FLIP;
    s_cfg.screensaver_sec = 120;
    s_cfg.led_mode        = HP_LED_RAINBOW;
    s_cfg.led_brightness  = 128;
    s_cfg.led_primary     = 0x00FF00;
    s_cfg.led_secondary   = 0x0000FF;
    s_cfg.haptic_strength = 1.0f;
    s_cfg.haptic_detents  = 24;
    s_cfg.encoder_invert  = false;
    s_cfg.log_level       = HP_LOG_INFO;
}

/* --- key/value ---------------------------------------------------------- */

bool hp_settings_set(const char *key, const char *value) {
    if (!key || !value) return false;

    if (!strcmp(key, "contrast"))        { s_cfg.contrast = (uint8_t)hp_ini_int(value, 223); }
    else if (!strcmp(key, "display_flip") || !strcmp(key, "flip"))
                                         { s_cfg.flip = hp_ini_bool(value, HP_DISP_FLIP); }
    else if (!strcmp(key, "screensaver_sec") || !strcmp(key, "screensaver"))
                                         { s_cfg.screensaver_sec = (uint16_t)hp_ini_int(value, 120); }
    else if (!strcmp(key, "led_mode")) {
        hp_led_mode_t m;
        if (!hp_led_mode_from_name(value, &m)) return false;
        s_cfg.led_mode = m;
    }
    else if (!strcmp(key, "led_brightness")) {
        int32_t v = hp_ini_int(value, 128);
        s_cfg.led_brightness = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    else if (!strcmp(key, "led_primary"))   { s_cfg.led_primary = hp_ini_color(value, 0x00FF00); }
    else if (!strcmp(key, "led_secondary")) { s_cfg.led_secondary = hp_ini_color(value, 0x0000FF); }
    else if (!strcmp(key, "haptic_strength")) {
        float v = hp_ini_float(value, 1.0f);
        s_cfg.haptic_strength = v < 0.2f ? 0.2f : (v > 5.0f ? 5.0f : v);
    }
    else if (!strcmp(key, "haptic_detents")) {
        int32_t v = hp_ini_int(value, 24);
        s_cfg.haptic_detents = (uint16_t)(v < 1 ? 1 : (v > 200 ? 200 : v));
    }
    else if (!strcmp(key, "encoder_invert")) { s_cfg.encoder_invert = hp_ini_bool(value, false); }
    else if (!strcmp(key, "log_level")) {
        if      (!strcasecmp(value, "error")) s_cfg.log_level = HP_LOG_ERROR;
        else if (!strcasecmp(value, "warn"))  s_cfg.log_level = HP_LOG_WARN;
        else if (!strcasecmp(value, "info"))  s_cfg.log_level = HP_LOG_INFO;
        else if (!strcasecmp(value, "debug")) s_cfg.log_level = HP_LOG_DEBUG;
        else return false;
    }
    else if (!strcmp(key, "last_profile")) {
        hp_ini_copy(s_cfg.last_profile, sizeof s_cfg.last_profile, value);
    }
    else {
        return false;
    }

    return true;
}

size_t hp_settings_serialize(char *buf, size_t buf_size) {
    static const char *const level_names[] = { "error", "warn", "info", "debug" };
    const char *level = level_names[s_cfg.log_level <= HP_LOG_DEBUG ? s_cfg.log_level : HP_LOG_INFO];

    int n = snprintf(buf, buf_size,
        "# HapticPad configuration\n"
        "# Written by the firmware; safe to edit by hand or from the configurator.\n"
        "\n"
        "# --- display ---\n"
        "contrast=%u\n"
        "display_flip=%d\n"
        "screensaver_sec=%u\n"
        "\n"
        "# --- LED ring ---\n"
        "# led_mode: off solid rainbow breath bands halo reactive profile\n"
        "led_mode=%s\n"
        "led_brightness=%u\n"
        "led_primary=#%06lX\n"
        "led_secondary=#%06lX\n"
        "\n"
        "# --- haptics ---\n"
        "haptic_strength=%.2f\n"
        "haptic_detents=%u\n"
        "encoder_invert=%d\n"
        "\n"
        "# --- system ---\n"
        "log_level=%s\n"
        "last_profile=%s\n",
        (unsigned)s_cfg.contrast,
        (int)s_cfg.flip,
        (unsigned)s_cfg.screensaver_sec,
        hp_led_mode_name(s_cfg.led_mode),
        (unsigned)s_cfg.led_brightness,
        (unsigned long)s_cfg.led_primary,
        (unsigned long)s_cfg.led_secondary,
        (double)s_cfg.haptic_strength,
        (unsigned)s_cfg.haptic_detents,
        (int)s_cfg.encoder_invert,
        level,
        s_cfg.last_profile);

    return n < 0 ? 0 : (size_t)n;
}

/* --- persistence -------------------------------------------------------- */

static bool on_key(const char *key, const char *value, void *ctx) {
    (void)ctx;
    if (!value) return true;
    if (!hp_settings_set(key, value)) HP_LOGW(TAG, "ignoring '%s=%s'", key, value);
    return true;
}

bool hp_settings_load(void) {
    hp_settings_defaults();

    if (!hp_storage_exists(CONFIG_PATH)) {
        HP_LOGI(TAG, "no %s yet - writing defaults", CONFIG_PATH);
        hp_settings_save();
        return false;
    }
    if (!hp_ini_parse_file(CONFIG_PATH, on_key, NULL)) {
        HP_LOGE(TAG, "could not read %s: %s", CONFIG_PATH, hp_storage_last_error());
        return false;
    }

    hp_log_set_level((hp_log_level_t)s_cfg.log_level);
    HP_LOGI(TAG, "settings loaded from %s", CONFIG_PATH);
    return true;
}

bool hp_settings_save(void) {
    static char buf[HP_FILE_BUF_BYTES];
    size_t len = hp_settings_serialize(buf, sizeof buf);
    if (!len) return false;

    if (!hp_storage_write(CONFIG_PATH, buf, len)) {
        HP_LOGE(TAG, "save failed: %s", hp_storage_last_error());
        return false;
    }
    s_dirty = false;
    HP_LOGI(TAG, "settings saved");
    return true;
}

void hp_settings_touch(void) {
    s_dirty = true;
    s_dirty_at = to_ms_since_boot(get_absolute_time());
}

void hp_settings_task(void) {
    if (!s_dirty) return;
    if (to_ms_since_boot(get_absolute_time()) - s_dirty_at < SAVE_DELAY_MS) return;
    hp_settings_save();
}

/* --- applying ----------------------------------------------------------- */

void hp_settings_apply(void) {
    hp_disp_set_contrast(s_cfg.contrast);
    hp_disp_set_flip(s_cfg.flip);

    hp_log_set_level((hp_log_level_t)s_cfg.log_level);
    hp_encoder_set_invert(s_cfg.encoder_invert);

    /* The wheel's feel is the profile's choice where it states one, falling
     * back to the global setting otherwise - so a profile can be as specific
     * or as hands-off as its author wants. */
    const hp_profile_t *p = hp_profile_current();
    hp_haptic_cmd_t cmd;
    hp_ipc_cmd_get(&cmd);          /* keep calibrate_seq and the parked flag */
    cmd = (hp_haptic_cmd_t){
        .mode      = p->wheel_mode,
        .strength  = p->strength > 0.0f ? p->strength : s_cfg.haptic_strength,
        .detents   = p->detents > 0 ? p->detents : s_cfg.haptic_detents,
        .calibrate_seq = cmd.calibrate_seq,
        .enabled   = cmd.enabled,
    };
    hp_ipc_cmd_set(&cmd);

    /* The ring is configured the same way: the profile wins where it states a
     * value, the device-wide setting fills in the rest.  A profile can
     * therefore change just its colour and still follow whatever mode you
     * picked in the menu, or take over the ring completely. */
    hp_led_set_mode(p->led.has_mode ? p->led.mode : s_cfg.led_mode);
    hp_led_set_brightness(p->led.has_brightness ? p->led.brightness : s_cfg.led_brightness);
    hp_led_set_primary(p->led.has_primary ? p->led.primary : s_cfg.led_primary);
    hp_led_set_secondary(p->led.has_secondary ? p->led.secondary : s_cfg.led_secondary);

    /* HP_LED_PROFILE predates these overrides and means "use the profile's
     * own colour"; feeding it the resolved primary keeps old configs working. */
    hp_led_set_profile_color(p->led.has_primary ? p->led.primary : 0);
}
