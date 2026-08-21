"""Reading and writing profile files, mirroring the firmware's parser.

Keeping this in step with src/profile.c matters: the configurator must round
trip a file without quietly dropping keys it does not know about, and it must
interpret wheel_func the same way the pad does or the preview lies.
"""

import collections
import re

BUTTON_COUNT = 6
MAX_STEPS = 8

WHEEL_MODES = ["clicky", "free", "ratchet", "damped", "4points", "off"]

LED_MODES = ["off", "solid", "rainbow", "breath", "bands", "halo", "reactive", "profile"]

LED_MODE_HELP = {
    "off":      "ring dark",
    "solid":    "steady primary colour",
    "rainbow":  "hue sweep around the ring",
    "breath":   "primary colour, slow fade",
    "bands":    "primary and secondary, rotating",
    "halo":     "a bright arc that follows the wheel",
    "reactive": "brightness tracks how fast you spin",
    "profile":  "the profile's own colour, steady",
}

# name -> (clockwise action, counter-clockwise action); mirrors wheel_preset()
WHEEL_FUNCS = collections.OrderedDict([
    ("scroll",    ("SCROLL_UP", "SCROLL_DOWN")),
    ("scroll_h",  ("SCROLL_RIGHT", "SCROLL_LEFT")),
    ("zoom",      ("CTRL+SCROLL_UP", "CTRL+SCROLL_DOWN")),
    ("volume",    ("VOL_UP", "VOL_DOWN")),
    ("page",      ("PGDN", "PGUP")),
    ("tabs",      ("CTRL+TAB", "CTRL+SHIFT+TAB")),
    ("history",   ("ALT+RIGHT", "ALT+LEFT")),
    ("undo_redo", ("CTRL+Y", "CTRL+Z")),
    ("arrow",     ("DOWN", "UP")),
    ("timeline",  ("RIGHT", "LEFT")),
    ("custom",    ("", "")),
])

WHEEL_FUNC_HELP = {
    "scroll":    "Vertical mouse wheel",
    "scroll_h":  "Horizontal scroll - Excel columns, video timelines",
    "zoom":      "Ctrl + wheel: zoom in CAD, Photoshop, browsers",
    "volume":    "System volume",
    "page":      "Page Down / Page Up",
    "tabs":      "Next / previous tab",
    "history":   "Forward / back navigation",
    "undo_redo": "Redo / undo",
    "arrow":     "Down / up arrow keys",
    "timeline":  "Right / left arrow keys, frame by frame",
    "custom":    "Whatever you put in the two fields below",
}


class Step(object):
    def __init__(self, action="", delay=0):
        self.action = action
        self.delay = int(delay or 0)

    def text(self):
        return "%s,%d" % (self.action, self.delay) if self.delay else self.action

    def __repr__(self):
        return "Step(%r, %d)" % (self.action, self.delay)


class Button(object):
    def __init__(self):
        self.label = ""
        self.icon = ""
        self.steps = []

    def action_text(self):
        return ";".join(s.text() for s in self.steps if s.action)

    def set_action_text(self, text):
        self.steps = parse_steps(text)


class Profile(object):
    """One profile file.

    The LED fields are all optional: an empty string means "say nothing and
    let the device-wide setting decide", which is exactly how the firmware
    reads a missing key.
    """

    def __init__(self, filename=""):
        self.filename = filename
        self.name = ""
        self.led_mode = ""
        self.led_primary = ""
        self.led_secondary = ""
        self.led_brightness = ""
        self.wheel = "clicky"
        self.wheel_func = "scroll"
        self.detents = ""
        self.strength = ""
        self.wheel_cw = ""
        self.wheel_ccw = ""
        self.buttons = [Button() for _ in range(BUTTON_COUNT)]
        # Keys we did not recognise, preserved verbatim so hand-written extras
        # survive a round trip through the editor.
        self.extra = []

    def led_overrides(self):
        """Which parts of the ring this profile takes over."""
        return {
            "mode": bool(self.led_mode),
            "primary": bool(self.led_primary),
            "secondary": bool(self.led_secondary),
            "brightness": str(self.led_brightness) != "",
        }

    def effective_wheel_actions(self):
        """What the pad will actually do, presets and overrides combined."""
        cw, ccw = WHEEL_FUNCS.get(self.wheel_func, ("", ""))
        return (self.wheel_cw or cw, self.wheel_ccw or ccw)


def parse_steps(text):
    """Splits "CTRL+P,500;ENTER" the way the firmware's parse_steps() does."""
    steps = []
    for segment in text.split(";"):
        segment = segment.strip()
        if not segment:
            continue

        delay = 0
        if not segment.upper().startswith("TEXT:"):
            # Only the last comma separates the delay; TEXT payloads keep theirs.
            head, sep, tail = segment.rpartition(",")
            if sep and re.match(r"^\s*\d+\s*$", tail):
                segment, delay = head.strip(), int(tail)

        steps.append(Step(segment, delay))
        if len(steps) >= MAX_STEPS:
            break
    return steps


def parse(text, filename=""):
    profile = Profile(filename)

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";") or line.startswith("["):
            continue
        if "=" not in line:
            continue

        key, _, value = line.partition("=")
        key = key.strip().lower()
        value = value.strip()

        if key == "name":
            profile.name = value
        elif key in ("color", "colour", "led_primary", "led_color", "led_colour"):
            # `color` is what older profiles use; it has always meant the
            # primary colour, so the two are the same key.
            profile.led_primary = value
        elif key == "led_secondary":
            profile.led_secondary = value
        elif key in ("led_mode", "led"):
            profile.led_mode = value.lower()
        elif key == "led_brightness":
            profile.led_brightness = value
        elif key in ("wheel", "wheel_mode"):
            profile.wheel = value.lower()
        elif key == "wheel_func":
            profile.wheel_func = value.lower()
        elif key in ("wheel_detents", "detents"):
            profile.detents = value
        elif key in ("wheel_strength", "hap_str", "haptic_strength"):
            profile.strength = value
        elif key == "wheel_cw":
            profile.wheel_cw = value
        elif key == "wheel_ccw":
            profile.wheel_ccw = value
        else:
            match = re.match(r"^btn(\d+)_(label|icon|action)$", key)
            if match:
                index = int(match.group(1)) - 1
                if 0 <= index < BUTTON_COUNT:
                    field = match.group(2)
                    if field == "label":
                        profile.buttons[index].label = value
                    elif field == "icon":
                        profile.buttons[index].icon = value
                    else:
                        profile.buttons[index].set_action_text(value)
                    continue
            profile.extra.append(raw.rstrip())

    return profile


def serialize(profile):
    out = []
    out.append("# %s" % (profile.name or profile.filename or "HapticPad profile"))
    out.append("# Written by the HapticPad configurator.")
    out.append("")
    out.append("name=%s" % profile.name)

    if any(profile.led_overrides().values()):
        out.append("")
        out.append("# LED ring - anything omitted follows the device setting")
        if profile.led_mode:
            out.append("led_mode=%s" % profile.led_mode)
        if profile.led_primary:
            out.append("led_primary=%s" % profile.led_primary)
        if profile.led_secondary:
            out.append("led_secondary=%s" % profile.led_secondary)
        if str(profile.led_brightness) != "":
            out.append("led_brightness=%s" % profile.led_brightness)

    out.append("")
    out.append("wheel=%s" % profile.wheel)
    out.append("wheel_func=%s" % profile.wheel_func)
    if profile.detents:
        out.append("wheel_detents=%s" % profile.detents)
    if profile.strength:
        out.append("wheel_strength=%s" % profile.strength)
    if profile.wheel_cw:
        out.append("wheel_cw=%s" % profile.wheel_cw)
    if profile.wheel_ccw:
        out.append("wheel_ccw=%s" % profile.wheel_ccw)

    for i, button in enumerate(profile.buttons, start=1):
        action = button.action_text()
        if not (button.label or button.icon or action):
            continue
        out.append("")
        out.append("# button %d" % i)
        out.append("btn%d_label=%s" % (i, button.label))
        if button.icon:
            out.append("btn%d_icon=%s" % (i, button.icon))
        if action:
            out.append("btn%d_action=%s" % (i, action))

    if profile.extra:
        out.append("")
        out.append("# preserved from the original file")
        out.extend(profile.extra)

    out.append("")
    return "\n".join(out)
