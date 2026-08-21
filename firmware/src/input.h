/* input.h - button scanning and debouncing.
 *
 * The previous implementation acted on the *first* edge it saw and only then
 * started a debounce timer, so contact bounce could fire a macro twice and a
 * quick tap could be swallowed entirely.  Here a level has to hold steady for
 * HP_BTN_DEBOUNCE_US before it counts, and events are delivered for press,
 * release, auto-repeat and long-press.
 */
#ifndef HP_INPUT_H
#define HP_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_BTN_DOWN = 0,
    HP_BTN_UP,
    HP_BTN_REPEAT,    /* held past HP_BTN_REPEAT_MS, then every REPEAT_RATE */
    HP_BTN_LONG,      /* held past HP_BTN_LONG_MS, delivered once           */
} hp_btn_event_t;

typedef void (*hp_input_cb)(int index, hp_btn_event_t event);

void hp_input_init(void);
void hp_input_set_handler(hp_input_cb cb);
void hp_input_task(void);

bool hp_input_is_down(int index);
/* Milliseconds since the button went down, or 0 if it is up. */
uint32_t hp_input_held_ms(int index);

#ifdef __cplusplus
}
#endif

#endif /* HP_INPUT_H */
