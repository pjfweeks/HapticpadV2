/* haptics.h - the core 1 side of the device.
 *
 * Core 1 does one thing: run the field-oriented control loop and make the
 * wheel feel like something.  It never touches USB, the SD card, the display
 * or the heap, so nothing the application does can stutter the motor - which
 * was the structural problem with the previous firmware.
 */
#ifndef HP_HAPTICS_H
#define HP_HAPTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Launches core 1.  Call once, from core 0, after hp_ipc_init(). */
void hp_haptics_start(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_HAPTICS_H */
