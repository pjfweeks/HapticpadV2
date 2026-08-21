/* wheel.h - turns encoder detent ticks into host actions.
 *
 * Core 1 only ever reports "the wheel moved one detent, this way".  All the
 * interpretation - is the menu open, is this a scroll or a keystroke, how
 * many ticks can be merged into one report - happens here, on core 0, next to
 * the HID queue.
 */
#ifndef HP_WHEEL_H
#define HP_WHEEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hp_wheel_init(void);
void hp_wheel_task(void);

/* Total detents seen since boot, split by direction; the configurator shows
 * these while you are tuning the feel. */
uint32_t hp_wheel_ticks_cw(void);
uint32_t hp_wheel_ticks_ccw(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_WHEEL_H */
