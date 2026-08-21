# HapticPad Configurator

Configures everything on the pad — profiles, macros, icons, LED and wheel
settings — over the USB cable. The SD card stays in the device; the firmware
does the reading and writing on its behalf.

## Running it

```bash
python3 hapticpad.py
```

That is the whole installation. It runs on the Python that ships with macOS
and opens its interface in your browser.

| Option | Effect |
| --- | --- |
| `--no-browser` | start the server without opening a browser |
| `--port N` | listen on a fixed port instead of a free one |

Two optional packages, neither required:

```bash
python3 -m pip install --user -r requirements.txt
```

- **pyserial** — needed on Windows. On macOS and Linux the app opens the
  serial device directly.
- **Pillow** — lets the icon importer read JPEG and GIF. PNG and BMP work
  without it, through a decoder built on `zlib`.

## Why it runs in a browser

The first version used tkinter, which looked like the obvious zero-dependency
choice. It is not one on macOS: `/usr/bin/python3` is bound to Tk 8.5.9, a
2010 build Apple has deprecated, and on macOS 11 and later it frequently fails
to paint a window at all — the background appears and no child widget is ever
drawn. There is no other Tk on a stock machine.

Serving the interface to the browser keeps the original promise (nothing to
install) while handing the rendering to something that works. Everything below
the presentation layer is unchanged.

Nothing leaves the machine: the server binds loopback only, and every API call
must carry a token minted at startup and injected into the page. That token is
what stops a web page you happen to have open from driving your keyboard — a
custom header forces a CORS preflight, which the browser will not send
cross-origin.

## What each tab does

**Profiles** — the list on the left is what is actually on the card. Editing a
profile shows a pixel-accurate preview of the pad's screen, rendered by the
firmware's own font and layout code, so a label too long for its cell looks
too long here too. **Steps** opens a table editor for multi-step macros with a
picker of common shortcuts, and every action has a **Test** button that makes
the pad send it right now.

The **LED ring** section belongs to the profile: mode, primary and secondary
colour and brightness, each of which can either be set here or left to follow
the device-wide setting. The ring preview shows what the result will be, and
the line underneath says which parts are inherited. **Clear** puts a field
back to inheriting — something a colour picker cannot express on its own.

**Device settings** — LED mode and colours, wheel force and detent count,
contrast, screen rotation, screensaver timeout. **Apply** takes effect
immediately so you can feel a change before committing it; **Save to card**
makes it survive a power cycle.

**Icons** — browse what is on the card and import any image. The converter
works out whether the shape is the opaque part of the artwork or a dark glyph
on a light background, previews the result at 24×24, and says so when the
conversion produced nothing. That check matters: every icon on the original
card was 72 bytes of zeros because the old converter got this wrong silently.

**Monitor** — live telemetry (FOC loop rate, wheel position, HID queue depth,
dropped reports) plus the device log, and a box for typing protocol commands
by hand.

## Testing it without a pad

```bash
python3 selftest.py
```

Checks the profile round trip, the icon converter, the OLED preview and the
web API including the token enforcement. No hardware needed.

## Talking to the pad without any of this

The protocol is plain text, so a terminal is enough:

```bash
screen /dev/cu.usbmodem* 115200
```

`INFO`, `STATUS`, `PROFILES`, `SET led_brightness 200`, `TEST CTRL+C`. The full
command list is at the top of `../firmware/src/cdc_protocol.h`.

## Layout

| File | Role |
| --- | --- |
| `hapticpad.py` | launcher |
| `hp/webserver.py` | local HTTP server and JSON API |
| `hp/webui/` | the interface: `index.html`, `app.css`, `app.js` |
| `hp/transport.py` | port discovery; pyserial or a raw termios fallback |
| `hp/protocol.py` | the request/response client |
| `hp/profiles.py` | profile files, parsed the way the firmware parses them |
| `hp/images.py` | PNG/BMP decoding and 24×24 monochrome conversion |
| `hp/preview.py` | renders the OLED screen |
| `hp/font5x7.py` | the firmware's font, generated from `src/font5x7.h` |
| `selftest.py` | offline checks |
