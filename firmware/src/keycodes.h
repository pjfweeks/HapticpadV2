/* keycodes.h - action strings to HID, resolved once at profile load time.
 *
 * The Arduino firmware re-parsed "CTRL+SHIFT+L" from an Arduino String on
 * every single key press, inside the same call that talked to USB.  Here a
 * profile is compiled into hp_step_t structs when it is loaded, so pressing a
 * button is just a memcpy into a report queue.
 *
 * Accepted action syntax (case-insensitive):
 *   CTRL+SHIFT+F12      modifiers plus a key
 *   CTRL+C, CTRL+V      up to three non-modifier keys held together
 *   VOL_UP MUTE PLAY    consumer-control keys
 *   TEXT:hello world    type a literal string
 *   HOLD:CTRL           press and keep held until the matching RELEASE
 *   RELEASE:CTRL        release a previously held combination
 *   MOUSE_LEFT          mouse button click (also MOUSE_RIGHT, MOUSE_MIDDLE)
 *   SCROLL_UP/DOWN      wheel, SCROLL_LEFT/RIGHT for horizontal pan
 *   WAIT                pure delay, using the step's own delay value
 */
#ifndef HP_KEYCODES_H
#define HP_KEYCODES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_STEP_NONE = 0,
    HP_STEP_KEY,        /* press modifiers+keys, then release            */
    HP_STEP_HOLD,       /* press and leave held                          */
    HP_STEP_RELEASE,    /* release everything currently held             */
    HP_STEP_TEXT,       /* type `text` character by character            */
    HP_STEP_CONSUMER,   /* media / volume key                            */
    HP_STEP_MOUSE,      /* buttons, movement, wheel - modifiers apply    */
    HP_STEP_WAIT,       /* delay only                                    */
} hp_step_type_t;

#define HP_STEP_KEYS 3          /* simultaneous non-modifier keys */

typedef struct {
    uint8_t  type;              /* hp_step_type_t                        */
    uint8_t  modifiers;         /* KEYBOARD_MODIFIER_* bitmask           */
    uint8_t  keys[HP_STEP_KEYS];
    uint16_t consumer;          /* HID_USAGE_CONSUMER_*                  */
    uint8_t  mouse_buttons;
    int8_t   mouse_dx, mouse_dy;
    int8_t   mouse_wheel, mouse_pan;
    uint16_t delay_ms;          /* applied after the step                */
    char     text[HP_TEXT_LEN];
} hp_step_t;

/* Parses one action string.  Returns false (and leaves *out zeroed) if the
 * string names nothing we recognise, so a typo in a profile disables that one
 * step rather than the whole button. */
bool hp_action_parse(const char *action, hp_step_t *out);

/* Renders a step back to its canonical action string - used by the CDC
 * protocol so the configurator sees exactly what the pad understood. */
void hp_action_format(const hp_step_t *step, char *dst, size_t dst_size);

/* Maps one ASCII character to the key and modifier that produce it on a US
 * layout.  Returns false for characters we cannot type. */
bool hp_char_to_key(char c, uint8_t *key, uint8_t *modifier);

#ifdef __cplusplus
}
#endif

#endif /* HP_KEYCODES_H */
