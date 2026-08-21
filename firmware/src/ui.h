/* ui.h - screens, menu and the interaction rules that tie them together.
 *
 * This module owns what a button press *means*.  main() just forwards raw
 * events here, which keeps the mapping between physical input and behaviour
 * in one readable place instead of spread across three files.
 */
#ifndef HP_UI_H
#define HP_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_SCREEN_BOOT = 0,
    HP_SCREEN_PROFILE,
    HP_SCREEN_PROFILE_LIST,
    HP_SCREEN_MENU,
    HP_SCREEN_CALIBRATE,
    HP_SCREEN_SCREENSAVER,
} hp_screen_t;

void hp_ui_init(void);
void hp_ui_task(void);

/* Returns true when the UI consumed the event; the wheel falls through to HID
 * when it did not. */
void hp_ui_button(int index, hp_btn_event_t event);
bool hp_ui_wheel(int8_t dir);

/* Leaves the splash screen and shows the active profile. */
void hp_ui_show_profile(void);

void hp_ui_invalidate(void);
void hp_ui_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
hp_screen_t hp_ui_screen(void);

/* Any activity postpones the screensaver; called by input and by the
 * configurator so a remote change also wakes the panel. */
void hp_ui_notify_activity(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_UI_H */
