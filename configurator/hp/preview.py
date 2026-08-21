"""Renders what the pad's OLED will show, pixel for pixel.

This mirrors draw_profile() in src/ui.c against the same font, so what you see
in the configurator is what ends up on the glass - including labels that turn
out to be too long for their cell.
"""

from . import font5x7
from .images import ICON_H, ICON_W, icon_to_matrix

WIDTH = 128
HEIGHT = 64


class Canvas(object):
    def __init__(self, width=WIDTH, height=HEIGHT):
        self.width = width
        self.height = height
        self.rows = [[False] * width for _ in range(height)]

    def pixel(self, x, y, on=True):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.rows[y][x] = on

    def fill(self, x, y, w, h, on=True):
        for j in range(h):
            for i in range(w):
                self.pixel(x + i, y + j, on)

    def rect(self, x, y, w, h, on=True):
        for i in range(w):
            self.pixel(x + i, y, on)
            self.pixel(x + i, y + h - 1, on)
        for j in range(h):
            self.pixel(x, y + j, on)
            self.pixel(x + w - 1, y + j, on)

    def round_rect(self, x, y, w, h, on=True):
        if w < 3 or h < 3:
            return self.rect(x, y, w, h, on)
        for i in range(1, w - 1):
            self.pixel(x + i, y, on)
            self.pixel(x + i, y + h - 1, on)
        for j in range(1, h - 1):
            self.pixel(x, y + j, on)
            self.pixel(x + w - 1, y + j, on)

    def glyph(self, x, y, index, scale=1, on=True):
        base = index * font5x7.FONT_W
        for col in range(font5x7.FONT_W):
            bits = font5x7.FONT[base + col]
            for row in range(8):
                if not (bits & (1 << row)):
                    continue
                if scale == 1:
                    self.pixel(x + col, y + row, on)
                else:
                    self.fill(x + col * scale, y + row * scale, scale, scale, on)

    def text(self, x, y, text, scale=1, on=True):
        for ch in text:
            self.glyph(x, y, font5x7.glyph_index(ch), scale, on)
            x += font5x7.FONT_ADVANCE * scale
        return x

    def text_clipped(self, x, y, text, max_w, scale=1, on=True):
        advance = font5x7.FONT_ADVANCE * scale
        if font5x7.text_width(text, scale) <= max_w:
            return self.text(x, y, text, scale, on)

        budget = max_w - advance
        start = x
        for ch in text:
            if x - start + advance > budget:
                break
            self.glyph(x, y, font5x7.glyph_index(ch), scale, on)
            x += advance
        self.glyph(x, y, 0xAF, scale, on)      # truncation mark, as in gfx.c
        return x + advance

    def xbm(self, x, y, matrix, on=True):
        for row, line in enumerate(matrix):
            for col, lit in enumerate(line):
                if lit:
                    self.pixel(x + col, y + row, on)


def render_profile(profile, icon_data=None, index=None, total=None):
    """icon_data maps an icon filename to its 72 raw bytes (or None if absent)."""
    icon_data = icon_data or {}
    canvas = Canvas()

    # Header bar
    canvas.fill(0, 0, WIDTH, 11, True)
    counter = ""
    if index is not None and total:
        counter = "%d/%d" % (index + 1, total)
    canvas.text_clipped(2, 2, profile.name or "(no name)",
                        96 if counter else 124, 1, False)
    if counter:
        canvas.text(WIDTH - font5x7.text_width(counter) - 2, 2, counter, 1, False)

    for i, button in enumerate(profile.buttons):
        col, row = i % 3, i // 3
        x = col * 43
        y = 13 + row * 26
        w, h = 41, 24

        data = icon_data.get(button.icon) if button.icon else None

        if data:
            canvas.xbm(x + (w - ICON_W) // 2, y, icon_to_matrix(data))
            if button.label:
                canvas.fill(x, y + 17, w, 7, False)
                tw = font5x7.text_width(button.label)
                tx = x + (w - tw) // 2 if tw < w else x
                canvas.text_clipped(tx, y + 17, button.label, w)
        else:
            canvas.round_rect(x, y, w, h)
            tw = font5x7.text_width(button.label)
            tx = x + (w - tw) // 2 if tw < w - 4 else x + 2
            canvas.text_clipped(tx, y + 9, button.label, w - 4)

    return canvas


def to_text(canvas, on="#", off="."):
    return "\n".join("".join(on if v else off for v in row) for row in canvas.rows)


def icon_size():
    return (ICON_W, ICON_H)
