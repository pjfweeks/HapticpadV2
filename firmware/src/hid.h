/* hid.h - the one and only path from firmware state to USB HID reports.
 *
 * Everything that wants to press a key - macros, the wheel, the menu - hands
 * a report to this queue.  hp_hid_task() is the sole caller of tud_hid_report,
 * runs on core 0 only, and refuses to send until TinyUSB says the endpoint is
 * free.  That combination is what fixes the dropped and mangled keystrokes:
 * the previous firmware called into the USB stack from both cores and never
 * checked readiness.
 */
#ifndef HP_HID_H
#define HP_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "keycodes.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HP_REPORT_ID_KEYBOARD = 1,
    HP_REPORT_ID_MOUSE,
    HP_REPORT_ID_CONSUMER,
};

/* Producers stop feeding once the queue passes this mark, so a long macro
 * cannot starve the wheel or the menu of queue space. */
#define HP_HID_FEED_LIMIT 96

void hp_hid_init(void);
void hp_hid_task(void);

/* True once the host has configured the device and is polling us. */
bool hp_hid_ready(void);

/* Queue depth currently in use - the CDC status command reports it, and a
 * persistently high value means something upstream is generating faster than
 * the host can consume. */
uint16_t hp_hid_queued(void);
uint32_t hp_hid_dropped(void);

/* --- enqueue -------------------------------------------------------------
 * All of these return false only when the queue is full.  A press is always
 * queued together with its release, so a full queue can never leave a key
 * stuck down. */
bool hp_hid_key(uint8_t modifiers, const uint8_t keys[HP_STEP_KEYS]);
bool hp_hid_key_hold(uint8_t modifiers, const uint8_t keys[HP_STEP_KEYS]);
bool hp_hid_release_all(void);
bool hp_hid_consumer(uint16_t usage);
bool hp_hid_mouse(uint8_t modifiers, uint8_t buttons, int8_t dx, int8_t dy,
                  int8_t wheel, int8_t pan);
bool hp_hid_text(const char *text);

/* Inserts a pause before the next queued report is sent. */
bool hp_hid_delay(uint16_t ms);

/* Drops everything pending and releases any held keys - used when a profile
 * changes underneath a running macro. */
void hp_hid_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_HID_H */
