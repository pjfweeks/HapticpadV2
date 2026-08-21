/* gfx.h - monochrome framebuffer drawing.
 *
 * The buffer is laid out exactly the way the SSD1309 wants it in horizontal
 * addressing mode (page-major, bit 0 = topmost row of the page), so a frame
 * goes straight to DMA with no conversion step.
 */
#ifndef HP_GFX_H
#define HP_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "ssd1309.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_INK_OFF = 0,
    HP_INK_ON  = 1,
    HP_INK_XOR = 2,
} hp_ink_t;

uint8_t *hp_gfx_fb(void);
void hp_gfx_clear(void);

void hp_gfx_pixel(int x, int y, hp_ink_t ink);
void hp_gfx_hline(int x, int y, int w, hp_ink_t ink);
void hp_gfx_vline(int x, int y, int h, hp_ink_t ink);
void hp_gfx_rect(int x, int y, int w, int h, hp_ink_t ink);
void hp_gfx_fill(int x, int y, int w, int h, hp_ink_t ink);
void hp_gfx_round_rect(int x, int y, int w, int h, hp_ink_t ink);

/* Text is UTF-8; the Latin-1 vowels used by Italian labels are mapped onto
 * the font's CP437 upper half.  `scale` of 1 gives 5x7, 2 gives 10x14.
 * Returns the advance width in pixels. */
int hp_gfx_text(int x, int y, const char *s, int scale, hp_ink_t ink);
int hp_gfx_text_width(const char *s, int scale);
int hp_gfx_text_centered(int cx, int y, const char *s, int scale, hp_ink_t ink);

/* Draws text clipped to `max_w`, appending a horizontal ellipsis if it does
 * not fit - profile labels are user-supplied and routinely too long. */
int hp_gfx_text_clipped(int x, int y, const char *s, int scale, int max_w, hp_ink_t ink);

/* XBM bit order: within each byte the least significant bit is the leftmost
 * pixel, rows padded up to whole bytes.  This matches the existing .bin icon
 * files on the SD card, so they keep working unchanged. */
void hp_gfx_xbm(int x, int y, int w, int h, const uint8_t *data, hp_ink_t ink);

#ifdef __cplusplus
}
#endif

#endif /* HP_GFX_H */
