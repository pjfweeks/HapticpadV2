# Contributing

Bug reports are as welcome as patches, and for this project a good bug report
is worth more.

## Reporting a bug

Include:

- the output of `INFO` and `STATUS` from the serial port (see
  [docs/serial-protocol.md](docs/serial-protocol.md)) — `STATUS` reports the
  FOC loop rate, the HID queue depth and any dropped reports, which usually
  says more than a description does;
- the profile file involved, if it is about a macro or the wheel;
- **the steps to reproduce it.** A recipe someone else can follow is what makes
  a bug fixable.

[docs/troubleshooting.md](docs/troubleshooting.md) covers the things that turn
out not to be bugs.

## Working on the firmware

Set the toolchain up once with `firmware/tools/setup_macos.sh`, then

```bash
source ~/pico/pico-env.sh
cmake -S firmware -B firmware/build -G Ninja && cmake --build firmware/build
```

A few conventions worth keeping:

- **Pins and limits live in `src/board.h`** and nowhere else, so a board
  revision touches one file.
- **Only core 0 writes HID reports.** Everything that produces keystrokes goes
  through the queue in `src/hid.c`. This is what v2 exists to fix; please do
  not reintroduce a second writer.
- **Nothing blocks.** Core 0 is a cooperative loop of short tasks — no
  `sleep_ms` in a task, no busy waits. Delays belong in the HID queue.
- **Nothing is allocated.** Buffers are static and sized from `board.h`.
- Core 1 must not touch USB, the SD card, the display or the heap.

Check the FOC rate with `STATUS` after anything that touches core 1 or the
timing of core 0.

## Working on the configurator

```bash
python3 configurator/selftest.py
```

Covers the profile round trip, the icon converter, the OLED preview and the web
API including token enforcement. No hardware needed, and it should stay that
way — it is what makes the app safe to refactor.

Standard library only. pyserial and Pillow are optional extras and the app must
run without them: a stock macOS Python is the target.

The font in `hp/font5x7.py` is generated from `firmware/src/font5x7.h` — keep
the two in step, or the preview stops matching the device.

## Anything touching the profile format

Three parsers have to agree: `firmware/src/profile.c`, `hp/profiles.py`, and
the documentation in [docs/profile-format.md](docs/profile-format.md). Update
all three, and keep the old spellings accepted so existing cards keep working.

## Licence

Contributions are accepted under the CERN Open Hardware Licence v2 — Strongly
Reciprocal, the licence of the project.
