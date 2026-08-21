#!/usr/bin/env python3
"""Checks the parts of the configurator that do not need a pad attached.

    python3 selftest.py

Covers the profile round trip, the icon converter, the OLED preview and the
web server's API, including the token check that keeps other pages on your
machine from driving the pad.  Run it after touching anything under hp/.
"""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hp import images, preview, profiles

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

failures = []


def check(name, condition, detail=""):
    print("%-46s %s" % (name, "ok" if condition else "FAIL"))
    if not condition:
        failures.append("%s %s" % (name, detail))


def test_profile_roundtrip():
    for path in sorted(glob.glob(os.path.join(REPO, "profiles", "*.txt"))):
        name = os.path.basename(path)
        original = profiles.parse(open(path).read(), name)
        reparsed = profiles.parse(profiles.serialize(original), name)

        same = (original.name == reparsed.name
                and original.wheel == reparsed.wheel
                and original.wheel_func == reparsed.wheel_func
                and original.led_primary == reparsed.led_primary
                and all(a.label == b.label and a.icon == b.icon
                        and a.action_text() == b.action_text()
                        for a, b in zip(original.buttons, reparsed.buttons)))
        check("profile round trip: %s" % name, same)


def test_led_overrides():
    """A profile may take over any part of the ring, or none of it."""
    blank = profiles.parse("name=Plain\n", "plain.txt")
    check("a profile with no LED keys inherits everything",
          not any(blank.led_overrides().values()))

    # `color` is the key the old firmware and hand-written files use.
    legacy = profiles.parse("name=Old\ncolor=0x00FF00\n", "old.txt")
    check("legacy color= still means the primary colour",
          legacy.led_primary == "0x00FF00" and legacy.led_overrides()["primary"])

    full = profiles.parse(
        "name=Full\nled_mode=bands\nled_primary=#FF0000\n"
        "led_secondary=#0000FF\nled_brightness=200\n", "full.txt")
    check("a profile can take over the whole ring",
          all(full.led_overrides().values()))

    again = profiles.parse(profiles.serialize(full), "full.txt")
    check("LED settings survive a round trip",
          (again.led_mode, again.led_primary, again.led_secondary, again.led_brightness)
          == ("bands", "#FF0000", "#0000FF", "200"),
          repr((again.led_mode, again.led_primary, again.led_secondary, again.led_brightness)))

    partial = profiles.parse("name=Half\nled_mode=solid\n", "half.txt")
    written = profiles.serialize(partial)
    check("an unset LED field is not written out",
          "led_mode=solid" in written and "led_primary" not in written)

    bad = profiles.parse("name=Bad\nled_mode=nonsense\n", "bad.txt")
    check("an unknown LED mode is kept for the firmware to reject",
          bad.led_mode == "nonsense")


def test_step_parsing():
    cases = [
        ("CTRL+P,500;ENTER", [("CTRL+P", 500), ("ENTER", 0)]),
        ("CTRL+C", [("CTRL+C", 0)]),
        ("TEXT:hello, world", [("TEXT:hello, world", 0)]),
        ("SHIFT+F12,10", [("SHIFT+F12", 10)]),
        ("HOLD:CTRL;A;RELEASE", [("HOLD:CTRL", 0), ("A", 0), ("RELEASE", 0)]),
    ]
    for text, expected in cases:
        steps = profiles.parse_steps(text)
        got = [(s.action, s.delay) for s in steps]
        check("steps: %s" % text, got == expected, "got %r" % (got,))


def test_icons():
    sources = sorted(glob.glob(os.path.join(REPO, "assets", "icons-png", "*.png")))[:25]
    check("icon sources found", len(sources) > 0)

    blank = []
    for path in sources:
        data, _mode = images.to_icon(images.load_image(path))
        if len(data) != images.ICON_BYTES:
            blank.append(os.path.basename(path) + " (wrong size)")
        elif images.icon_coverage(data) < 0.01:
            blank.append(os.path.basename(path))
    check("%d icons convert to something visible" % len(sources),
          not blank, "blank: %s" % ", ".join(blank))

    on_card = sorted(glob.glob(os.path.join(REPO, "sd-card", "profiles", "icons", "*.bin")))
    empty = [os.path.basename(p) for p in on_card
             if images.icon_coverage(open(p, "rb").read()) < 0.01]
    check("%d icons on the card are not blank" % len(on_card),
          not empty, "blank: %s" % ", ".join(empty))


def test_preview():
    path = os.path.join(REPO, "sd-card", "profiles", "excel.txt")
    if not os.path.exists(path):
        return
    profile = profiles.parse(open(path).read(), "excel.txt")

    icon_data = {}
    for button in profile.buttons:
        if not button.icon:
            continue
        source = os.path.join(REPO, "sd-card", "profiles", "icons", button.icon)
        if os.path.exists(source):
            icon_data[button.icon] = open(source, "rb").read()

    canvas = preview.render_profile(profile, icon_data, 0, 2)
    lit = sum(sum(1 for v in row if v) for row in canvas.rows)
    check("preview renders %d lit pixels" % lit,
          preview.WIDTH * 8 < lit < preview.WIDTH * preview.HEIGHT)


def test_webserver():
    """Starts the real server and exercises the endpoints that need no pad.

    This replaced a Tk contrast check: the interface moved to the browser
    because macOS's Tk 8.5.9 could not paint a window at all.
    """
    import base64
    import json
    import re
    import tempfile
    import threading
    import urllib.error
    import urllib.request

    from hp import webserver

    server = webserver.Server(("127.0.0.1", 0), tempfile.mkdtemp(prefix="hapticpad-test-"))
    base = "http://127.0.0.1:%d" % server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    def request(path, payload=None, token=None):
        req = urllib.request.Request(base + path)
        if token:
            req.add_header("X-HapticPad-Token", token)
        if payload is not None:
            req.add_header("Content-Type", "application/json")
            req.data = json.dumps(payload).encode()
        try:
            with urllib.request.urlopen(req, timeout=10) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()

    try:
        status, body = request("/")
        page = body.decode()
        check("web UI is served", status == 200 and "HapticPad Configurator" in page)

        match = re.search(r'window\.HP_TOKEN = "([^"]+)"', page)
        token = match.group(1) if match else ""
        check("a real token is injected", bool(token) and token != "__TOKEN__")

        check("stylesheet and script are served",
              request("/app.css")[0] == 200 and request("/app.js")[0] == 200)

        # The token is what stops a random web page from driving the pad.
        check("API refuses a missing token", request("/api/ports")[0] == 403)
        check("API refuses a wrong token", request("/api/ports", token="wrong")[0] == 403)
        check("API accepts the real token", request("/api/ports", token=token)[0] == 200)

        status, body = request("/api/vocabulary", token=token)
        vocabulary = json.loads(body)
        check("vocabulary matches the firmware",
              vocabulary["buttons"] == profiles.BUTTON_COUNT
              and vocabulary["max_steps"] == profiles.MAX_STEPS)

        path = os.path.join(REPO, "sd-card", "profiles", "excel.txt")
        parsed = profiles.parse(open(path).read(), "excel.txt")
        status, body = request("/api/preview",
                               {"profile": webserver.profile_to_json(parsed),
                                "index": 0, "total": 1}, token)
        rows = json.loads(body).get("rows", [])
        lit = sum(row.count("1") for row in rows)
        check("preview renders over the API", status == 200 and len(rows) == 64 and lit > 500,
              "rows=%d lit=%d" % (len(rows), lit))

        sources = sorted(glob.glob(os.path.join(REPO, "assets", "icons-png", "*.png")))
        if sources:
            with open(sources[0], "rb") as handle:
                encoded = base64.b64encode(handle.read()).decode()
            status, body = request("/api/icon/convert",
                                   {"image": encoded, "mode": "auto", "threshold": 128}, token)
            converted = json.loads(body)
            check("icon converts over the API",
                  status == 200 and len(base64.b64decode(converted["data"])) == images.ICON_BYTES
                  and converted["coverage"] > 0.01)

        status, body = request("/api/profiles", token=token)
        check("a device call with no pad fails cleanly",
              status == 400 and "not connected" in json.loads(body)["error"])
    finally:
        server.shutdown()
        server.server_close()


def main():
    test_profile_roundtrip()
    test_led_overrides()
    test_step_parsing()
    test_icons()
    test_preview()
    test_webserver()

    print()
    if failures:
        print("%d check(s) failed:" % len(failures))
        for line in failures:
            print("  " + line)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
