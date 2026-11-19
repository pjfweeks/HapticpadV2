# Troubleshooting

Start here:

```bash
screen /dev/cu.usbmodem* 115200
```

`STATUS` reports the FOC loop rate, the HID queue depth and how many reports or
encoder ticks have been dropped. `INFO` reports the firmware version and what
the card looks like. `LOG debug` turns up the detail. That turns most of the
questions below into a number instead of a guess.

## The pad does nothing when plugged in

- The screen stays black after the screensaver timeout — press a key.
- Nothing at all: reflash. Hold BOOTSEL while plugging it in, drag
  `firmware/release/hapticpad-2.0.0.uf2` onto the `RPI-RP2` drive.
- No `RPI-RP2` drive appears: try another cable. Charge-only USB cables are the
  most common cause.

## No profiles, or the screen says the card is missing

`INFO` says whether the card mounted. If it did not:

- Format FAT32, not exFAT.
- Check the SD reader's orientation and its power and ground pins against the
  labels on the controller board.
- Confirm `/profiles/` exists at the root and holds at least one `.txt`.

`LIST /profiles` lists what the firmware actually sees.

## A macro does not fire

- `TEST CTRL+C` makes the pad send that action right now. If `TEST` works and
  the button does not, the problem is in the profile file, not in USB.
- `STATUS` reports dropped reports. If the count is climbing, say so in an
  issue — that should not happen in v2.
- Check the action spelling against the
  [vocabulary](profile-format.md#action-vocabulary). An unparseable action is
  logged at boot; `LOG debug` then `RESCAN` shows it.
- A macro of more than 8 steps is truncated at 8.

## The icons are blank

Almost certainly icons carried over from a v1 card: they were 72 bytes of zeros
there too. Re-import them from the configurator's **Icons** tab, which previews
the conversion and warns when the result is empty. See
[migration-from-v1.md](migration-from-v1.md#4-regenerate-your-icons).

Also check that the file is exactly 72 bytes and that `btnN_icon=` names it
without a path — it is looked up in `/profiles/icons/`, then `/icons/`.

## The wheel turns the wrong way

Set `encoder_invert=1` in `config.txt`, or flip it in the menu.

## The wheel feels weak, or buzzes, or fights you

- `wheel_strength` (per profile) and `haptic_strength` (device-wide) scale the
  detent torque.
- A wheel that hums while idle usually means the motor wires are in an order
  the startup alignment resolved oddly — swap two of the U/V/W wires.
- `STATUS`'s FOC rate should be around 6.8 kHz with the motor live. Much lower
  means core 1 is being starved; open an issue with what you changed.

## The screen is black or garbled, but the LEDs work

- Check the display's VCC: it must be **3.3 V**, not 5 V. At 5 V its level
  shifter barely registers the RP2040's 3.3 V signals, which gives a black
  screen on some boots and drifting, slanted fragments of text on others. See
  [hardware.md](hardware.md#connecting-the-two-boards).
- `STATUS` reporting `screen 1` only means the firmware is drawing; the display
  link is one-way, so the firmware cannot tell whether the panel received it.
- Check continuity with the power off. Measured powered, the meter can report
  connections between pins that are not really there.

## The screen is upside down, or too dim

`display_flip` and `contrast` in `config.txt`, or the menu.

## The configurator does not see the pad

- Only one program can hold the serial port. Close any `screen` session first.
- On Windows, install pyserial: `python3 -m pip install --user pyserial`.
- On Linux, add yourself to the serial-port group and log back in: `dialout`
  on Debian, Ubuntu and Fedora, `uucp` on Arch.
- `python3 configurator/selftest.py` checks everything that does not need
  hardware, which separates an app problem from a connection problem.

## The firmware will not build

- Run `firmware/tools/setup_macos.sh` (macOS) or
  `firmware/tools/setup_linux.sh` (Linux) — it installs CMake, Ninja, the ARM
  toolchain and the Pico SDK under `~/pico` without Homebrew or a password.
- On Linux, if `pip` fails with "externally-managed-environment", you are
  running an old copy of the setup instructions; `setup_linux.sh` installs
  into a venv instead. On Debian/Ubuntu, `python3 -m venv` also needs the
  `python3-venv` package.
- `source ~/pico/pico-env.sh` in every new shell.
- After changing branches or the SDK, delete `firmware/build/` and configure
  again.

## Reporting a bug

Include the `INFO` and `STATUS` output, the profile file involved, and the
steps that reproduce it. A recipe someone else can follow is what makes a bug
fixable.
