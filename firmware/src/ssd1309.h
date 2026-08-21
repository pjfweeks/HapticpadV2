/* ssd1309.h - display panel driver: PIO SPI + DMA, no CPU in the hot path.
 *
 * A full frame is 1024 bytes.  Bit-banged over software SPI (what the Arduino
 * firmware did) that costs roughly 30 ms of blocked CPU; here the DMA engine
 * feeds the PIO state machine and the transfer completes in about 1 ms with
 * the CPU free to scan buttons and service USB throughout.
 */
#ifndef HP_SSD1309_H
#define HP_SSD1309_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HP_FB_BYTES (128 * 64 / 8)

void hp_disp_init(void);

/* Kick off an asynchronous transfer of `fb` (HP_FB_BYTES).  The buffer must
 * stay valid until hp_disp_busy() goes false.  Returns false if a transfer is
 * still running, in which case nothing was started. */
bool hp_disp_flush(const uint8_t *fb);
bool hp_disp_busy(void);

/* Call from the main loop: releases chip select once a DMA frame has landed.
 * Cheap enough to call unconditionally. */
void hp_disp_task(void);

void hp_disp_set_contrast(uint8_t contrast);
void hp_disp_power(bool on);          /* panel off for the screensaver */
void hp_disp_set_flip(bool flipped);  /* 180 degree rotation, done in the panel */

#ifdef __cplusplus
}
#endif

#endif /* HP_SSD1309_H */
