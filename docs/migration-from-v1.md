# Upgrading a v1 pad

Nothing to unsolder and nothing to rewire: v2 runs on the v1 hardware as built.
What changes is the firmware, the card layout and the way you configure it.

## 1. Back up the card

Copy the whole card to your computer before you touch it. `config.xml` is the
only copy of your macros, and the v2 firmware does not read it.

## 2. Flash

Hold BOOTSEL while plugging the pad in and drag
[`firmware/release/hapticpad-2.0.0.uf2`](../firmware/release/) onto the
`RPI-RP2` drive. This is the last time you need BOOTSEL — afterwards the pad's
own menu has a **Firmware update** entry, and so does the configurator.

To go back to v1, flash the old `MacroPad.UF2` the same way. The firmware is
the only thing that changes; nothing about the upgrade is one-way.

## 3. Rebuild the card

Copy the contents of [`sd-card/`](../sd-card/) onto a FAT32 card. The layout is
different:

| v1 | v2 |
| --- | --- |
| `/config.xml` — settings *and* every profile in one XML file | `/config.txt` for settings, one `.txt` per profile in `/profiles/` |
| BMP images per profile folder | 72-byte `.bin` icons in `/profiles/icons/` |

Translating a profile is mechanical. This v1 profile:

```xml
<Profile name="Solidworks">
  <WheelMode>Clicky</WheelMode>
  <WheelKey>17</WheelKey>
  <MacroButtons>
    <MacroButton>
      <Action>0,68</Action>
      <Action>0,0</Action>
      <Action>0,0</Action>
      <Label>Dimension</Label>
    </MacroButton>
    ...
```

becomes `/profiles/solidworks.txt`:

```ini
name=Solidworks
wheel=clicky
wheel_func=scroll

btn1_label=Dimension
btn1_action=F11,0
```

Two things to watch:

- **Keycodes become names.** v1 took numeric HID keycodes from a lookup site;
  v2 takes `F11`, `CTRL+SHIFT+P`, `TEXT:hello`. See the
  [action vocabulary](profile-format.md#action-vocabulary).
- **`<WheelKey>` becomes a function.** A held modifier plus scrolling is now a
  preset — `zoom` is `CTRL+SCROLL_UP`/`CTRL+SCROLL_DOWN`, and anything else can
  be written out with `wheel_cw` and `wheel_ccw`.

The `<Settings>` block maps onto `config.txt` key by key: LED mode and colours
keep their meanings, and the P/I tuning values are replaced by
`haptic_strength` and `haptic_detents` — the wheel's feel is a torque curve per
mode now, not a PID to tune.

If you already have v2-style `.txt` profiles, they load unchanged: `color`,
`led_color`, `led` and `hap_str` are all still accepted, and icons are searched
in `/icons/` as well as `/profiles/icons/`.

## 4. Regenerate your icons

**Every `.bin` icon on the original card was 72 bytes of zeros** — the v1
converter thresholded white-on-transparent artwork by brightness and silently
produced nothing. If your pad showed blank icon cells, this is why.

Re-import them from the configurator's **Icons** tab: it works out whether the
shape is the opaque part of the artwork or a dark glyph on a light background,
previews the result at 24×24, and tells you when a conversion came out empty.
`assets/icons-png/` holds 190+ icons ready to import.

## 5. What you gain

Beyond the three bugs that motivated the rewrite — dropped macros, broken zoom,
`delay()` freezing the whole device — the parts you will notice first:

- macros of up to 8 steps, with media keys, typed text, held modifiers and
  mouse actions
- six wheel feel modes and twelve function presets, per profile
- a settings menu on the device, and a configurator that edits profiles,
  macros and icons over USB with a live preview of the pad's screen
- a screensaver that parks the motor
- `STATUS` over the serial port, so a misfiring macro becomes a number

The full list is in [CHANGELOG.md](../CHANGELOG.md).
