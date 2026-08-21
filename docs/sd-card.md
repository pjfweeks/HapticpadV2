# SD card

A FAT32 microSD holds the profiles, the icons and the device settings. Copy the
contents of [`sd-card/`](../sd-card/) onto a freshly formatted card and you have
a working pad; from then on the firmware and the configurator maintain it for
you, with the card left in the device.

## Layout

```
/config.txt              device settings, written by the firmware
/profiles/*.txt          one file per profile, up to 32
/profiles/icons/*.bin    24x24 monochrome icons, 72 bytes each
/icons/*.bin             also searched, for v1 compatibility
```

- [`config.txt`](../sd-card/config.txt) — every device-wide setting, documented
  in [configuration.md](configuration.md). Delete it and the firmware writes a
  fresh one with the defaults at the next boot.
- [`profiles/`](../sd-card/profiles/) — see
  [profile-format.md](profile-format.md). The file name is the identifier; the
  `name=` line is what the screen shows.
- `profiles/icons/` — 72-byte monochrome bitmaps. Import your own from the
  configurator's **Icons** tab; `assets/icons-png/` in this repository holds
  190+ ready to convert.

## What the firmware writes

- `config.txt` — on `SAVE`, on **Save to card**, and a moment after you stop
  changing a value in the on-device menu.
- `last_profile=` inside `config.txt` — on every profile change, so the pad
  starts on whichever profile was last active.

Nothing else on the card is touched unless you ask for it, so anything else you
keep there survives.

## Editing without the configurator

The files are plain text, so the card can be edited in any editor. After
changing files with the card still in the pad, either power-cycle it or send
`RESCAN` over the serial port to re-read the card and reload the active profile.

## Coming from a v1 card

v1 kept a single `config.xml` and BMP images. The v2 firmware does not read
`config.xml`; see [migration-from-v1.md](migration-from-v1.md) for what to
carry across.
