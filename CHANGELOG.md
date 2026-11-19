# Changelog

## Unreleased

### Added

- **Linux toolchain setup.** `firmware/tools/setup_linux.sh` does what
  `setup_macos.sh` does on x86_64 and aarch64 Linux, without root: system
  CMake/Ninja if present (else a venv, sidestepping PEP 668), the Linux ARM
  toolchain, and the Pico SDK. It also patches SDK 2.1.1's `pioasm`, which
  does not compile on GCC 15+ without `<cstdint>`.

### Fixed

- **Display chip select released early.** The driver waited a fixed 3 µs
  after the TX FIFO drained before moving CS or DC, which only covers the last
  byte at 8 MHz, and the end-of-frame release did not wait at all, so the last
  byte of each frame could be cut off. Both now wait one byte time at the
  configured clock, which is a new setting, `HP_DISP_SPI_HZ` in `board.h`
  (default 8 MHz, unchanged).
- **"Add files to /profiles" was clipped.** At 132 px it was wider than the
  128 px panel; it now reads "Add files: /profiles".
- **Documented the display's 3.3 V supply.** A 5 V supply makes the module's
  level shifter miss clock edges, giving a black or garbled screen.

## 2.0.0

A complete firmware rewrite, from an Arduino sketch to C/C++ on the Raspberry
Pi Pico SDK, plus a new desktop configurator. The hardware is unchanged: the
same PCBs, the same pins, no rewiring.

### Fixed — the reasons for the rewrite

- **Macros that never reached the host.** `sendKeystroke()` ran on both cores
  (core 1 for buttons, core 0 for the wheel) and TinyUSB is not reentrant, so
  concurrent HID reports were corrupted or lost. Both cores were also
  allocating Arduino `String` objects on the same heap, and nothing checked
  that the endpoint was ready before sending. Core 0 is now the only writer of
  HID reports, and every report goes through one queue.
- **The wheel's zoom function.** `HID_KEY_CONTROL_LEFT` (0xE0) was written
  into the report's *modifier* byte, which is a bitmask — the host saw
  RightShift + RightAlt + RightGUI instead of Ctrl.
- **Blocking everywhere.** Macros used `delay()`, so `CTRL+P, wait 500 ms,
  ENTER` froze the display, the LEDs and the button scan for half a second.
  Macro delays now live in the HID queue instead.
- **Empty icons.** Every `.bin` on the original SD card was 72 bytes of zeros:
  the old converter thresholded white-on-transparent artwork by brightness and
  produced nothing. The new converter detects which part of the image is the
  glyph, previews the result, and reports a conversion that came out blank.

### Changed — performance

| Job | v1 | v2 |
| --- | --- | --- |
| Display | software SPI, ~30 ms/frame of blocked CPU | PIO + DMA, ~1 ms, CPU free |
| LED ring | FastLED bit-bang, interrupts disabled | PIO + DMA |
| Encoder | 2 GPIO interrupts, up to 80 k/s | PIO quadrature decoder, zero interrupts |
| FOC loop | claimed 20–30 kHz, unreachable on an M0+ without an FPU | measured 6.8 kHz live, ~81 kHz with the motor parked |

The display pins (GP28 clock, GP22 data) are not a legal hardware-SPI pair on
the RP2040 — which is why v1 had to bit-bang them. PIO drives the same pins.

### Added

- **Up to 8 macro steps** per button, up from 3.
- **Richer actions**: media keys, `TEXT:` strings, `HOLD:`/`RELEASE` for held
  modifiers, mouse buttons, scrolling, and modified scrolling (`CTRL+SCROLL_UP`).
- **Wheel feel modes**: `off free clicky ratchet damped 4points`, with
  per-profile `wheel_detents` and `wheel_strength`.
- **Wheel function presets**: `scroll scroll_h zoom volume page tabs history
  undo_redo arrow arrow_h timeline custom`, or raw `wheel_cw` / `wheel_ccw`.
- **LED modes**: `off solid rainbow breath bands halo reactive profile`, with
  per-profile overrides that are per key, not all-or-nothing.
- **On-device settings menu** — LED, wheel force and detents, contrast, screen
  rotation, screensaver timeout, and a **Firmware update** entry that reboots
  into the bootloader, so BOOTSEL is needed only for the first flash.
- **Screensaver** after a configurable timeout; it parks the motor.
- **USB serial protocol** (`INFO`, `STATUS`, `SET`, `PROFILES`, `READ`,
  `WRITE`, `TEST`, `BOOTSEL`…) — plain text, usable from a terminal.
- **Desktop configurator**: profile and macro editing with a pixel-accurate
  preview of the pad's screen, an icon importer, device settings applied live,
  and a telemetry monitor. Runs in the browser off a loopback server with a
  per-session token; nothing to install on a stock macOS.
- **Plain-text configuration** (`config.txt`, `profiles/*.txt`) replacing the
  XML file, with the old spellings still accepted.
- **Diagnostics**: `STATUS` reports the FOC loop rate, HID queue depth and the
  count of dropped reports and encoder ticks.

### Compatibility

Profiles from v1 load unchanged. `color` and `led_color` are read as
`led_primary`, `led` as `led_mode`, `hap_str` as `wheel_strength`; the
configurator rewrites them to the canonical names when it next saves.
Icons are searched in `/profiles/icons/` and also in `/icons/`.

## 1.0.0

The original project by dmcke5: Arduino firmware, XML configuration on the SD
card, three macro steps per button, three wheel modes.
