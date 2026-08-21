# Prebuilt firmware

`hapticpad-2.0.0.uf2` is the build that was tested on hardware.

**Flashing:** hold BOOTSEL while plugging the pad in, then drag the file onto
the `RPI-RP2` drive that appears. After this first flash you never need BOOTSEL
again — the pad's menu has a **Firmware update** entry, and so does the
configurator.

```
SHA-256  a4a0267e87d04001f93c39c1ed671d1fffe7e6346753b31564a819b01da749db
size     452096 bytes
built    Pico SDK 2.1.1, arm-none-eabi-gcc 14.2
```

A build you make yourself will not match this checksum byte for byte — the
absolute source paths are embedded in the image — but it is the same firmware.
