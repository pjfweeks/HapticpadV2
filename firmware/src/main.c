/* main.c - core 0: everything except the motor.
 *
 * The whole application is one cooperative loop of short, non-blocking tasks.
 * Nothing here waits on anything: no delay() in a macro, no bit-banged frame,
 * no file read in an interrupt.  That is what keeps the pad responsive while
 * a macro with half-second pauses plays out, and it is why USB now behaves.
 *
 * Division of labour:
 *   core 0 (this file)  USB, SD, display, LEDs, buttons, all HID reports
 *   core 1 (haptics.cpp) the FOC loop and nothing else
 */

#include "board.h"
#include "cdc_protocol.h"
#include "encoder.h"
#include "gfx.h"
#include "haptics.h"
#include "hid.h"
#include "ini.h"
#include "input.h"
#include "ipc.h"
#include "led.h"
#include "log.h"
#include "macro.h"
#include "profile.h"
#include "settings.h"
#include "ssd1309.h"
#include "storage.h"
#include "system.h"
#include "ui.h"
#include "wheel.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define TAG "main"

#define SPLASH_MS       900
#define WATCHDOG_MS    4000
#define CORE1_GRACE_MS 4000   /* FOC alignment on a cold start takes a moment */

static void load_startup_profile(void) {
    hp_profile_scan();
    if (hp_profile_count() == 0) return;

    const char *last = hp_settings()->last_profile;
    if (!last[0] || !hp_profile_load_by_name(last)) {
        hp_profile_load(0);
        hp_ini_copy(hp_settings()->last_profile, HP_PATH_LEN, hp_profile_filename(0));
    }
}

/* Feeds the watchdog only while core 1 is demonstrably alive.  If the FOC
 * loop ever wedges, the board resets instead of sitting there with a motor
 * holding a fixed vector - which is the failure mode that gets warm. */
static void watchdog_task(void) {
    static uint32_t last_beat;
    static uint32_t last_change_ms;
    static bool     started;

    hp_haptic_state_t st;
    hp_ipc_state_get(&st);

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!started) { started = true; last_change_ms = now; }

    if (st.heartbeat != last_beat) {
        last_beat = st.heartbeat;
        last_change_ms = now;
    }

    if (now - last_change_ms < CORE1_GRACE_MS) watchdog_update();
}

/* Core 1 is still driving PWM and PIO when we leave; the ROM bootloader is
 * entitled to assume it owns the chip, so stop it first.  And the watchdog has
 * to go, or it resets us straight back out of the bootloader. */
static void prepare_to_leave(void) {
    hp_hid_flush();                 /* never leave a key stuck down */
    multicore_reset_core1();
    watchdog_disable();
}

void hp_system_enter_bootloader(void) {
    prepare_to_leave();
    reset_usb_boot(0, 0);
    while (1) tight_loop_contents();
}

void hp_system_reboot(void) {
    prepare_to_leave();
    watchdog_reboot(0, 0, 0);
    while (1) tight_loop_contents();
}

int main(void) {
    hp_log_init();
    hp_ipc_init();

    /* USB first: the sooner the host enumerates us, the sooner the log
     * channel is available for whatever goes wrong next. */
    tusb_init();
    hp_hid_init();
    hp_cdc_init();

    hp_encoder_init();
    hp_disp_init();
    hp_led_init();
    hp_input_init();
    hp_input_set_handler(hp_ui_button);
    hp_macro_init();
    hp_wheel_init();

    hp_ui_init();
    hp_ui_task();                       /* paint the splash immediately */

    HP_LOGI(TAG, "%s %s starting", HP_FW_NAME, HP_FW_VERSION);

    hp_settings_defaults();
    if (hp_storage_init()) {
        hp_settings_load();
        load_startup_profile();
    } else {
        HP_LOGW(TAG, "continuing without a card: defaults only, no profiles");
    }

    hp_settings_apply();

    /* Core 1 takes over the motor from here and never comes back. */
    hp_haptics_start();

    absolute_time_t splash_until = make_timeout_time_ms(SPLASH_MS);
    bool splash_done = false;

    watchdog_enable(WATCHDOG_MS, true);

    for (;;) {
        tud_task();          /* USB device stack                            */
        hp_cdc_task();       /* configurator protocol and log streaming     */
        hp_input_task();     /* debounced buttons -> hp_ui_button           */
        hp_wheel_task();     /* detent ticks -> menu or HID                 */
        hp_macro_task();     /* feeds queued macro steps                    */
        hp_hid_task();       /* the only place reports are actually sent    */
        hp_led_task();       /* ring effects                                */
        hp_ui_task();        /* redraw when something changed               */
        hp_settings_task();  /* deferred write-back to the card             */
        watchdog_task();

        if (!splash_done && absolute_time_diff_us(get_absolute_time(), splash_until) <= 0) {
            splash_done = true;
            hp_ui_notify_activity();
            hp_ui_show_profile();
        }
    }
}
