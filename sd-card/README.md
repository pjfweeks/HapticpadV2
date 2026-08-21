# Starter SD card

Copy everything in this folder onto a freshly FAT32-formatted microSD, insert
it, and the pad boots with a working example profile.

```
config.txt              device settings      -> ../docs/configuration.md
profiles/*.txt          one file per profile -> ../docs/profile-format.md
profiles/icons/*.bin    24x24 icons, 72 bytes each
```

From then on the card can stay in the device: the configurator reads and writes
it over the USB cable, and the firmware keeps `config.txt` up to date.

Do not copy this `README.md` onto the card — the firmware ignores it, but there
is no reason for it to be there.
