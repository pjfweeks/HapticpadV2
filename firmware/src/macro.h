/* macro.h - runs a button's macro without ever blocking.
 *
 * The old implementation looped over the steps calling delay(), which stalled
 * the whole core for the duration of the macro - a "CTRL+P, wait 500 ms,
 * ENTER" sequence froze the display, the LEDs and the button scan for half a
 * second.  Here the engine only ever feeds the HID queue with whatever room
 * it has and returns immediately; the delays are honoured by the sender.
 */
#ifndef HP_MACRO_H
#define HP_MACRO_H

#include <stdbool.h>
#include <stdint.h>

#include "keycodes.h"

#ifdef __cplusplus
extern "C" {
#endif

void hp_macro_init(void);

/* Starts the macro bound to a macro button (0..HP_MACRO_COUNT-1).  A macro
 * already in flight is abandoned first, so mashing buttons behaves the way a
 * keyboard does rather than interleaving two sequences. */
bool hp_macro_start(int button);

/* Runs a single ad-hoc step - used by the wheel and by the configurator's
 * "test this action" button. */
bool hp_macro_run_step(const hp_step_t *step);

void hp_macro_task(void);
bool hp_macro_running(void);
void hp_macro_abort(void);

/* Index of the button whose macro is running, or -1. */
int hp_macro_current_button(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_MACRO_H */
