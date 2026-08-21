"""Serial transport for the HapticPad's USB CDC interface.

pyserial is used when it is installed, but it is not required: on macOS and
Linux a USB CDC port is an ordinary character device, so the fallback opens it
directly and puts the terminal into raw mode.  That keeps the configurator
runnable with nothing but a stock Python install, which is the whole point of
shipping it as a script.
"""

import glob
import os
import platform
import sys
import time

try:
    import serial                       # type: ignore
    from serial.tools import list_ports  # type: ignore
    HAVE_PYSERIAL = True
except ImportError:
    HAVE_PYSERIAL = False

# Matches the VID/PID pair in the firmware's usb_descriptors.c.
HP_VID = 0xCAFE
HP_PID = 0x4010


class PortInfo(object):
    def __init__(self, device, description="", is_candidate=False):
        self.device = device
        self.description = description
        self.is_candidate = is_candidate

    def __str__(self):
        return self.device if not self.description else "%s  (%s)" % (
            self.device, self.description)


def list_serial_ports():
    """Every serial port we could plausibly talk to, likely ones first."""
    ports = []

    if HAVE_PYSERIAL:
        for p in list_ports.comports():
            candidate = (p.vid == HP_VID and p.pid == HP_PID)
            desc = p.description or ""
            if p.vid is not None:
                desc = "%s  %04X:%04X" % (desc, p.vid, p.pid)
            ports.append(PortInfo(p.device, desc.strip(), candidate))
    else:
        system = platform.system()
        if system == "Darwin":
            # cu.* is the callout device: unlike tty.*, opening it does not
            # block waiting for carrier detect.
            patterns = ["/dev/cu.usbmodem*"]
        elif system == "Linux":
            patterns = ["/dev/ttyACM*", "/dev/ttyUSB*"]
        else:
            patterns = []

        for pattern in patterns:
            for path in sorted(glob.glob(pattern)):
                ports.append(PortInfo(path, "USB serial", True))

    ports.sort(key=lambda p: (not p.is_candidate, p.device))
    return ports


class TransportError(Exception):
    pass


class _RawTty(object):
    """Minimal raw serial channel built on os.open + termios (POSIX only)."""

    def __init__(self, device, timeout=1.0):
        import termios

        self.timeout = timeout
        try:
            self.fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            raise TransportError("cannot open %s: %s" % (device, exc))

        try:
            attrs = termios.tcgetattr(self.fd)
        except termios.error as exc:
            os.close(self.fd)
            raise TransportError("%s is not a serial device (%s)" % (device, exc))

        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs

        # Raw mode: no echo, no canonical processing, no flow control, no
        # translation of any byte.  The link is a USB pipe; the baud rate and
        # framing bits are ignored by the device but must still be legal.
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                   termios.ISTRIP | termios.INLCR | termios.IGNCR |
                   termios.ICRNL | termios.IXON)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                   termios.ISIG | termios.IEXTEN)
        cflag &= ~(termios.CSIZE | termios.PARENB)
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL

        cc = list(cc)
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0

        termios.tcsetattr(self.fd, termios.TCSANOW,
                          [iflag, oflag, cflag, lflag, ispeed, ospeed, cc])
        try:
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except termios.error:
            pass

    def write(self, data):
        total = 0
        deadline = time.time() + self.timeout
        while total < len(data):
            try:
                total += os.write(self.fd, data[total:])
            except BlockingIOError:
                if time.time() > deadline:
                    raise TransportError("write timed out")
                time.sleep(0.002)
        return total

    def read(self, size):
        try:
            data = os.read(self.fd, size)
        except BlockingIOError:
            return b""
        except OSError as exc:
            raise TransportError("read failed: %s" % exc)
        return data or b""

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


class SerialLink(object):
    """Byte-level link, with whichever backend is available."""

    def __init__(self, device, timeout=1.0):
        self.device = device
        self.timeout = timeout
        self._backend = None

        if HAVE_PYSERIAL:
            try:
                self._backend = serial.Serial(device, 115200, timeout=0,
                                              write_timeout=timeout)
            except Exception as exc:                # serial.SerialException
                raise TransportError("cannot open %s: %s" % (device, exc))
        elif os.name == "posix":
            self._backend = _RawTty(device, timeout)
        else:
            raise TransportError(
                "pyserial is required on this platform.\n"
                "Install it with:  %s -m pip install --user pyserial"
                % os.path.basename(sys.executable))

    def write(self, data):
        return self._backend.write(data)

    def read(self, size=4096):
        return self._backend.read(size)

    def close(self):
        if self._backend is not None:
            self._backend.close()
            self._backend = None
