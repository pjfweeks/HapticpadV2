/* encoder.h - quadrature wheel encoder, decoded in PIO and delivered by DMA.
 *
 * Reading the wheel costs a single load from RAM: no interrupts, no critical
 * sections, and no possibility of a missed edge under load.
 */
#ifndef HP_ENCODER_H
#define HP_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hp_encoder_init(void);

/* Raw x4 count since boot.  Wraps naturally on int32 overflow, which at
 * 4096 counts per revolution is half a million turns away. */
int32_t hp_encoder_count(void);

/* Absolute shaft angle in radians, unwrapped (keeps growing past 2*pi). */
float hp_encoder_angle(void);

/* Flip the sign if the A/B wires are swapped relative to the motor phases. */
void hp_encoder_set_invert(bool invert);
bool hp_encoder_invert(void);

/* Define the current position as zero. */
void hp_encoder_zero(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_ENCODER_H */
