# Architecture

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

The two cores share exactly three things, all in `src/ipc.c` and all
lock-free: a single-producer ring of wheel ticks, and two seqlock-protected
structs for commands going down and telemetry coming up.

Core 1 never touches USB, the card, the display or the heap. Nothing the
application does can stutter the motor, and nothing the motor does can corrupt
a keystroke. In v1 both cores called `sendKeystroke()` into a non-reentrant
TinyUSB and allocated `String` objects on a shared heap — which is how macros
went missing.

Core 0 is one cooperative loop of short non-blocking tasks. Macro delays are
carried in the HID queue rather than in `delay()` calls, so a macro with
half-second pauses plays out while the display keeps redrawing.

Nothing is `malloc`'d: every buffer is static and sized by the limits at the
bottom of `src/board.h`.

## What PIO bought us

| Job | v1 | v2 |
| --- | --- | --- |
| Display | software SPI, ~30 ms/frame of blocked CPU | PIO + DMA, ~1 ms, CPU free |
| LED ring | FastLED bit-bang, interrupts off | PIO + DMA |
| Encoder | 2 GPIO interrupts, up to 80 k/s | PIO decoder + DMA, zero interrupts |

The display pins (GP28 clock, GP22 data) are not a legal hardware-SPI pair on
the RP2040, which is exactly why the Arduino build had to bit-bang them. PIO
drives the same pins with no rewiring.

PIO and DMA claims are fixed up front in `board.h` so two subsystems can never
grab the same state machine. `pio0/sm0` is left free as headroom.

## Measured on hardware

With the motor live the FOC loop runs at about **6.8 kHz**, and about **81 kHz**
while the screensaver has the motor parked. That is comfortable for haptics on
a 125 MHz Cortex-M0+ with no floating-point unit; v1's claim of 20–30 kHz was
never achievable on this part.

`STATUS` reports the live figure, so you can see the cost of any change you
make to the wheel's feel.

## Source map

| Path | What lives there |
| --- | --- |
| `src/board.h` | every pin and hard limit — a board respin touches this file only |
| `src/ipc.c` | the entire core-0 / core-1 boundary |
| `src/haptics.cpp` | core 1: SimpleFOC setup and the feel of each wheel mode |
| `src/hid.c` | the report queue; the only place `tud_hid_report` is called |
| `src/macro.c` | non-blocking macro playback |
| `src/wheel.c` | detent ticks → scrolling or keystrokes |
| `src/profile.c` | profile files → compiled structs |
| `src/settings.c` | `/config.txt`, shared by the menu, USB and the parser |
| `src/ui.c` | screens, menu, screensaver, and what each button means |
| `src/cdc_protocol.c` | the protocol the configurator speaks |
| `src/storage.c` | FatFs on SPI0 |
| `src/ssd1309.c`, `src/gfx.c` | panel driver and drawing |
| `src/keycodes.c` | the action vocabulary → HID usages |
| `pio/*.pio` | display SPI, WS2812, quadrature decoder |
| `compat/Arduino.h` | the small shim that lets SimpleFOC build unmodified |

## Pin map

Inherited from the v1 PCB and not free to change without respinning the board;
`src/board.h` is the authority.

| Function | Pins |
| --- | --- |
| Display (SSD1309, PIO SPI) | SCK 28, MOSI 22, CS 6, DC 7, RST 8 |
| microSD (hardware SPI0) | SCK 2, MOSI 3, MISO 4, CS 5 |
| LED ring (WS2812, PIO) | 15 — 20 LEDs |
| Motor (TMC6300, 6-PWM) | U 20/21, V 18/19, W 16/17 |
| Encoder (PIO quadrature) | B 26, A 27 — 1024 PPR |
| Buttons 1–6 | 9, 1, 0, 12, 11, 10 |
| Navigation next / prev | 13 / 14 |
