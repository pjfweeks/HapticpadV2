"""Local web server that backs the configurator UI.

macOS binds /usr/bin/python3 to Tk 8.5.9, a 2010 build that Apple deprecated
because it cannot paint reliably on a modern macOS - freshly mapped windows
come up empty.  There is no other Tk on a stock machine, so the interface is
served to the browser instead: still nothing to install, but rendered by
something that actually works.

Everything below the presentation layer is unchanged - the same protocol
client, profile parser, icon converter and OLED renderer the Tk version used.

Only loopback is bound, and every API call must carry the token minted at
startup.  That token is what stops a web page you happen to have open from
driving the pad: a custom header forces a CORS preflight, which the browser
refuses to send cross-origin.
"""

import base64
import json
import mimetypes
import os
import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import images, preview, profiles as profile_model
from .protocol import PadClient, ProtocolError
from .transport import TransportError, list_serial_ports

WEBUI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "webui")
ICON_DIRS = ["/profiles/icons", "/icons"]

MAX_BODY = 8 * 1024 * 1024          # generous enough for an imported image


class Session(object):
    """Owns the link to the pad.  One at a time, guarded by a lock."""

    def __init__(self):
        self.client = None
        self.device = None
        self.greeting = ""
        self.icon_dir = ICON_DIRS[0]
        self.lock = threading.Lock()
        self.logs = []

    # -- connection ----------------------------------------------------

    def connect(self, device):
        self.disconnect()
        client = PadClient(device, log_callback=self._on_log)
        greeting = client.ping()          # fails loudly if it is not our pad
        self.client = client
        self.device = device
        self.greeting = greeting
        self._detect_icon_dir()
        return greeting

    def disconnect(self):
        if self.client:
            try:
                self.client.close()
            except Exception:
                pass
        self.client = None
        self.device = None
        self.greeting = ""

    def require(self):
        if not self.client:
            raise ProtocolError("not connected to a pad")
        return self.client

    def _on_log(self, line):
        self.logs.append(line)
        if len(self.logs) > 2000:
            del self.logs[:1000]

    def drain_logs(self):
        out = self.logs
        self.logs = []
        return out

    # -- helpers -------------------------------------------------------

    def _detect_icon_dir(self):
        """Uses whichever icon directory the card actually has."""
        for directory in ICON_DIRS:
            try:
                if self.client.listdir(directory):
                    self.icon_dir = directory
                    return
            except ProtocolError:
                continue
        self.icon_dir = ICON_DIRS[0]

    def icon_names(self):
        try:
            entries = self.client.listdir(self.icon_dir)
        except ProtocolError:
            return []
        return sorted(name for name, size, kind in entries
                      if kind == "file" and name.lower().endswith(".bin")
                      and size == images.ICON_BYTES)

    def fetch_icons(self, names):
        out = {}
        for name in names:
            try:
                out[name] = self.client.read_file(self.icon_dir + "/" + name)
            except ProtocolError:
                out[name] = None
        return out


# ---------------------------------------------------------------------------
# Serialisation between the profile model and JSON
# ---------------------------------------------------------------------------

def profile_to_json(profile):
    return {
        "filename": profile.filename,
        "name": profile.name,
        "led_mode": profile.led_mode,
        "led_primary": profile.led_primary,
        "led_secondary": profile.led_secondary,
        "led_brightness": profile.led_brightness,
        "led_overrides": profile.led_overrides(),
        "wheel": profile.wheel,
        "wheel_func": profile.wheel_func,
        "detents": profile.detents,
        "strength": profile.strength,
        "wheel_cw": profile.wheel_cw,
        "wheel_ccw": profile.wheel_ccw,
        "effective_wheel": list(profile.effective_wheel_actions()),
        "buttons": [
            {
                "label": b.label,
                "icon": b.icon,
                "action": b.action_text(),
                "steps": [{"action": s.action, "delay": s.delay} for s in b.steps],
            }
            for b in profile.buttons
        ],
        "extra": profile.extra,
    }


def profile_from_json(payload):
    profile = profile_model.Profile(payload.get("filename", ""))
    profile.name = payload.get("name", "")
    profile.led_mode = payload.get("led_mode", "") or ""
    profile.led_primary = payload.get("led_primary", "") or ""
    profile.led_secondary = payload.get("led_secondary", "") or ""
    profile.led_brightness = str(payload.get("led_brightness", "") or "")
    profile.wheel = payload.get("wheel", "clicky")
    profile.wheel_func = payload.get("wheel_func", "scroll")
    profile.detents = str(payload.get("detents", "") or "")
    profile.strength = str(payload.get("strength", "") or "")
    profile.wheel_cw = payload.get("wheel_cw", "")
    profile.wheel_ccw = payload.get("wheel_ccw", "")
    profile.extra = payload.get("extra", []) or []

    for i, data in enumerate(payload.get("buttons", [])[:profile_model.BUTTON_COUNT]):
        button = profile.buttons[i]
        button.label = data.get("label", "")
        button.icon = data.get("icon", "")
        if "steps" in data and data["steps"] is not None:
            button.steps = [profile_model.Step(s.get("action", ""), s.get("delay", 0))
                            for s in data["steps"] if s.get("action")]
        else:
            button.set_action_text(data.get("action", ""))
    return profile


def matrix_to_rows(canvas):
    """The preview as one string per row - compact, and trivial for JS to draw."""
    return ["".join("1" if v else "0" for v in row) for row in canvas.rows]


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "HapticPad"
    protocol_version = "HTTP/1.1"

    # -- plumbing ------------------------------------------------------

    def log_message(self, fmt, *args):
        pass                              # the browser console is enough

    def _send(self, code, body, content_type="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, payload, code=200):
        self._send(code, json.dumps(payload))

    def _error(self, message, code=400):
        self._json({"error": str(message)}, code)

    def _authorised(self):
        """Token in a custom header, so a cross-origin page cannot forge it."""
        return self.headers.get("X-HapticPad-Token") == self.server.token

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        if length > MAX_BODY:
            raise ValueError("request body too large")
        return json.loads(self.rfile.read(length).decode("utf-8"))

    # -- routing -------------------------------------------------------

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path.startswith("/api/"):
            if not self._authorised():
                return self._error("bad or missing token", 403)
            return self._api(parsed.path, parse_qs(parsed.query), None)
        return self._static(parsed.path)

    def do_POST(self):
        parsed = urlparse(self.path)
        if not parsed.path.startswith("/api/"):
            return self._error("not found", 404)
        if not self._authorised():
            return self._error("bad or missing token", 403)
        try:
            payload = self._body()
        except ValueError as exc:
            return self._error(exc)
        return self._api(parsed.path, parse_qs(parsed.query), payload)

    def _static(self, path):
        if path in ("/", "/index.html"):
            name = "index.html"
        else:
            name = os.path.basename(path)

        full = os.path.join(WEBUI_DIR, name)
        if not os.path.isfile(full):
            return self._send(404, "not found", "text/plain")

        with open(full, "rb") as handle:
            body = handle.read()

        if name == "index.html":
            # The token reaches the page exactly once, in the page itself.
            body = body.replace(b"__TOKEN__", self.server.token.encode("ascii"))

        content_type = mimetypes.guess_type(name)[0] or "application/octet-stream"
        self._send(200, body, content_type)

    # -- the API -------------------------------------------------------

    def _api(self, path, query, payload):
        session = self.server.session
        try:
            with session.lock:
                result = self._dispatch(path, query, payload, session)
        except (ProtocolError, TransportError) as exc:
            return self._error(exc)
        except Exception as exc:                     # pragma: no cover
            import traceback
            traceback.print_exc()
            return self._error("%s: %s" % (type(exc).__name__, exc), 500)

        return self._json(result if result is not None else {"ok": True})

    def _dispatch(self, path, query, payload, session):
        payload = payload or {}

        # --- connection -----------------------------------------------
        if path == "/api/ports":
            return {
                "ports": [{"device": p.device, "description": p.description,
                           "likely": p.is_candidate} for p in list_serial_ports()],
                "connected": session.device,
            }

        if path == "/api/connect":
            greeting = session.connect(payload["port"])
            return {"greeting": greeting, "device": session.device}

        if path == "/api/disconnect":
            session.disconnect()
            return {"ok": True}

        client = None
        if path not in ("/api/ports", "/api/connect", "/api/disconnect", "/api/preview",
                        "/api/icon/convert", "/api/vocabulary"):
            client = session.require()

        # --- device ---------------------------------------------------
        if path == "/api/info":
            return {"info": client.info(), "device": session.device,
                    "greeting": session.greeting, "icon_dir": session.icon_dir}

        if path == "/api/status":
            return {"status": client.status(), "logs": session.drain_logs()}

        if path == "/api/logs":
            return {"logs": session.drain_logs()}

        if path == "/api/settings":
            if payload:
                client.set(payload["key"], payload["value"])
                return {"ok": True}
            parsed = {}
            for line in client.settings_text().splitlines():
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, _, value = line.partition("=")
                    parsed[key.strip()] = value.strip()
            return {"settings": parsed}

        if path == "/api/settings/save":
            client.save()
            return {"ok": True}

        # --- profiles -------------------------------------------------
        if path == "/api/profiles":
            entries = client.profiles()
            return {"profiles": [{"index": i, "file": f, "active": a}
                                 for i, f, a in entries]}

        if path == "/api/profile":
            name = (query.get("file") or [""])[0]
            if not name:
                raise ProtocolError("which profile?")
            text = client.read_file("/profiles/" + name).decode("utf-8", "replace")
            return {"profile": profile_to_json(profile_model.parse(text, name)),
                    "text": text}

        if path == "/api/profile/save":
            profile = profile_from_json(payload["profile"])
            if not profile.filename:
                raise ProtocolError("the profile needs a file name")
            if not profile.filename.lower().endswith((".txt", ".ini")):
                profile.filename += ".txt"
            text = profile_model.serialize(profile).encode("utf-8")
            written = client.write_file("/profiles/" + profile.filename, text)
            client.rescan()
            return {"written": written, "filename": profile.filename}

        if path == "/api/profile/activate":
            client.load_profile(payload["file"])
            return {"ok": True}

        if path == "/api/profile/delete":
            client.delete("/profiles/" + payload["file"])
            client.rescan()
            return {"ok": True}

        if path == "/api/rescan":
            client.rescan()
            return {"ok": True}

        # --- preview (no device needed) -------------------------------
        if path == "/api/preview":
            profile = profile_from_json(payload["profile"])
            wanted = sorted({b.icon for b in profile.buttons if b.icon})
            icons = {}
            for name in wanted:
                if name in self.server.icon_cache:
                    icons[name] = self.server.icon_cache[name]
                elif session.client:
                    try:
                        data = session.client.read_file(session.icon_dir + "/" + name)
                    except ProtocolError:
                        data = None
                    self.server.icon_cache[name] = data
                    icons[name] = data
            canvas = preview.render_profile(profile, icons,
                                            payload.get("index"), payload.get("total"))
            return {"rows": matrix_to_rows(canvas)}

        # --- icons ----------------------------------------------------
        if path == "/api/icons":
            names = session.icon_names()
            return {"icons": names, "dir": session.icon_dir}

        if path == "/api/icon":
            name = (query.get("name") or [""])[0]
            data = client.read_file(session.icon_dir + "/" + name)
            self.server.icon_cache[name] = data
            return {"name": name, "rows": _icon_rows(data),
                    "coverage": images.icon_coverage(data)}

        if path == "/api/icon/convert":
            raw = base64.b64decode(payload["image"])
            tmp = os.path.join(self.server.scratch, "import.bin")
            with open(tmp, "wb") as handle:
                handle.write(raw)
            try:
                image = images.load_image(tmp)
            except images.ImageError as exc:
                raise ProtocolError(exc)
            data, mode = images.to_icon(image,
                                        threshold=int(payload.get("threshold", 128)),
                                        mode=payload.get("mode", images.MODE_AUTO))
            return {"data": base64.b64encode(data).decode("ascii"),
                    "rows": _icon_rows(data), "mode": mode,
                    "coverage": images.icon_coverage(data)}

        if path == "/api/icon/upload":
            name = payload["name"]
            if not name.lower().endswith(".bin"):
                name += ".bin"
            data = base64.b64decode(payload["data"])
            client.mkdir(session.icon_dir)
            client.write_file(session.icon_dir + "/" + name, data)
            self.server.icon_cache[name] = data
            return {"name": name}

        if path == "/api/icon/delete":
            client.delete(session.icon_dir + "/" + payload["name"])
            self.server.icon_cache.pop(payload["name"], None)
            return {"ok": True}

        # --- actions --------------------------------------------------
        if path == "/api/test":
            reply = client.test_action(payload["action"])
            return {"message": reply.message}

        if path == "/api/raw":
            reply = client.command(payload["command"], timeout=10.0)
            return {"ok": reply.ok, "message": reply.message, "data": reply.data}

        if path == "/api/bootsel":
            client.bootsel()
            session.disconnect()
            return {"ok": True}

        if path == "/api/reboot":
            client.reboot()
            session.disconnect()
            return {"ok": True}

        if path == "/api/vocabulary":
            return {
                "wheel_modes": profile_model.WHEEL_MODES,
                "wheel_funcs": [{"name": name, "cw": cw, "ccw": ccw,
                                 "help": profile_model.WHEEL_FUNC_HELP.get(name, "")}
                                for name, (cw, ccw) in profile_model.WHEEL_FUNCS.items()],
                "led_modes": [{"name": name,
                               "help": profile_model.LED_MODE_HELP.get(name, "")}
                              for name in profile_model.LED_MODES],
                "max_steps": profile_model.MAX_STEPS,
                "buttons": profile_model.BUTTON_COUNT,
                "icon_modes": list(images.MODES),
            }

        raise ProtocolError("unknown endpoint %s" % path)


def _icon_rows(data):
    return ["".join("1" if v else "0" for v in row)
            for row in images.icon_to_matrix(data)]


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, scratch):
        super().__init__(address, Handler)
        self.token = secrets.token_urlsafe(24)
        self.session = Session()
        self.icon_cache = {}
        self.scratch = scratch


def serve(port=0, open_browser=True):
    import tempfile

    scratch = tempfile.mkdtemp(prefix="hapticpad-")
    server = Server(("127.0.0.1", port), scratch)
    actual = server.server_address[1]
    # The token is injected into the page itself rather than carried in the
    # URL, so it never lands in browser history or a shoulder-surfed address bar.
    url = "http://127.0.0.1:%d/" % actual

    print("HapticPad configurator running at:")
    print("    %s" % url)
    print("Press Ctrl-C to stop.")

    if open_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.session.disconnect()
        server.server_close()
