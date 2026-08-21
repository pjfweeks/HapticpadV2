#include "profile.h"

#include <stdio.h>
#include <string.h>

#include "ini.h"
#include "log.h"
#include "storage.h"

#define TAG "prof"

#define PROFILE_DIR "/profiles"

static char  s_files[HP_MAX_PROFILES][HP_PATH_LEN];
static int   s_count;
static int   s_active = -1;
static hp_profile_t s_current;

/* --- haptic mode names -------------------------------------------------- */

static const char *const k_mode_names[HP_HAPTIC_COUNT] = {
    "off", "free", "clicky", "ratchet", "damped", "4points"
};

const char *hp_haptic_mode_name(hp_haptic_mode_t mode) {
    return mode < HP_HAPTIC_COUNT ? k_mode_names[mode] : "?";
}

bool hp_haptic_mode_from_name(const char *name, hp_haptic_mode_t *out) {
    for (int i = 0; i < HP_HAPTIC_COUNT; i++) {
        if (strcasecmp(name, k_mode_names[i]) == 0) { *out = (hp_haptic_mode_t)i; return true; }
    }
    /* Names carried over from the original XML firmware. */
    if (!strcasecmp(name, "twist"))    { *out = HP_HAPTIC_DAMPED; return true; }
    if (!strcasecmp(name, "momentum")) { *out = HP_HAPTIC_FREE;   return true; }
    if (!strcasecmp(name, "4_points")) { *out = HP_HAPTIC_4POINTS; return true; }
    return false;
}

/* --- scanning ----------------------------------------------------------- */

static bool has_extension(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

static bool collect(const hp_dirent_t *entry, void *ctx) {
    (void)ctx;
    if (entry->is_dir || s_count >= HP_MAX_PROFILES) return s_count < HP_MAX_PROFILES;
    if (!has_extension(entry->name, ".txt") && !has_extension(entry->name, ".ini")) return true;

    hp_ini_copy(s_files[s_count], HP_PATH_LEN, entry->name);
    s_count++;
    return true;
}

void hp_profile_scan(void) {
    s_count = 0;

    if (!hp_storage_ready()) {
        HP_LOGW(TAG, "no card - running with no profiles");
        return;
    }
    if (!hp_storage_exists(PROFILE_DIR)) hp_storage_mkdir(PROFILE_DIR);
    hp_storage_list(PROFILE_DIR, collect, NULL);

    /* Insertion sort: the list is at most HP_MAX_PROFILES entries and this
     * runs once, so simplicity beats cleverness. */
    for (int i = 1; i < s_count; i++) {
        char key[HP_PATH_LEN];
        memcpy(key, s_files[i], HP_PATH_LEN);
        int j = i - 1;
        while (j >= 0 && strcasecmp(s_files[j], key) > 0) {
            memcpy(s_files[j + 1], s_files[j], HP_PATH_LEN);
            j--;
        }
        memcpy(s_files[j + 1], key, HP_PATH_LEN);
    }

    HP_LOGI(TAG, "%d profile(s) in %s", s_count, PROFILE_DIR);
}

int hp_profile_count(void) { return s_count; }

const char *hp_profile_filename(int index) {
    return (index >= 0 && index < s_count) ? s_files[index] : "";
}

int hp_profile_active(void) { return s_active; }
const hp_profile_t *hp_profile_current(void) { return &s_current; }

/* --- icons -------------------------------------------------------------- */

static void load_icon(hp_button_t *btn) {
    if (!btn->icon_file[0]) return;

    /* Search order mirrors the layout people already have on their cards. */
    static const char *const dirs[] = { PROFILE_DIR "/icons/", "/icons/", "/" };

    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        char path[HP_PATH_LEN * 2];
        snprintf(path, sizeof path, "%s%s", dirs[i], btn->icon_file);

        /* Each directory is tried in turn; a miss here is ordinary, so it is
         * not worth a log line of its own. */
        size_t len = 0;
        if (!hp_storage_read(path, btn->icon, HP_ICON_BYTES, &len)) continue;
        if (len < HP_ICON_BYTES) {
            HP_LOGW(TAG, "icon %s is %u bytes, expected %d", path, (unsigned)len, HP_ICON_BYTES);
            continue;
        }

        btn->has_icon = true;
        HP_LOGD(TAG, "icon %s loaded", path);
        return;
    }

    HP_LOGW(TAG, "icon '%s' not found for label '%s'", btn->icon_file, btn->label);
}

/* --- wheel function presets --------------------------------------------- */

/* Each preset is just a pair of action strings, so a wheel function and a
 * hand-written wheel_cw= are the same thing to everything downstream. */
static bool wheel_preset(const char *func, const char **cw, const char **ccw) {
    static const struct { const char *name, *cw, *ccw; } k[] = {
        { "scroll",    "SCROLL_UP",       "SCROLL_DOWN"      },
        { "scroll_h",  "SCROLL_RIGHT",    "SCROLL_LEFT"      },
        { "scroll_x",  "SCROLL_RIGHT",    "SCROLL_LEFT"      },
        { "zoom",      "CTRL+SCROLL_UP",  "CTRL+SCROLL_DOWN" },
        { "volume",    "VOL_UP",          "VOL_DOWN"         },
        { "page",      "PGDN",            "PGUP"             },
        { "tabs",      "CTRL+TAB",        "CTRL+SHIFT+TAB"   },
        { "history",   "ALT+RIGHT",       "ALT+LEFT"         },
        { "undo_redo", "CTRL+Y",          "CTRL+Z"           },
        { "arrow",     "DOWN",            "UP"               },
        { "arrow_h",   "RIGHT",           "LEFT"             },
        { "timeline",  "RIGHT",           "LEFT"             },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (strcasecmp(func, k[i].name) == 0) { *cw = k[i].cw; *ccw = k[i].ccw; return true; }
    }
    return false;
}

/* --- parsing ------------------------------------------------------------ */

/* Splits "CTRL+P,500;ENTER" into steps.  Each segment is "action[,delay_ms]";
 * the delay applies after that step. */
static void parse_steps(const char *value, hp_step_t *steps, uint8_t *count) {
    *count = 0;

    const char *seg = value;
    while (*seg && *count < HP_MAX_STEPS) {
        const char *end = strchr(seg, ';');
        size_t seg_len = end ? (size_t)(end - seg) : strlen(seg);

        char buf[HP_TEXT_LEN + 32];
        size_t n = seg_len < sizeof buf - 1 ? seg_len : sizeof buf - 1;
        memcpy(buf, seg, n);
        buf[n] = '\0';

        /* A TEXT: payload may legitimately contain commas, so only the last
         * comma of a non-TEXT segment is treated as the delay separator. */
        uint16_t delay = 0;
        if (strncasecmp(buf, "TEXT:", 5) != 0) {
            char *comma = strrchr(buf, ',');
            if (comma) {
                *comma = '\0';
                delay = (uint16_t)hp_ini_int(comma + 1, 0);
            }
        }

        hp_step_t step;
        if (hp_action_parse(buf, &step)) {
            step.delay_ms = delay;
            steps[(*count)++] = step;
        }

        if (!end) break;
        seg = end + 1;
    }
}

static bool on_key(const char *key, const char *value, void *ctx) {
    (void)ctx;
    if (!value) return true;                  /* [section] header - ignored */

    if (!strcmp(key, "name")) {
        hp_ini_copy(s_current.name, sizeof s_current.name, value);
        return true;
    }
    /* Spellings that have appeared in real profiles on real cards: `color` is
     * what the original firmware used, `led_color` is what people write by
     * hand.  They all mean the primary colour. */
    if (!strcmp(key, "color") || !strcmp(key, "colour") ||
        !strcmp(key, "led_primary") || !strcmp(key, "led_color") ||
        !strcmp(key, "led_colour")) {
        s_current.led.primary = hp_ini_color(value, 0);
        s_current.led.has_primary = true;
        return true;
    }
    if (!strcmp(key, "led_secondary")) {
        s_current.led.secondary = hp_ini_color(value, 0);
        s_current.led.has_secondary = true;
        return true;
    }
    if (!strcmp(key, "led_mode") || !strcmp(key, "led")) {
        hp_led_mode_t mode;
        if (hp_led_mode_from_name(value, &mode)) {
            s_current.led.mode = mode;
            s_current.led.has_mode = true;
        } else {
            HP_LOGW(TAG, "unknown led_mode '%s'", value);
        }
        return true;
    }
    if (!strcmp(key, "led_brightness")) {
        int32_t v = hp_ini_int(value, 128);
        s_current.led.brightness = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        s_current.led.has_brightness = true;
        return true;
    }
    if (!strcmp(key, "wheel") || !strcmp(key, "wheel_mode")) {
        hp_haptic_mode_t m;
        if (hp_haptic_mode_from_name(value, &m)) s_current.wheel_mode = m;
        else HP_LOGW(TAG, "unknown wheel mode '%s'", value);
        return true;
    }
    if (!strcmp(key, "wheel_detents") || !strcmp(key, "detents")) {
        int32_t d = hp_ini_int(value, 24);
        s_current.detents = (uint16_t)(d < 1 ? 1 : (d > 200 ? 200 : d));
        return true;
    }
    if (!strcmp(key, "wheel_strength") || !strcmp(key, "hap_str") ||
        !strcmp(key, "haptic_strength")) {
        s_current.strength = hp_ini_float(value, 0.0f);
        return true;
    }
    if (!strcmp(key, "wheel_func")) {
        hp_ini_copy(s_current.wheel_func, sizeof s_current.wheel_func, value);
        const char *cw, *ccw;
        if (wheel_preset(value, &cw, &ccw)) {
            hp_action_parse(cw, &s_current.wheel_cw);
            hp_action_parse(ccw, &s_current.wheel_ccw);
        } else if (strcasecmp(value, "custom") != 0) {
            HP_LOGW(TAG, "unknown wheel_func '%s'", value);
        }
        return true;
    }
    if (!strcmp(key, "wheel_cw"))  { hp_action_parse(value, &s_current.wheel_cw);  return true; }
    if (!strcmp(key, "wheel_ccw")) { hp_action_parse(value, &s_current.wheel_ccw); return true; }

    int idx;
    if (hp_ini_indexed(key, "btn", "_label", &idx) && idx >= 1 && idx <= HP_MACRO_COUNT) {
        hp_ini_copy(s_current.buttons[idx - 1].label, HP_LABEL_LEN, value);
        return true;
    }
    if (hp_ini_indexed(key, "btn", "_icon", &idx) && idx >= 1 && idx <= HP_MACRO_COUNT) {
        hp_ini_copy(s_current.buttons[idx - 1].icon_file, HP_PATH_LEN, value);
        return true;
    }
    if (hp_ini_indexed(key, "btn", "_action", &idx) && idx >= 1 && idx <= HP_MACRO_COUNT) {
        hp_button_t *b = &s_current.buttons[idx - 1];
        parse_steps(value, b->steps, &b->step_count);
        return true;
    }

    HP_LOGD(TAG, "ignoring unknown key '%s'", key);
    return true;
}

/* --- loading ------------------------------------------------------------ */

static void reset_profile(void) {
    memset(&s_current, 0, sizeof s_current);
    s_current.wheel_mode = HP_HAPTIC_CLICKY;
    s_current.detents    = 24;
    snprintf(s_current.name, sizeof s_current.name, "Profile");
    for (int i = 0; i < HP_MACRO_COUNT; i++) {
        snprintf(s_current.buttons[i].label, HP_LABEL_LEN, "%d", i + 1);
    }
}

bool hp_profile_load(int index) {
    if (index < 0 || index >= s_count) {
        HP_LOGE(TAG, "profile index %d out of range (%d available)", index, s_count);
        return false;
    }

    reset_profile();
    hp_ini_copy(s_current.file, sizeof s_current.file, s_files[index]);

    char path[HP_PATH_LEN * 2];
    snprintf(path, sizeof path, "%s/%s", PROFILE_DIR, s_files[index]);

    if (!hp_ini_parse_file(path, on_key, NULL)) {
        HP_LOGE(TAG, "could not read %s: %s", path, hp_storage_last_error());
        return false;
    }

    for (int i = 0; i < HP_MACRO_COUNT; i++) load_icon(&s_current.buttons[i]);

    s_active = index;
    HP_LOGI(TAG, "loaded '%s' (%s), wheel=%s func=%s, led=%s",
            s_current.name, s_current.file,
            hp_haptic_mode_name(s_current.wheel_mode),
            s_current.wheel_func[0] ? s_current.wheel_func : "custom",
            s_current.led.has_mode ? hp_led_mode_name(s_current.led.mode) : "inherited");
    return true;
}

bool hp_profile_load_by_name(const char *filename) {
    for (int i = 0; i < s_count; i++) {
        if (strcasecmp(s_files[i], filename) == 0) return hp_profile_load(i);
    }
    HP_LOGW(TAG, "no profile named '%s'", filename);
    return false;
}

bool hp_profile_overrides_led_mode(void) { return s_current.led.has_mode; }
bool hp_profile_overrides_led_brightness(void) { return s_current.led.has_brightness; }

void hp_profile_next(void) {
    if (s_count > 0) hp_profile_load((s_active + 1) % s_count);
}

void hp_profile_prev(void) {
    if (s_count > 0) hp_profile_load((s_active - 1 + s_count) % s_count);
}
