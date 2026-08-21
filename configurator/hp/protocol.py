"""Client for the HapticPad's line protocol (see src/cdc_protocol.h).

Everything is synchronous and runs on the caller's thread: commands complete
in milliseconds, so a request/response call is simpler and easier to reason
about than a background reader with a queue.  Asynchronous log lines that
arrive mid-exchange are collected and handed to a callback rather than being
mistaken for data.
"""

import base64
import time

from .transport import SerialLink, TransportError


class ProtocolError(Exception):
    pass


class Response(object):
    def __init__(self, ok, message, data):
        self.ok = ok
        self.message = message
        self.data = data          # list of "+" lines, prefix stripped

    def pairs(self):
        """Interprets the data lines as tab-separated key/value pairs."""
        out = {}
        for line in self.data:
            if "\t" in line:
                key, _, value = line.partition("\t")
                out[key] = value
            else:
                out[line] = ""
        return out

    def rows(self):
        return [line.split("\t") for line in self.data]


class PadClient(object):
    DEFAULT_TIMEOUT = 3.0

    def __init__(self, device, log_callback=None):
        self.link = SerialLink(device)
        self.device = device
        self.log_callback = log_callback
        self._buffer = ""
        self._pending_logs = []

    def close(self):
        self.link.close()

    # -- line plumbing ----------------------------------------------------

    def _pump(self):
        """Moves bytes off the wire into the line buffer."""
        chunk = self.link.read(8192)
        if chunk:
            self._buffer += chunk.decode("utf-8", "replace")

    def _next_line(self):
        idx = self._buffer.find("\n")
        if idx < 0:
            return None
        line = self._buffer[:idx].rstrip("\r")
        self._buffer = self._buffer[idx + 1:]
        return line

    def _dispatch_log(self, line):
        text = line[1:]
        self._pending_logs.append(text)
        if self.log_callback:
            self.log_callback(text)

    def poll(self):
        """Collects log lines that arrived while we were not asking anything."""
        self._pump()
        while True:
            line = self._next_line()
            if line is None:
                return
            if line.startswith("#"):
                self._dispatch_log(line)
            # Anything else outside a command is stray output; drop it rather
            # than letting it desynchronise the next exchange.

    def drain_logs(self):
        out = self._pending_logs
        self._pending_logs = []
        return out

    # -- request / response ------------------------------------------------

    def command(self, text, timeout=None):
        if timeout is None:
            timeout = self.DEFAULT_TIMEOUT

        # Clear anything still in flight so a reply cannot be attributed to
        # the wrong command.
        self.poll()

        try:
            self.link.write((text + "\n").encode("utf-8"))
        except TransportError as exc:
            raise ProtocolError(str(exc))

        data = []
        deadline = time.time() + timeout

        while time.time() < deadline:
            line = self._next_line()
            if line is None:
                self._pump()
                if not self._buffer:
                    time.sleep(0.002)
                continue

            if line.startswith("#"):
                self._dispatch_log(line)
            elif line.startswith("+"):
                data.append(line[1:])
            elif line.startswith("OK"):
                return Response(True, line[2:].strip(), data)
            elif line.startswith("ERR"):
                return Response(False, line[3:].strip(), data)
            # Blank or unrecognised lines are ignored.

        raise ProtocolError("no reply to %r within %.1fs" % (text, timeout))

    def expect(self, text, timeout=None):
        reply = self.command(text, timeout)
        if not reply.ok:
            raise ProtocolError(reply.message or "command failed")
        return reply

    # -- convenience wrappers ----------------------------------------------

    def ping(self):
        return self.expect("PING").message

    def info(self):
        return self.expect("INFO").pairs()

    def status(self):
        return self.expect("STATUS").pairs()

    def settings_text(self):
        reply = self.expect("CONFIG")
        return base64.b64decode("".join(reply.data)).decode("utf-8", "replace")

    def set(self, key, value):
        return self.expect("SET %s %s" % (key, value))

    def save(self):
        return self.expect("SAVE", timeout=5.0)

    def profiles(self):
        """[(index, filename, is_active), ...]"""
        out = []
        for row in self.expect("PROFILES").rows():
            if len(row) >= 2:
                out.append((int(row[0]), row[1], len(row) > 2 and row[2] == "active"))
        return out

    def active_profile(self):
        return self.expect("PROFILE").pairs()

    def load_profile(self, filename):
        return self.expect("PROFILE %s" % filename, timeout=5.0)

    def rescan(self):
        return self.expect("RESCAN", timeout=10.0)

    def listdir(self, path="/"):
        """[(name, size, kind), ...]"""
        out = []
        for row in self.expect("LIST %s" % path, timeout=5.0).rows():
            if len(row) >= 3:
                out.append((row[0], int(row[1]), row[2]))
        return out

    def read_file(self, path):
        reply = self.expect("READ %s" % path, timeout=8.0)
        return base64.b64decode("".join(reply.data))

    def write_file(self, path, data):
        self.expect("WRITE %s" % path, timeout=5.0)

        encoded = base64.b64encode(data).decode("ascii")
        for i in range(0, len(encoded), 76):
            self.link.write((encoded[i:i + 76] + "\n").encode("ascii"))

        # The trailing dot commits the file; the pad answers with the byte
        # count it actually stored.
        self.link.write(b".\n")

        deadline = time.time() + 10.0
        while time.time() < deadline:
            line = self._next_line()
            if line is None:
                self._pump()
                time.sleep(0.002)
                continue
            if line.startswith("#"):
                self._dispatch_log(line)
            elif line.startswith("OK"):
                return int(line[2:].strip() or 0)
            elif line.startswith("ERR"):
                raise ProtocolError(line[3:].strip())
        raise ProtocolError("no confirmation for write of %s" % path)

    def delete(self, path):
        return self.expect("DELETE %s" % path, timeout=5.0)

    def mkdir(self, path):
        return self.expect("MKDIR %s" % path, timeout=5.0)

    def test_action(self, action):
        return self.expect("TEST %s" % action)

    def set_log_level(self, level):
        return self.expect("LOG %s" % level)

    def bootsel(self):
        try:
            self.command("BOOTSEL", timeout=1.0)
        except ProtocolError:
            pass          # the device disappears mid-reply, which is expected

    def reboot(self):
        try:
            self.command("REBOOT", timeout=1.0)
        except ProtocolError:
            pass
