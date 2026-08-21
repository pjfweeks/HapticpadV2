#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "gfx.h"
#include "ini.h"
#include "hid.h"
#include "ipc.h"
#include "led.h"
#include "log.h"
#include "macro.h"
#include "profile.h"
#include "settings.h"
#include "ssd1309.h"
#include "system.h"
#include "storage.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#define TAG "ui"

#define REDRAW_INTERVAL_MS 100      /* upper bound; normally event-driven */
#define TOAST_MS          1400

static hp_screen_t s_screen = HP_SCREEN_BOOT;
static hp_screen_t s_screen_before_saver = HP_SCREEN_PROFILE;
static bool        s_dirty = true;
static uint32_t    s_last_activity_ms;
static uint32_t    s_last_draw_ms;

static char     s_toast[24];
static uint32_t s_toast_until;

static int  s_menu_index;
static int  s_menu_scroll;
static bool s_menu_editing;

static int  s_list_index;
static int  s_list_scroll;

/* Set when NEXT+PREV fire the menu toggle, so the releases that follow do not
 * also switch profiles. */
static bool s_combo_used[HP_BTN_COUNT];

/* ------------------------------------------------------------------ */
/* Menu model                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    MI_BACK = 0, MI_LED_MODE, MI_LED_BRIGHT, MI_HAPTIC, MI_DETENTS,
    MI_CONTRAST, MI_SCREENSAVER, MI_FLIP, MI_ENC_INVERT,
    MI_PROFILES, MI_CALIBRATE, MI_SAVE, MI_BOOTSEL, MI_REBOOT,
    MI_COUNT
} menu_id_t;

static const char *const k_menu_labels[MI_COUNT] = {
    "Back", "LED mode", "Brightness", "Haptic", "Detents",
    "Contrast", "Screensaver", "Flip screen", "Wheel invert",
    "Profiles", "Calibrate", "Save now", "Firmware update", "Restart",
};

/* True for entries that hold a value rather than performing an action. */
static bool menu_is_editable(menu_id_t id) {
    switch (id) {
        case MI_LED_MODE: case MI_LED_BRIGHT: case MI_HAPTIC: case MI_DETENTS:
        case MI_CONTRAST: case MI_SCREENSAVER: case MI_FLIP: case MI_ENC_INVERT:
            return true;
        default:
            return false;
    }
}

/* A value the active profile has taken over is shown in brackets: changing the
 * device-wide setting is still possible, but it will not be what you see until
 * you switch to a profile that does not override it. */
static void menu_format_value(menu_id_t id, char *buf, size_t size) {
    hp_settings_t *cfg = hp_settings();
    const hp_profile_t *p = hp_profile_current();
    switch (id) {
        case MI_LED_MODE:
            if (hp_profile_overrides_led_mode()) {
                snprintf(buf, size, "[%s]", hp_led_mode_name(p->led.mode));
            } else {
                snprintf(buf, size, "%s", hp_led_mode_name(cfg->led_mode));
            }
            break;
        case MI_LED_BRIGHT:
            if (hp_profile_overrides_led_brightness()) {
                snprintf(buf, size, "[%d%%]", (p->led.brightness * 100) / 255);
            } else {
                snprintf(buf, size, "%d%%", (cfg->led_brightness * 100) / 255);
            }
            break;
        case MI_HAPTIC:      snprintf(buf, size, "%.1f", (double)cfg->haptic_strength); break;
        case MI_DETENTS:     snprintf(buf, size, "%u", cfg->haptic_detents); break;
        case MI_CONTRAST:    snprintf(buf, size, "%d%%", (cfg->contrast * 100) / 255); break;
        case MI_SCREENSAVER:
            if (cfg->screensaver_sec == 0) snprintf(buf, size, "off");
            else snprintf(buf, size, "%us", cfg->screensaver_sec);
            break;
        case MI_FLIP:        snprintf(buf, size, "%s", cfg->flip ? "yes" : "no"); break;
        case MI_ENC_INVERT:  snprintf(buf, size, "%s", cfg->encoder_invert ? "yes" : "no"); break;
        default:             buf[0] = '\0'; break;
    }
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void menu_adjust(menu_id_t id, int delta) {
    hp_settings_t *cfg = hp_settings();
    switch (id) {
        case MI_LED_MODE:
            cfg->led_mode = (hp_led_mode_t)(((int)cfg->led_mode + delta + HP_LED_MODE_COUNT)
                                            % HP_LED_MODE_COUNT);
            break;
        case MI_LED_BRIGHT:
            cfg->led_brightness = (uint8_t)clamp_int(cfg->led_brightness + delta * 16, 0, 255);
            break;
        case MI_HAPTIC: {
            float v = cfg->haptic_strength + (float)delta * 0.2f;
            cfg->haptic_strength = v < 0.2f ? 0.2f : (v > 5.0f ? 5.0f : v);
            break;
        }
        case MI_DETENTS:
            cfg->haptic_detents = (uint16_t)clamp_int(cfg->haptic_detents + delta * 2, 2, 100);
            break;
        case MI_CONTRAST:
            cfg->contrast = (uint8_t)clamp_int(cfg->contrast + delta * 16, 0, 255);
            break;
        case MI_SCREENSAVER:
            cfg->screensaver_sec = (uint16_t)clamp_int(cfg->screensaver_sec + delta * 30, 0, 900);
            break;
        case MI_FLIP:       cfg->flip = !cfg->flip; break;
        case MI_ENC_INVERT: cfg->encoder_invert = !cfg->encoder_invert; break;
        default: return;
    }
    hp_settings_apply();
    hp_settings_touch();
    hp_ui_invalidate();
}

/* ------------------------------------------------------------------ */
/* Screen transitions                                                  */
/* ------------------------------------------------------------------ */

static void go(hp_screen_t screen) {
    s_screen = screen;
    s_menu_editing = false;
    hp_ui_invalidate();
}

void hp_ui_show_profile(void) { go(HP_SCREEN_PROFILE); }

void hp_ui_invalidate(void) { s_dirty = true; }
hp_screen_t hp_ui_screen(void) { return s_screen; }

void hp_ui_notify_activity(void) {
    s_last_activity_ms = to_ms_since_boot(get_absolute_time());
    if (s_screen == HP_SCREEN_SCREENSAVER) {
        hp_disp_power(true);
        hp_led_set_suspended(false);

        hp_haptic_cmd_t cmd;
        hp_ipc_cmd_get(&cmd);
        cmd.enabled = true;
        hp_ipc_cmd_set(&cmd);

        go(s_screen_before_saver);
    }
}

void hp_ui_toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_toast, sizeof s_toast, fmt, ap);
    va_end(ap);
    s_toast_until = to_ms_since_boot(get_absolute_time()) + TOAST_MS;
    hp_ui_invalidate();
}

static void enter_screensaver(void) {
    if (s_screen == HP_SCREEN_SCREENSAVER) return;
    s_screen_before_saver = s_screen;

    hp_disp_power(false);
    hp_led_set_suspended(true);

    /* Park the motor too: it is the largest consumer on the board and there
     * is nothing to feel while nobody is at the desk. */
    hp_haptic_cmd_t cmd;
    hp_ipc_cmd_get(&cmd);
    cmd.enabled = false;
    hp_ipc_cmd_set(&cmd);

    s_screen = HP_SCREEN_SCREENSAVER;
    HP_LOGD(TAG, "screensaver on");
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_header(const char *text, const char *right) {
    hp_gfx_fill(0, 0, HP_DISP_W, 11, HP_INK_ON);
    hp_gfx_text_clipped(2, 2, text, 1, right ? 96 : 124, HP_INK_OFF);
    if (right) {
        int w = hp_gfx_text_width(right, 1);
        hp_gfx_text(HP_DISP_W - w - 2, 2, right, 1, HP_INK_OFF);
    }
}

static void draw_profile(void) {
    const hp_profile_t *p = hp_profile_current();

    char counter[16];
    if (hp_profile_count() > 0) {
        snprintf(counter, sizeof counter, "%d/%d", hp_profile_active() + 1, hp_profile_count());
    } else {
        snprintf(counter, sizeof counter, "--");
    }
    draw_header(p->name, counter);

    if (hp_profile_count() == 0) {
        hp_gfx_text_centered(64, 26, "No profiles on card", 1, HP_INK_ON);
        hp_gfx_text_centered(64, 40, "Add files to /profiles", 1, HP_INK_ON);
        return;
    }

    /* Two rows of three, matching the physical key layout. */
    for (int i = 0; i < HP_MACRO_COUNT; i++) {
        const hp_button_t *b = &p->buttons[i];
        int col = i % 3, row = i / 3;
        int x = col * 43;
        int y = 13 + row * 26;
        int w = 41, h = 24;

        if (b->has_icon) {
            hp_gfx_xbm(x + (w - HP_ICON_W) / 2, y, HP_ICON_W, HP_ICON_H, b->icon, HP_INK_ON);
            /* Label sits in a knocked-out strip over the icon's lower edge,
             * which is the only way both fit in a 41x24 cell. */
            if (b->label[0]) {
                hp_gfx_fill(x, y + 17, w, 7, HP_INK_OFF);
                int tw = hp_gfx_text_width(b->label, 1);
                int tx = tw < w ? x + (w - tw) / 2 : x;
                hp_gfx_text_clipped(tx, y + 17, b->label, 1, w, HP_INK_ON);
            }
        } else {
            hp_gfx_round_rect(x, y, w, h, HP_INK_ON);
            int tw = hp_gfx_text_width(b->label, 1);
            int tx = tw < w - 4 ? x + (w - tw) / 2 : x + 2;
            hp_gfx_text_clipped(tx, y + 9, b->label, 1, w - 4, HP_INK_ON);
        }

        /* A pressed key inverts, so it is obvious which macro just ran. */
        if (hp_input_is_down(i)) {
            for (int yy = y; yy < y + h; yy++) hp_gfx_hline(x, yy, w, HP_INK_XOR);
        }
    }
}

static void draw_menu(void) {
    draw_header("Settings", NULL);

    const int rows = 4, line_h = 12;
    for (int r = 0; r < rows; r++) {
        int idx = s_menu_scroll + r;
        if (idx >= MI_COUNT) break;

        int y = 14 + r * line_h;
        bool selected = (idx == s_menu_index);

        if (selected) hp_gfx_fill(0, y - 1, HP_DISP_W, line_h - 1, HP_INK_ON);
        hp_ink_t ink = selected ? HP_INK_OFF : HP_INK_ON;

        hp_gfx_text(4, y, k_menu_labels[idx], 1, ink);

        char value[16];
        menu_format_value((menu_id_t)idx, value, sizeof value);
        if (value[0]) {
            int w = hp_gfx_text_width(value, 1);
            if (s_menu_editing && selected) {
                hp_gfx_text(HP_DISP_W - w - 12, y, "<", 1, ink);
                hp_gfx_text(HP_DISP_W - 6,      y, ">", 1, ink);
            }
            hp_gfx_text(HP_DISP_W - w - 8, y, value, 1, ink);
        }
    }

    /* Scrollbar: cheap orientation cue on a four-line window over 14 items. */
    int track = HP_DISP_H - 14;
    int knob = track * rows / MI_COUNT;
    int pos  = track * s_menu_scroll / MI_COUNT;
    hp_gfx_vline(HP_DISP_W - 1, 14, track, HP_INK_ON);
    hp_gfx_fill(HP_DISP_W - 2, 14 + pos, 2, knob > 3 ? knob : 3, HP_INK_ON);
}

static void draw_profile_list(void) {
    draw_header("Profiles", NULL);

    const int rows = 4, line_h = 12;
    int count = hp_profile_count();

    if (count == 0) {
        hp_gfx_text_centered(64, 32, "none found", 1, HP_INK_ON);
        return;
    }

    for (int r = 0; r < rows; r++) {
        int idx = s_list_scroll + r;
        if (idx >= count) break;

        int y = 14 + r * line_h;
        bool selected = (idx == s_list_index);
        if (selected) hp_gfx_fill(0, y - 1, HP_DISP_W, line_h - 1, HP_INK_ON);

        hp_ink_t ink = selected ? HP_INK_OFF : HP_INK_ON;
        hp_gfx_text_clipped(4, y, hp_profile_filename(idx), 1, 112, ink);

        if (idx == hp_profile_active()) hp_gfx_text(HP_DISP_W - 8, y, "*", 1, ink);
    }
}

static void draw_calibrate(void) {
    hp_haptic_state_t st;
    hp_ipc_state_get(&st);

    draw_header("Motor", st.ready ? "ready" : "aligning");

    char line[32];
    snprintf(line, sizeof line, "angle %+7.2f rad", (double)st.angle);
    hp_gfx_text(2, 16, line, 1, HP_INK_ON);
    snprintf(line, sizeof line, "speed %+7.2f r/s", (double)st.velocity);
    hp_gfx_text(2, 26, line, 1, HP_INK_ON);
    snprintf(line, sizeof line, "Uq    %+7.2f V", (double)st.voltage_q);
    hp_gfx_text(2, 36, line, 1, HP_INK_ON);
    snprintf(line, sizeof line, "loop  %lu Hz", (unsigned long)st.loop_hz);
    hp_gfx_text(2, 46, line, 1, HP_INK_ON);

    if (st.fault) {
        snprintf(line, sizeof line, "FAULT %u", st.fault);
        hp_gfx_text(2, 56, line, 1, HP_INK_ON);
    } else {
        hp_gfx_text(2, 56, "any key to exit", 1, HP_INK_ON);
    }
}

static void draw_boot(void) {
    hp_gfx_text_centered(64, 20, "HapticPad", 2, HP_INK_ON);
    hp_gfx_text_centered(64, 44, HP_FW_VERSION, 1, HP_INK_ON);
}

static void draw_toast(void) {
    int w = hp_gfx_text_width(s_toast, 1) + 10;
    int x = (HP_DISP_W - w) / 2;
    hp_gfx_fill(x, 24, w, 15, HP_INK_OFF);
    hp_gfx_rect(x, 24, w, 15, HP_INK_ON);
    hp_gfx_text_centered(64, 29, s_toast, 1, HP_INK_ON);
}

/* Renders and waits for the frame to reach the panel.  Only used on the way
 * out of the firmware, where there is no next iteration to finish the job. */
static void render(void);

static void render_now(void) {
    while (hp_disp_busy()) hp_disp_task();
    render();
    while (hp_disp_busy()) hp_disp_task();
}

static void render(void) {
    hp_gfx_clear();

    switch (s_screen) {
        case HP_SCREEN_BOOT:         draw_boot(); break;
        case HP_SCREEN_PROFILE:      draw_profile(); break;
        case HP_SCREEN_PROFILE_LIST: draw_profile_list(); break;
        case HP_SCREEN_MENU:         draw_menu(); break;
        case HP_SCREEN_CALIBRATE:    draw_calibrate(); break;
        case HP_SCREEN_SCREENSAVER:  return;      /* panel is powered down */
    }

    if (s_toast_until && to_ms_since_boot(get_absolute_time()) < s_toast_until) {
        draw_toast();
    }

    hp_disp_flush(hp_gfx_fb());
}

/* ------------------------------------------------------------------ */
/* Interaction                                                         */
/* ------------------------------------------------------------------ */

static void scroll_into_view(int index, int *scroll, int count, int rows) {
    if (index < *scroll) *scroll = index;
    else if (index >= *scroll + rows) *scroll = index - rows + 1;
    if (*scroll > count - rows) *scroll = count - rows;
    if (*scroll < 0) *scroll = 0;
}

static void menu_move(int delta) {
    s_menu_index = (s_menu_index + delta + MI_COUNT) % MI_COUNT;
    scroll_into_view(s_menu_index, &s_menu_scroll, MI_COUNT, 4);
    hp_ui_invalidate();
}

static void menu_activate(void) {
    menu_id_t id = (menu_id_t)s_menu_index;

    if (menu_is_editable(id)) {
        s_menu_editing = !s_menu_editing;
        hp_ui_invalidate();
        return;
    }

    switch (id) {
        case MI_BACK:
            go(HP_SCREEN_PROFILE);
            break;
        case MI_PROFILES:
            s_list_index = hp_profile_active() < 0 ? 0 : hp_profile_active();
            scroll_into_view(s_list_index, &s_list_scroll, hp_profile_count(), 4);
            go(HP_SCREEN_PROFILE_LIST);
            break;
        case MI_CALIBRATE: {
            hp_haptic_cmd_t cmd;
            hp_ipc_cmd_get(&cmd);
            cmd.calibrate_seq++;
            hp_ipc_cmd_set(&cmd);
            go(HP_SCREEN_CALIBRATE);
            break;
        }
        case MI_SAVE:
            hp_ui_toast(hp_settings_save() ? "Saved" : "Save failed");
            break;
        case MI_BOOTSEL:
            /* Drops straight into the ROM bootloader so a new .uf2 can be
             * dragged across without touching the BOOTSEL button. */
            hp_ui_toast("USB bootloader");
            render_now();
            sleep_ms(400);
            hp_system_enter_bootloader();
            break;
        case MI_REBOOT:
            hp_settings_save();
            hp_ui_toast("Restarting");
            render_now();
            sleep_ms(400);
            hp_system_reboot();
            break;
        default:
            break;
    }
}

static void select_profile_from_list(void) {
    if (hp_profile_load(s_list_index)) {
        hp_settings_apply();
        hp_ini_copy(hp_settings()->last_profile, HP_PATH_LEN,
                    hp_profile_filename(s_list_index));
        hp_settings_touch();
        hp_ui_toast("%s", hp_profile_current()->name);
        go(HP_SCREEN_PROFILE);
    } else {
        hp_ui_toast("Load failed");
    }
}

static void change_profile(int delta) {
    if (hp_profile_count() == 0) return;

    if (delta > 0) hp_profile_next(); else hp_profile_prev();
    hp_settings_apply();
    hp_ini_copy(hp_settings()->last_profile, HP_PATH_LEN,
                hp_profile_filename(hp_profile_active()));
    hp_settings_touch();
    hp_ui_invalidate();
}

void hp_ui_button(int index, hp_btn_event_t event) {
    hp_ui_notify_activity();

    if (index < 0 || index >= HP_BTN_COUNT) return;

    /* Any key leaves the calibration screen. */
    if (s_screen == HP_SCREEN_CALIBRATE && event == HP_BTN_DOWN) {
        go(HP_SCREEN_MENU);
        return;
    }

    const bool is_nav = (index == HP_BTN_NEXT || index == HP_BTN_PREV);

    if (is_nav && event == HP_BTN_DOWN) {
        int other = (index == HP_BTN_NEXT) ? HP_BTN_PREV : HP_BTN_NEXT;
        if (hp_input_is_down(other)) {
            /* Both navigation keys together toggle the settings menu. */
            s_combo_used[index] = s_combo_used[other] = true;
            if (s_screen == HP_SCREEN_MENU || s_screen == HP_SCREEN_PROFILE_LIST) {
                go(HP_SCREEN_PROFILE);
            } else {
                s_menu_index = 0;
                s_menu_scroll = 0;
                go(HP_SCREEN_MENU);
            }
        }
        return;
    }

    if (is_nav && event == HP_BTN_UP) {
        if (s_combo_used[index]) { s_combo_used[index] = false; return; }

        int delta = (index == HP_BTN_NEXT) ? 1 : -1;
        switch (s_screen) {
            case HP_SCREEN_MENU:
                if (s_menu_editing) menu_adjust((menu_id_t)s_menu_index, delta);
                else menu_move(delta);
                break;
            case HP_SCREEN_PROFILE_LIST:
                s_list_index = (s_list_index + delta + hp_profile_count()) % hp_profile_count();
                scroll_into_view(s_list_index, &s_list_scroll, hp_profile_count(), 4);
                hp_ui_invalidate();
                break;
            default:
                change_profile(delta);
                break;
        }
        return;
    }

    /* Auto-repeat only makes sense while navigating a list. */
    if (is_nav && event == HP_BTN_REPEAT) {
        if (s_screen == HP_SCREEN_MENU && !s_menu_editing) menu_move(index == HP_BTN_NEXT ? 1 : -1);
        else if (s_screen == HP_SCREEN_MENU) menu_adjust((menu_id_t)s_menu_index, index == HP_BTN_NEXT ? 1 : -1);
        return;
    }

    /* Macro keys. */
    if (index >= HP_MACRO_COUNT) return;

    if (s_screen == HP_SCREEN_MENU) {
        if (event == HP_BTN_DOWN && index == 0) menu_activate();
        return;
    }
    if (s_screen == HP_SCREEN_PROFILE_LIST) {
        if (event == HP_BTN_DOWN && index == 0) select_profile_from_list();
        else if (event == HP_BTN_DOWN && index == 1) go(HP_SCREEN_MENU);
        return;
    }

    if (s_screen == HP_SCREEN_PROFILE) {
        if (event == HP_BTN_DOWN) {
            hp_macro_start(index);
            hp_ui_invalidate();          /* show the key as pressed */
        } else if (event == HP_BTN_UP) {
            hp_ui_invalidate();
        }
    }
}

bool hp_ui_wheel(int8_t dir) {
    hp_ui_notify_activity();

    switch (s_screen) {
        case HP_SCREEN_MENU:
            if (s_menu_editing) menu_adjust((menu_id_t)s_menu_index, dir);
            else menu_move(dir);
            return true;

        case HP_SCREEN_PROFILE_LIST:
            if (hp_profile_count() > 0) {
                s_list_index = (s_list_index + dir + hp_profile_count()) % hp_profile_count();
                scroll_into_view(s_list_index, &s_list_scroll, hp_profile_count(), 4);
                hp_ui_invalidate();
            }
            return true;

        case HP_SCREEN_CALIBRATE:
            return true;         /* swallowed: turning it is the test */

        default:
            return false;        /* falls through to HID */
    }
}

/* ------------------------------------------------------------------ */

void hp_ui_init(void) {
    hp_ui_notify_activity();
    go(HP_SCREEN_BOOT);
}

void hp_ui_task(void) {
    hp_disp_task();

    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (s_toast_until && now >= s_toast_until) {
        s_toast_until = 0;
        s_dirty = true;
    }

    uint16_t saver = hp_settings()->screensaver_sec;
    if (saver && s_screen != HP_SCREEN_SCREENSAVER &&
        (now - s_last_activity_ms) / 1000u >= saver) {
        enter_screensaver();
    }

    /* The calibration screen shows live motor telemetry, so it redraws on a
     * timer; everything else only when something actually changed. */
    bool periodic = (s_screen == HP_SCREEN_CALIBRATE) &&
                    (now - s_last_draw_ms >= REDRAW_INTERVAL_MS);

    if ((s_dirty || periodic) && !hp_disp_busy()) {
        s_dirty = false;
        s_last_draw_ms = now;
        render();
    }
}
