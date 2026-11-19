# HapticPad firmware 2.0

A ground-up rewrite of the macropad firmware on the Raspberry Pi Pico SDK,
replacing the Arduino sketch that shipped with version 1 of the project.

## Why the rewrite

Three problems in the old firmware were structural rather than incidental, and
none of them could be patched out:

**Macros did not reach the host reliably.** `sendKeystroke()` was called from
both cores — core 1 ran it for button macros, core 0 ran it from the wheel
handler — and TinyUSB is not reentrant. Two cores writing HID reports at once
corrupt or lose them, and both cores were also allocating and freeing Arduino
`String` objects on the same heap. There was no `ready()` check before sending
either, so reports issued while the endpoint was busy simply vanished.

**The wheel's zoom function never worked.** `Haptics.ino` put
`HID_KEY_CONTROL_LEFT` (0xE0) into the report's *modifier* byte, which is a
bitmask: the host saw RightShift + RightAlt + RightGUI instead of Ctrl.

**Everything blocked everything.** Macros used `delay()`, so a
`CTRL+P, wait 500 ms, ENTER` sequence froze the display, the LEDs and the
button scan for half a second. The display ran on bit-banged software SPI —
about 30 ms of blocked CPU per frame — and FastLED bit-banged the ring with
interrupts disabled.

A fourth problem lived on the SD card rather than in the firmware: every
`.bin` icon file was 72 bytes of zeros, because the old converter thresholded
white-on-transparent artwork by brightness and produced nothing. They have
been regenerated.

## Architecture

```
core 0 — application                     core 1 — haptics
────────────────────────                 ─────────────────────────
USB (TinyUSB): HID + CDC                 SimpleFOC control loop
  ↳ the ONLY writer of HID reports       reads the PIO encoder count
display (PIO SPI + DMA)                  computes the detent torque
LED ring (PIO + DMA)                     publishes angle/velocity
SD card (hardware SPI0 + FatFs)          pushes detent ticks
buttons, menu, macro engine
```

The two cores share exactly three things, all in `src/ipc.c` and all lock-free:
a single-producer ring of wheel ticks, and two seqlock-protected structs for
commands going down and telemetry coming up. Core 1 never touches USB, the
card, the display or the heap, so nothing the application does can stutter the
motor — and nothing the motor does can corrupt a keystroke.

Core 0 is one cooperative loop of short non-blocking tasks. Macro delays are
carried in the HID queue rather than in `delay()` calls, so a macro with
half-second pauses plays out while the display keeps redrawing.

### What PIO bought us

| Job | Before | Now |
| --- | --- | --- |
| Display | software SPI, ~30 ms/frame of blocked CPU | PIO + DMA, ~1 ms, CPU free |
| LED ring | FastLED bit-bang, interrupts off | PIO + DMA |
| Encoder | 2 GPIO interrupts, up to 80 k/s | PIO decoder + DMA, zero interrupts |

The display pins (GP28 clock, GP22 data) are not a legal hardware-SPI pair on
the RP2040, which is exactly why the Arduino build had to bit-bang them. PIO
drives the same pins with no rewiring.

## Building

One-time setup — installs CMake, Ninja, the ARM toolchain and the Pico SDK
under `~/pico`, with no Homebrew and no administrator password:

```bash
./tools/setup_macos.sh   # macOS
./tools/setup_linux.sh   # Linux (x86_64 or aarch64)
```

On Linux, first install `git`, `curl`, `xz`, `python3` and a C/C++ compiler
from your package manager; the script lists anything missing and how to get
it. It uses the system CMake and Ninja if present, otherwise it installs them
into a private venv under `~/pico`. No root needed.

Then, in every new shell:

```bash
source ~/pico/pico-env.sh
```

Build:

```bash
cmake -S . -B build -G Ninja && cmake --build build
```

## Flashing

Hold BOOTSEL while plugging the pad in, then drag `build/hapticpad.uf2` onto
the `RPI-RP2` drive that appears.

After the first flash you never need the BOOTSEL button again: the pad's own
menu has a **Firmware update** entry, and the configurator has a button for
it, both of which drop straight into the bootloader.

## Measured on hardware

With the motor live the FOC loop runs at about **6.8 kHz**, and about 81 kHz
while the screensaver has the motor parked. That is comfortable for haptics on
a 125 MHz Cortex-M0+ with no floating-point unit; the old sketch's claim of
20-30 kHz was never achievable on this part.

`STATUS` reports the live figure, so you can see the cost of any change you
make to the wheel's feel.

## Debugging

The pad exposes a USB serial port. Everything the firmware logs comes out of
it, prefixed with `#`, interleaved with command replies:

```bash
screen /dev/cu.usbmodem* 115200
```

Type `INFO`, `STATUS` or `PROFILE` at it. `STATUS` is the one to reach for
first — it reports the FOC loop rate, the HID queue depth, and how many
reports or encoder ticks have been dropped, which turns "my macro didn't fire"
into a number.

`LOG debug` raises the verbosity; `LOG off` silences the stream.

With a Raspberry Pi Debug Probe attached, `.vscode/launch.json` gives
breakpoint debugging through OpenOCD.

## Files

| Path | What lives there |
| --- | --- |
| `src/board.h` | every pin and hard limit — a board respin touches this file only |
| `src/ipc.c` | the entire core-0 / core-1 boundary |
| `src/haptics.cpp` | core 1: SimpleFOC setup and the feel of each wheel mode |
| `src/hid.c` | the report queue; the only place `tud_hid_report` is called |
| `src/macro.c` | non-blocking macro playback |
| `src/wheel.c` | detent ticks → scrolling or keystrokes |
| `src/profile.c` | profile files → compiled structs |
| `src/ui.c` | screens, menu, screensaver, and what each button means |
| `src/cdc_protocol.c` | the protocol the configurator speaks |
| `pio/*.pio` | display SPI, WS2812, quadrature decoder |
| `compat/Arduino.h` | the small shim that lets SimpleFOC build unmodified |

## SD card layout

```
/config.txt              device settings (written by the firmware)
/profiles/*.txt          one file per profile
/profiles/icons/*.bin    24x24 monochrome icons, 72 bytes each
/icons/*.bin             also searched, for compatibility
```

### Profile format

Existing profiles load unchanged. The additions are `wheel_detents`,
`wheel_strength`, more than three steps per button, and the richer action
vocabulary.

```ini
name=Excel

# LED ring - every key is optional; whatever you leave out follows the
# device-wide setting, so a profile can restate the whole ring or just
# its colour.
led_mode=solid          # off solid rainbow breath bands halo reactive profile
led_primary=#00FF00
led_secondary=#0000FF   # used by 'bands'
led_brightness=180      # 0-255

wheel=clicky            # clicky free ratchet damped 4points off
wheel_func=scroll_h     # scroll scroll_h zoom volume page tabs history
                        # undo_redo arrow timeline custom
wheel_detents=24
wheel_strength=1.5      # omit to use the device-wide setting

btn1_label=Save
btn1_icon=save.bin
btn1_action=CTRL+P,500;ENTER      # up to 8 steps of "action,delay_ms"
```

Older and hand-written spellings are accepted so existing cards keep working:
`color` and `led_color` both mean `led_primary`, `led` means `led_mode`, and
`hap_str` means `wheel_strength`. The configurator rewrites them to the
canonical names the next time it saves the profile.

### How a profile and the device settings combine

The device settings in `/config.txt` are the defaults. A profile overrides
only the keys it actually states — per key, not all-or-nothing. A profile that
sets `led_primary` and nothing else keeps following whichever mode you last
picked in the menu; one that sets all four takes over the ring completely.

The on-device menu shows a value the active profile has taken over **in
brackets**, e.g. `LED mode [solid]`. You can still change the device-wide
value there, but it will not be what you see until you switch to a profile
that leaves that key alone.

### Action vocabulary

| Form | Example | Meaning |
| --- | --- | --- |
| combination | `CTRL+SHIFT+F12` | modifiers plus up to three keys |
| media | `VOL_UP` `MUTE` `PLAY` `NEXT` | consumer control |
| text | `TEXT:hello world` | types the string |
| hold | `HOLD:CTRL` … `RELEASE` | keeps keys down across steps |
| mouse | `MOUSE_LEFT` `SCROLL_UP` | buttons and wheel |
| modified scroll | `CTRL+SCROLL_UP` | how `zoom` is expressed |
| pause | `WAIT` with a delay | a step that only waits |

## On-device controls

- **Macro keys 1–6** run their macros.
- **Either navigation key** switches profile.
- **Both navigation keys together** open and close the settings menu.
- In a menu, the **wheel** or the navigation keys move and change values, and
  **key 1** selects or enters edit mode.
