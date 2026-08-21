# Profile format

One profile is one plain-text file in `/profiles/` on the card, `key=value`
per line, `#` starts a comment. Up to 32 profiles are loaded. The file name is
what you pass to `PROFILE` over USB; the `name=` key is what the screen shows.

```ini
name=Excel

# --- LED ring (every key optional — see "Inheritance" below) ---
led_mode=solid          # off solid rainbow breath bands halo reactive profile
led_primary=#00FF00
led_secondary=#0000FF   # used by 'bands'
led_brightness=180      # 0-255

# --- wheel ---
wheel=clicky            # off free clicky ratchet damped 4points
wheel_func=scroll_h     # preset, see the table below
wheel_detents=24
wheel_strength=1.5

# --- keys 1-6 ---
btn1_label=Save         # up to 12 characters
btn1_icon=save.bin      # 24x24 monochrome, in /profiles/icons/
btn1_action=CTRL+P,500;ENTER
```

## Keys

| Key | Values | Notes |
| --- | --- | --- |
| `name` | text | shown on screen, up to 20 characters |
| `led_mode` | `off solid rainbow breath bands halo reactive profile` | also accepted as `led` |
| `led_primary` | `#RRGGBB` or `0xRRGGBB` | also accepted as `color`, `colour`, `led_color`, `led_colour` |
| `led_secondary` | `#RRGGBB` | used by `bands` |
| `led_brightness` | 0–255 | |
| `wheel` | `off free clicky ratchet damped 4points` | also accepted as `wheel_mode` |
| `wheel_func` | preset name or `custom` | see below |
| `wheel_detents` | number of clicks per turn | also accepted as `detents` |
| `wheel_strength` | detent torque, e.g. `1.5` | also accepted as `hap_str`, `haptic_strength` |
| `wheel_cw` / `wheel_ccw` | an action | overrides the preset — this is what `custom` means |
| `btnN_label` | text, N = 1–6 | up to 12 characters |
| `btnN_icon` | file name | looked up in `/profiles/icons/`, then `/icons/` |
| `btnN_action` | up to 8 steps | `action,delay_ms` separated by `;` |

## Wheel feel modes

| Mode | Feel |
| --- | --- |
| `off` | motor idle, wheel spins freely and sends nothing |
| `free` | free spin, no detents, still reports movement |
| `clicky` | crisp detent at every step |
| `ratchet` | turns one way, resists the other |
| `damped` | smooth resistance, no clicks |
| `4points` | four hard stops per revolution |

`wheel_strength` scales the detent torque; `wheel_detents` sets how many
detents fill one revolution.

## Wheel function presets

| Preset | Clockwise | Counter-clockwise |
| --- | --- | --- |
| `scroll` | `SCROLL_UP` | `SCROLL_DOWN` |
| `scroll_h` (`scroll_x`) | `SCROLL_RIGHT` | `SCROLL_LEFT` |
| `zoom` | `CTRL+SCROLL_UP` | `CTRL+SCROLL_DOWN` |
| `volume` | `VOL_UP` | `VOL_DOWN` |
| `page` | `PGDN` | `PGUP` |
| `tabs` | `CTRL+TAB` | `CTRL+SHIFT+TAB` |
| `history` | `ALT+RIGHT` | `ALT+LEFT` |
| `undo_redo` | `CTRL+Y` | `CTRL+Z` |
| `arrow` | `DOWN` | `UP` |
| `arrow_h` | `RIGHT` | `LEFT` |
| `timeline` | `RIGHT` | `LEFT` |
| `custom` | whatever `wheel_cw` says | whatever `wheel_ccw` says |

A preset is only a shorthand for a pair of actions, so `wheel_cw=` and a preset
are the same thing to everything downstream.

## Action vocabulary

| Form | Example | Meaning |
| --- | --- | --- |
| combination | `CTRL+SHIFT+F12` | modifiers plus up to three keys |
| media | `VOL_UP` `MUTE` `PLAY` `NEXT` | consumer control |
| text | `TEXT:hello world` | types the string, up to 64 characters |
| hold | `HOLD:CTRL` … `RELEASE` | keeps keys down across later steps |
| mouse | `MOUSE_LEFT` `SCROLL_UP` | buttons and wheel |
| modified scroll | `CTRL+SCROLL_UP` | how `zoom` is expressed |
| pause | `WAIT` with a delay | a step that only waits |

### Steps

`btnN_action` is up to 8 steps separated by `;`, each `action,delay_ms` where
the delay is applied **before** the action:

```ini
btn1_action=CTRL+P,0;WAIT,500;ENTER,0
btn2_action=HOLD:CTRL,0;C,20;V,20;RELEASE,0
btn3_action=TEXT:lorenzo@example.com,0
```

Delays do not block anything: the display, the LEDs and the wheel keep running
while a macro plays out.

## Inheritance

The device settings in `/config.txt` are the defaults. A profile overrides
**only the keys it states**, per key. A profile with just `led_primary` keeps
whichever mode you last picked in the menu; one that sets mode, both colours
and brightness takes over the ring completely.

The on-device menu shows a value the active profile has taken over **in
brackets** — `LED mode [solid]`. You can still change the device-wide value
there, but you will not see it until you switch to a profile that leaves that
key alone.

## Icons

24×24 pixels, monochrome, XBM bit order, exactly 72 bytes. Put them in
`/profiles/icons/`. The configurator's **Icons** tab converts any PNG, BMP —
and, with Pillow installed, JPEG and GIF — and previews the result at 24×24
before you write it. `assets/icons-png/` in this repository holds 190+ ready
icons to import.
