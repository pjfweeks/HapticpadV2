#!/usr/bin/env python3
"""HapticPad configurator.

Edits everything the pad can be configured with - profiles, macros, icons, LED
and haptic settings - over the USB serial link, writing straight to the card
inside the device.  The SD card never has to come out.

    python3 hapticpad.py

That is the whole installation: it runs on the Python that ships with macOS
and opens its interface in your browser.  Nothing leaves the machine - the
server listens on loopback only, and every request must carry a token that is
minted at startup and handed to the page itself.

Options:
    --no-browser        start the server but do not open a browser
    --port N            listen on a fixed port instead of a free one

pyserial is used when installed and is required only on Windows; Pillow adds
JPEG and GIF support to the icon importer.  Neither is needed on macOS.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hp import webserver


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv

    if "--help" in argv or "-h" in argv:
        print(__doc__)
        return 0

    port = 0
    if "--port" in argv:
        try:
            port = int(argv[argv.index("--port") + 1])
        except (IndexError, ValueError):
            print("--port needs a number", file=sys.stderr)
            return 2

    webserver.serve(port=port, open_browser="--no-browser" not in argv)
    return 0


if __name__ == "__main__":
    sys.exit(main())
