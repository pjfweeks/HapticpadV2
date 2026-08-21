# Device settings

Device-wide settings live in `/config.txt` on the card. They are reachable
three ways — the on-device menu, the configurator over USB, and editing the
file — and all three go through the same code, so they cannot drift apart.

The firmware writes the file itself; if the card has no `config.txt`, it
creates one with the defaults on first boot.

```ini
# HapticPad configuration
# Written by the firmware; safe to edit by hand or from the configurator.

# --- display ---
contrast=223
display_flip=1
screensaver_sec=120

# --- LED ring ---
# led_mode: off solid rainbow breath bands halo reactive profile
led_mode=rainbow
led_brightness=128
led_primary=#00FF00
led_secondary=#0000FF

# --- haptics ---
haptic_strength=1.00
haptic_detents=24
encoder_invert=0

# --- system ---
log_level=info
last_profile=/profiles/excel.txt
```

| Key | Default | Meaning |
| --- | --- | --- |
| `contrast` | 223 | OLED contrast, 0–255 |
| `display_flip` | 1 | rotate 180°, for a panel mounted upside down (also `flip`) |
| `screensaver_sec` | 120 | blank the screen and park the motor after this many seconds; `0` disables it |
| `led_mode` | `rainbow` | `off solid rainbow breath bands halo reactive profile` |
| `led_brightness` | 128 | 0–255 |
| `led_primary` | `#00FF00` | main colour |
| `led_secondary` | `#0000FF` | second colour, used by `bands` |
| `haptic_strength` | 1.00 | detent torque multiplier |
| `haptic_detents` | 24 | detents per revolution |
| `encoder_invert` | 0 | flip the wheel's direction |
| `log_level` | `info` | `error warn info debug` on the USB serial port |
| `last_profile` | — | written on every profile change, so the pad starts where you left it |

A profile overrides any of the LED and wheel keys for as long as it is active —
see [profile-format.md](profile-format.md#inheritance).

## Changing them

**On the pad.** Press both navigation keys together to open the menu. The
wheel or the navigation keys move and change values; key 1 selects or enters
edit mode. A value the active profile has taken over is shown in brackets.

**From the configurator.** The **Device settings** tab. **Apply** takes effect
immediately, so you can feel a change before committing it; **Save to card**
makes it survive a power cycle.

**From a terminal.**

```bash
screen /dev/cu.usbmodem* 115200
```

```
SET led_brightness 200
SET haptic_detents 36
SAVE
```

`SET` applies at once and `SAVE` writes the file. Without `SAVE` the change is
lost at the next power cycle — the firmware also flushes settings a moment
after you stop turning a value in the menu, so the card is not hammered.

## On-device controls

- **Macro keys 1–6** run their macros.
- **Either navigation key** switches profile.
- **Both navigation keys together** open and close the settings menu.
- In a menu, the **wheel** or the navigation keys move and change values, and
  **key 1** selects or enters edit mode.
- The menu's **Firmware update** entry reboots into the USB bootloader — no
  BOOTSEL button.
