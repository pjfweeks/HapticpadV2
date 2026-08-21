#include "gfx.h"

#include <string.h>

#include "font5x7.h"

static uint8_t s_fb[HP_FB_BYTES];

uint8_t *hp_gfx_fb(void) { return s_fb; }
void hp_gfx_clear(void)  { memset(s_fb, 0, sizeof s_fb); }

void hp_gfx_pixel(int x, int y, hp_ink_t ink) {
    if (x < 0 || x >= HP_DISP_W || y < 0 || y >= HP_DISP_H) return;
    uint8_t *p = &s_fb[(y >> 3) * HP_DISP_W + x];
    uint8_t  m = (uint8_t)(1u << (y & 7));
    switch (ink) {
        case HP_INK_ON:  *p |= m;  break;
        case HP_INK_OFF: *p &= (uint8_t)~m; break;
        case HP_INK_XOR: *p ^= m;  break;
    }
}

void hp_gfx_hline(int x, int y, int w, hp_ink_t ink) {
    for (int i = 0; i < w; i++) hp_gfx_pixel(x + i, y, ink);
}

void hp_gfx_vline(int x, int y, int h, hp_ink_t ink) {
    for (int i = 0; i < h; i++) hp_gfx_pixel(x, y + i, ink);
}

void hp_gfx_rect(int x, int y, int w, int h, hp_ink_t ink) {
    if (w <= 0 || h <= 0) return;
    hp_gfx_hline(x, y, w, ink);
    hp_gfx_hline(x, y + h - 1, w, ink);
    hp_gfx_vline(x, y, h, ink);
    hp_gfx_vline(x + w - 1, y, h, ink);
}

void hp_gfx_fill(int x, int y, int w, int h, hp_ink_t ink) {
    for (int j = 0; j < h; j++) hp_gfx_hline(x, y + j, w, ink);
}

/* A one-pixel corner notch reads as a rounded box at this size and costs far
 * less than real arc drawing. */
void hp_gfx_round_rect(int x, int y, int w, int h, hp_ink_t ink) {
    if (w < 3 || h < 3) { hp_gfx_rect(x, y, w, h, ink); return; }
    hp_gfx_hline(x + 1, y, w - 2, ink);
    hp_gfx_hline(x + 1, y + h - 1, w - 2, ink);
    hp_gfx_vline(x, y + 1, h - 2, ink);
    hp_gfx_vline(x + w - 1, y + 1, h - 2, ink);
}

/* --- text --------------------------------------------------------------- */

/* Map the Latin-1 code points that actually turn up in Italian profile names
 * onto the font's CP437 upper half.  Anything else becomes a dot, which is
 * far more legible than the mojibake two-glyph rendering of raw UTF-8. */
static uint8_t latin1_to_cp437(uint32_t cp) {
    switch (cp) {
        case 0xE0: return 0x85;  /* a grave */
        case 0xE1: return 0xA0;  /* a acute */
        case 0xE8: return 0x8A;  /* e grave */
        case 0xE9: return 0x82;  /* e acute */
        case 0xEC: return 0x8D;  /* i grave */
        case 0xED: return 0xA1;  /* i acute */
        case 0xF2: return 0x95;  /* o grave */
        case 0xF3: return 0xA2;  /* o acute */
        case 0xF9: return 0x97;  /* u grave */
        case 0xFA: return 0xA3;  /* u acute */
        case 0xE7: return 0x87;  /* c cedilla */
        case 0xC8: return 0xD4;  /* E grave */
        case 0xB0: return 0xF8;  /* degree  */
        case 0xB5: return 0xE6;  /* micro   */
        default:   return 0xF9;  /* small centred dot */
    }
}

/* Decodes one UTF-8 sequence, returning the glyph index and advancing *s. */
static uint8_t next_glyph(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint8_t c = *p++;
    if (c < 0x80) { *s = (const char *)p; return c; }

    uint32_t cp; int extra;
    if      ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
    else                         { *s = (const char *)p; return 0xF9; }

    for (int i = 0; i < extra; i++) {
        if ((*p & 0xC0) != 0x80) break;          /* truncated - bail out */
        cp = (cp << 6) | (*p++ & 0x3Fu);
    }
    *s = (const char *)p;
    return latin1_to_cp437(cp);
}

static void draw_glyph(int x, int y, uint8_t g, int scale, hp_ink_t ink) {
    const uint8_t *bits = &hp_font5x7[(size_t)g * HP_FONT_W];
    for (int col = 0; col < HP_FONT_W; col++) {
        uint8_t bitmap = bits[col];
        for (int row = 0; row < 8; row++) {
            if (!(bitmap & (1u << row))) continue;
            if (scale == 1) {
                hp_gfx_pixel(x + col, y + row, ink);
            } else {
                hp_gfx_fill(x + col * scale, y + row * scale, scale, scale, ink);
            }
        }
    }
}

int hp_gfx_text(int x, int y, const char *s, int scale, hp_ink_t ink) {
    if (scale < 1) scale = 1;
    int start = x;
    while (*s) {
        uint8_t g = next_glyph(&s);
        draw_glyph(x, y, g, scale, ink);
        x += HP_FONT_ADVANCE * scale;
    }
    return x - start;
}

int hp_gfx_text_width(const char *s, int scale) {
    if (scale < 1) scale = 1;
    int n = 0;
    while (*s) { next_glyph(&s); n++; }
    return n * HP_FONT_ADVANCE * scale;
}

int hp_gfx_text_centered(int cx, int y, const char *s, int scale, hp_ink_t ink) {
    int w = hp_gfx_text_width(s, scale);
    return hp_gfx_text(cx - w / 2, y, s, scale, ink);
}

int hp_gfx_text_clipped(int x, int y, const char *s, int scale, int max_w, hp_ink_t ink) {
    if (scale < 1) scale = 1;
    int adv = HP_FONT_ADVANCE * scale;
    if (hp_gfx_text_width(s, scale) <= max_w) return hp_gfx_text(x, y, s, scale, ink);

    int budget = max_w - adv;            /* leave room for the ellipsis */
    int start = x;
    while (*s && x - start + adv <= budget) {
        uint8_t g = next_glyph(&s);
        draw_glyph(x, y, g, scale, ink);
        x += adv;
    }
    draw_glyph(x, y, 0xAF, scale, ink);  /* CP437 '>>' reads as a truncation mark */
    return x + adv - start;
}

void hp_gfx_xbm(int x, int y, int w, int h, const uint8_t *data, hp_ink_t ink) {
    int stride = (w + 7) / 8;
    for (int row = 0; row < h; row++) {
        const uint8_t *line = &data[(size_t)row * stride];
        for (int col = 0; col < w; col++) {
            if (line[col >> 3] & (1u << (col & 7))) {
                hp_gfx_pixel(x + col, y + row, ink);
            }
        }
    }
}
