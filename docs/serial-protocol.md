# USB serial protocol

The pad exposes a USB CDC serial port alongside its HID interfaces. The
configurator speaks this protocol, and so can you:

```bash
screen /dev/cu.usbmodem* 115200
```

It is deliberately line-based ASCII rather than a binary framing, which makes
both the firmware and the configurator far easier to debug. Binary payloads —
icons, profile files — travel as base64 so the transport stays text.

## Shape of an exchange

Every command answers with zero or more `+` data lines, then exactly one
terminator: `OK` (optionally with text) or `ERR <reason>`.

Lines beginning with `#` are asynchronous log output and can appear at any
moment; a client that ignores them still sees a well-formed exchange.

```
> STATUS
+ foc_hz=6800
+ hid_queue=0
+ dropped=0
OK
```

## Commands

| Command | Effect |
| --- | --- |
| `PING` | identity handshake |
| `INFO` | device, storage and firmware facts |
| `STATUS` | live telemetry: wheel, queues, screen |
| `GET <key>` | read one setting |
| `SET <key> <value>` | change one setting, applied immediately |
| `SAVE` | flush settings to the card now |
| `PROFILES` | list profile files |
| `PROFILE <file>` | make that profile active |
| `RESCAN` | re-read the card and reload the active profile |
| `LIST <dir>` | directory listing |
| `READ <path>` | file contents, base64 |
| `WRITE <path>` | base64 lines follow, terminated by a line containing `.` |
| `DELETE <path>` | remove a file |
| `MKDIR <path>` | create a directory |
| `TEST <action>` | perform one action now, e.g. `TEST CTRL+C` |
| `LOG <off\|error\|warn\|info\|debug>` | verbosity of the `#` stream |
| `BOOTSEL` | reboot into the USB firmware bootloader |
| `REBOOT` | restart the firmware |

The authoritative list is the comment block at the top of
[`firmware/src/cdc_protocol.h`](../firmware/src/cdc_protocol.h).

## Diagnostics

`STATUS` is the one to reach for first. It reports the FOC loop rate, the HID
queue depth, and how many reports or encoder ticks have been dropped — which
turns "my macro didn't fire" into a number.

`LOG debug` raises the verbosity, `LOG off` silences the stream.

## Settings keys

`GET` and `SET` accept the keys in [configuration.md](configuration.md).
