/* profile.h - the active profile, compiled from a text file on the SD card.
 *
 * A profile is parsed exactly once, when it is selected.  Everything the hot
 * path needs afterwards - key codes, icons, wheel actions - is already in
 * fixed-size structs, so pressing a button or turning the wheel touches no
 * strings and no file system.
 *
 * File format (extends the existing one; old profiles load unchanged):
 *
 *   name=Excel
 *
 *   led_mode=solid          off solid rainbow breath bands halo reactive
 *   led_primary=#00FF00     also accepted as color= for older files
 *   led_secondary=#0000FF   used by 'bands'
 *   led_brightness=180      0-255
 *   # anything omitted falls back to the device-wide setting
 *   wheel=clicky            clicky | free | ratchet | damped | 4points | off
 *   wheel_func=scroll_h     scroll scroll_h zoom volume page tabs history
 *                           undo_redo arrow timeline custom
 *   wheel_detents=24        detents per turn in clicky mode
 *   wheel_strength=1.5      per-profile haptic force; omit to use the global
 *   wheel_cw=CTRL+C         overrides whatever wheel_func implied
 *   wheel_ccw=CTRL+V
 *
 *   btn1_label=Salva
 *   btn1_icon=save.bin
 *   btn1_action=CTRL+P,500;ENTER      up to HP_MAX_STEPS steps, "action,delay"
 */
#ifndef HP_PROFILE_H
#define HP_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "ipc.h"
#include "keycodes.h"
#include "led.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char      label[HP_LABEL_LEN];
    char      icon_file[HP_PATH_LEN];
    bool      has_icon;
    uint8_t   icon[HP_ICON_BYTES];
    uint8_t   step_count;
    hp_step_t steps[HP_MAX_STEPS];
} hp_button_t;

/* Per-profile LED overrides.
 *
 * Each field is independent and optional: a profile can restate the whole
 * ring, or just its colour, and anything it leaves out keeps whatever the
 * device-wide setting says.  That is why every field carries its own `has_`
 * flag rather than relying on a sentinel - black and zero brightness are
 * legitimate choices, not "unset". */
typedef struct {
    bool          has_mode;
    hp_led_mode_t mode;
    bool          has_primary;
    uint32_t      primary;
    bool          has_secondary;
    uint32_t      secondary;
    bool          has_brightness;
    uint8_t       brightness;
} hp_profile_led_t;

typedef struct {
    char             file[HP_PATH_LEN];
    char             name[HP_NAME_LEN];
    hp_profile_led_t led;
    hp_haptic_mode_t wheel_mode;
    uint16_t         detents;
    float            strength;           /* 0 = inherit the global setting */
    char             wheel_func[16];
    hp_step_t        wheel_cw;
    hp_step_t        wheel_ccw;
    hp_button_t      buttons[HP_MACRO_COUNT];
} hp_profile_t;

/* Rebuilds the list of profile files found in /profiles. */
void hp_profile_scan(void);
int  hp_profile_count(void);
const char *hp_profile_filename(int index);

bool hp_profile_load(int index);
bool hp_profile_load_by_name(const char *filename);
void hp_profile_next(void);
void hp_profile_prev(void);

int  hp_profile_active(void);
const hp_profile_t *hp_profile_current(void);

/* Name of a haptic mode, for the menu and the configurator. */
const char *hp_haptic_mode_name(hp_haptic_mode_t mode);
bool hp_haptic_mode_from_name(const char *name, hp_haptic_mode_t *out);

/* True when the active profile overrides that part of the LED configuration -
 * the on-device menu uses these to show that changing the global value will
 * not be what you see. */
bool hp_profile_overrides_led_mode(void);
bool hp_profile_overrides_led_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_PROFILE_H */
