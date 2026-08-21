"""Icon handling: source images in, 24x24 monochrome .bin out.

The firmware draws icons in XBM bit order - within each byte the least
significant bit is the leftmost pixel, three bytes per row, 72 bytes per
icon - which is exactly the format the existing .bin files on the SD card
already use, so nothing on anyone's card has to be regenerated.

Pillow is used when it is installed.  When it is not, the PNG and BMP readers
below take over, which covers every icon set worth importing without asking
the user to install anything.
"""

import struct
import zlib

ICON_W = 24
ICON_H = 24
ICON_BYTES = ICON_W * ICON_H // 8

try:
    from PIL import Image                       # type: ignore
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


class ImageError(Exception):
    pass


class RGBAImage(object):
    """A plain RGBA raster; `pixels` is a flat list of (r, g, b, a) tuples."""

    def __init__(self, width, height, pixels):
        self.width = width
        self.height = height
        self.pixels = pixels

    def get(self, x, y):
        return self.pixels[y * self.width + x]


# ---------------------------------------------------------------------------
# PNG
# ---------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _png_unfilter(data, width, height, bpp, stride):
    out = bytearray(height * stride)
    pos = 0
    prev = bytearray(stride)

    for row in range(height):
        filter_type = data[pos]
        pos += 1
        line = bytearray(data[pos:pos + stride])
        pos += stride

        if filter_type == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upper_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], upper_left)) & 0xFF
        elif filter_type != 0:
            raise ImageError("unsupported PNG filter %d" % filter_type)

        out[row * stride:(row + 1) * stride] = line
        prev = line

    return bytes(out)


def _read_png(raw):
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ImageError("not a PNG file")

    pos = 8
    width = height = depth = color_type = interlace = 0
    palette = []
    transparency = []
    idat = bytearray()

    while pos < len(raw):
        length, ctype = struct.unpack(">I4s", raw[pos:pos + 8])
        pos += 8
        body = raw[pos:pos + length]
        pos += length + 4                     # skip the CRC

        if ctype == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif ctype == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif ctype == b"tRNS":
            transparency = list(body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break

    if interlace:
        raise ImageError("interlaced PNGs are not supported - re-save without "
                         "the interlace option, or install Pillow")

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise ImageError("unsupported PNG colour type %d" % color_type)
    if depth not in (1, 2, 4, 8, 16):
        raise ImageError("unsupported PNG bit depth %d" % depth)

    bits_per_pixel = channels * depth
    stride = (width * bits_per_pixel + 7) // 8
    bpp = max(1, bits_per_pixel // 8)

    data = _png_unfilter(zlib.decompress(bytes(idat)), width, height, bpp, stride)

    def sample(row, index):
        """One channel value, normalised to 0..255."""
        if depth == 8:
            return data[row * stride + index]
        if depth == 16:
            return data[row * stride + index * 2]
        per_byte = 8 // depth
        byte = data[row * stride + index // per_byte]
        shift = 8 - depth * (index % per_byte + 1)
        value = (byte >> shift) & ((1 << depth) - 1)
        return value * 255 // ((1 << depth) - 1)

    pixels = []
    for y in range(height):
        for x in range(width):
            base = x * channels
            if color_type == 0:
                v = sample(y, base)
                pixels.append((v, v, v, 255))
            elif color_type == 4:
                v = sample(y, base)
                pixels.append((v, v, v, sample(y, base + 1)))
            elif color_type == 2:
                pixels.append((sample(y, base), sample(y, base + 1),
                               sample(y, base + 2), 255))
            elif color_type == 6:
                pixels.append((sample(y, base), sample(y, base + 1),
                               sample(y, base + 2), sample(y, base + 3)))
            else:                              # palette
                idx = sample(y, base) * ((1 << depth) - 1) // 255 if depth < 8 \
                    else data[y * stride + base]
                if idx >= len(palette):
                    pixels.append((0, 0, 0, 0))
                else:
                    r, g, b = palette[idx]
                    a = transparency[idx] if idx < len(transparency) else 255
                    pixels.append((r, g, b, a))

    return RGBAImage(width, height, pixels)


# ---------------------------------------------------------------------------
# BMP
# ---------------------------------------------------------------------------

def _read_bmp(raw):
    if raw[:2] != b"BM":
        raise ImageError("not a BMP file")

    offset = struct.unpack("<I", raw[10:14])[0]
    header_size = struct.unpack("<I", raw[14:18])[0]
    if header_size < 40:
        raise ImageError("unsupported BMP header")

    width, height, planes, bpp, compression = struct.unpack("<iiHHI", raw[18:34])
    del planes
    if compression != 0:
        raise ImageError("compressed BMPs are not supported")

    bottom_up = height > 0
    height = abs(height)

    palette = []
    if bpp <= 8:
        count = struct.unpack("<I", raw[46:50])[0] or (1 << bpp)
        pal_start = 14 + header_size
        for i in range(count):
            b, g, r, _ = raw[pal_start + i * 4: pal_start + i * 4 + 4]
            palette.append((r, g, b))

    stride = ((width * bpp + 31) // 32) * 4
    pixels = [(0, 0, 0, 0)] * (width * height)

    for row in range(height):
        y = height - 1 - row if bottom_up else row
        line = raw[offset + row * stride: offset + (row + 1) * stride]
        for x in range(width):
            if bpp == 24 or bpp == 32:
                i = x * (bpp // 8)
                b, g, r = line[i], line[i + 1], line[i + 2]
                a = line[i + 3] if bpp == 32 else 255
                pixels[y * width + x] = (r, g, b, a)
            else:
                per_byte = 8 // bpp
                byte = line[x // per_byte]
                shift = 8 - bpp * (x % per_byte + 1)
                idx = (byte >> shift) & ((1 << bpp) - 1)
                r, g, b = palette[idx] if idx < len(palette) else (0, 0, 0)
                pixels[y * width + x] = (r, g, b, 255)

    return RGBAImage(width, height, pixels)


# ---------------------------------------------------------------------------
# Loading and conversion
# ---------------------------------------------------------------------------

def load_image(path):
    if HAVE_PIL:
        img = Image.open(path).convert("RGBA")
        return RGBAImage(img.width, img.height, list(img.getdata()))

    with open(path, "rb") as fh:
        raw = fh.read()

    if raw[:8] == b"\x89PNG\r\n\x1a\n":
        return _read_png(raw)
    if raw[:2] == b"BM":
        return _read_bmp(raw)

    raise ImageError(
        "only PNG and BMP can be read without Pillow.\n"
        "Install it for other formats:  python3 -m pip install --user Pillow")


def _luminance_alpha(img, x, y):
    r, g, b, a = img.get(x, y)
    return (r * 299 + g * 587 + b * 114) // 1000, a


def resample_gray(img, size=ICON_W):
    """Area-average downscale to size x size, returning (luma, alpha) pairs.

    Averaging rather than nearest-neighbour matters a lot here: icons are
    usually 128 px or larger and point sampling turns thin strokes into a
    dotted mess at 24 px.
    """
    out = []
    for ty in range(size):
        y0 = ty * img.height // size
        y1 = max(y0 + 1, (ty + 1) * img.height // size)
        for tx in range(size):
            x0 = tx * img.width // size
            x1 = max(x0 + 1, (tx + 1) * img.width // size)

            lum_sum = alpha_sum = count = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    lum, alpha = _luminance_alpha(img, x, y)
                    lum_sum += lum
                    alpha_sum += alpha
                    count += 1
            out.append((lum_sum // count, alpha_sum // count))
    return out


MODE_AUTO = "auto"
MODE_ALPHA = "alpha"      # the shape is the opaque part
MODE_DARK = "dark"        # dark glyph on a light background
MODE_LIGHT = "light"      # light glyph on a dark background

MODES = (MODE_AUTO, MODE_ALPHA, MODE_DARK, MODE_LIGHT)


def detect_mode(samples, size=ICON_W):
    """Works out how the artwork encodes its shape.

    Most icon sets ship a coloured glyph on a fully transparent background, in
    which case the alpha channel *is* the shape and the luminance is useless -
    thresholding a white-on-transparent icon by brightness produces a blank
    result, which is exactly the "blank/black image" failure the old converter
    kept hitting.  Only when there is no meaningful transparency do we fall
    back to comparing the border against the middle.
    """
    transparent = sum(1 for _, alpha in samples if alpha < 128)
    if 0.05 < transparent / float(len(samples)) < 0.98:
        return MODE_ALPHA

    border = []
    for i, (lum, _) in enumerate(samples):
        x, y = i % size, i // size
        if x < 2 or y < 2 or x >= size - 2 or y >= size - 2:
            border.append(lum)

    return MODE_DARK if sum(border) / float(len(border)) >= 128 else MODE_LIGHT


def to_icon(img, threshold=128, mode=MODE_AUTO):
    """Converts an image to the firmware's 72-byte icon format.

    Returns (data, mode_used) so the caller can tell the user what happened.
    """
    samples = resample_gray(img)

    if mode == MODE_AUTO:
        mode = detect_mode(samples)

    data = bytearray(ICON_BYTES)
    for y in range(ICON_H):
        for x in range(ICON_W):
            lum, alpha = samples[y * ICON_W + x]

            if mode == MODE_ALPHA:
                on = alpha >= 128
            elif alpha < 128:
                on = False
            elif mode == MODE_DARK:
                on = lum < threshold
            else:
                on = lum >= threshold

            if on:
                data[y * 3 + (x >> 3)] |= 1 << (x & 7)

    return bytes(data), mode


def icon_to_matrix(data):
    """Unpacks a 72-byte icon into rows of booleans, for previewing."""
    if len(data) < ICON_BYTES:
        data = data + b"\x00" * (ICON_BYTES - len(data))
    rows = []
    for y in range(ICON_H):
        row = []
        for x in range(ICON_W):
            row.append(bool(data[y * 3 + (x >> 3)] & (1 << (x & 7))))
        rows.append(row)
    return rows


def icon_coverage(data):
    """Fraction of lit pixels - a blank result is the usual conversion bug."""
    lit = sum(bin(byte).count("1") for byte in data[:ICON_BYTES])
    return lit / float(ICON_W * ICON_H)
