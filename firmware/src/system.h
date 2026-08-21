/* system.h - leaving the firmware cleanly.
 *
 * Both exits have to undo what the running system has set up before they jump.
 * The watchdog in particular will happily reset the chip four seconds into the
 * ROM bootloader, which silently defeats a USB firmware update: the board
 * appears as RPI-RP2 just long enough to look like it worked, then reboots
 * into the old firmware.
 */
#ifndef HP_SYSTEM_H
#define HP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reboots into the ROM bootloader so a .uf2 can be dragged onto RPI-RP2.
 * Does not return. */
void hp_system_enter_bootloader(void);

/* Restarts the firmware.  Does not return. */
void hp_system_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_SYSTEM_H */
