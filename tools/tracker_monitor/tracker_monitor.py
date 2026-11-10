"""Send values to the Trade monitor screen of a Polyend Tracker Mini over the USB cable.

On the Tracker: Config > Trade monitor > Open monitor. The Tracker keeps the page while other
screens are open, so it is up to date whenever the monitor screen is opened.

Needs:  pip install mido python-rtmidi

Two ways to use it:

1. Mirror an existing tkinter window (no other changes to your program):

       from tracker_monitor import mirror_tk
       mirror_tk(root)          # root = your Tk() window, call before root.mainloop()

   Once a second it reads the labels of the window (top to bottom), the last lines of its console
   (a Text widget) and the labels below it, and sends whatever changed. Font size and colour of
   each label are carried over roughly.

2. Send the values yourself:

       from tracker_monitor import TrackerMonitor
       mon = TrackerMonitor()
       mon.set_top([("NSDQ100 | INVESTED: 1000", "title"),
                    ("No trade open", "big"),
                    ("Stoploss: -14.17", "bold")])
       mon.set_bottom([("Loss/Profit since last run: +0.00", "large", "green")])
       mon.log("Final vote result: 4")

   A row is a string, or (text, size) or (text, size, colour).
   Sizes: small, normal, bold, large, title, big.
   Colours: white, grey, green, red, yellow, cyan, orange.

If the Tracker is not connected, everything is silently skipped and the connection is tried again
every few seconds, so your program never stops because of the monitor.
"""

import threading
import time
from collections import deque

__all__ = ["TrackerMonitor", "mirror_tk", "encode_row", "encode_log", "SIZES", "COLOURS"]

TOP_ROWS = 16
BOTTOM_ROWS = 8
ROW_CHARS = 80
LOG_CHARS = 120
LOG_KEEP = 16

SIZES = ["small", "normal", "bold", "large", "title", "big"]
COLOURS = ["white", "grey", "green", "red", "yellow", "cyan", "orange"]

_HEADER = [0x7D, 0x54, 0x4D]  # non-commercial manufacturer ID, "TM"
_CMD_SET_ROW = 0x01
_CMD_LOG = 0x02
_CMD_CLEAR = 0x03
_CMD_HEARTBEAT = 0x04
_CMD_CLEAR_LOG = 0x05

_REPLACEMENTS = {
    "–": "-", "—": "-", "−": "-", "‘": "'", "’": "'", "“": '"',
    "”": '"', "…": "...", "€": "EUR", "£": "GBP", "°": " deg",
    " ": " ", "\t": " ",
}


def _ascii(text, limit):
    out = []
    for ch in str(text):
        ch = _REPLACEMENTS.get(ch, ch)
        for c in ch:
            o = ord(c)
            out.append(o if 0x20 <= o < 0x7F else ord("?"))
    return out[:limit]


def _style(size="normal", colour="white"):
    s = SIZES.index(size) if size in SIZES else 1
    c = COLOURS.index(colour) if colour in COLOURS else 0
    return s | (c << 3)


def _normalise_row(row):
    """Accepts None, "text", (text,), (text, size) or (text, size, colour)."""
    if row is None:
        return ("", "normal", "white")
    if isinstance(row, str):
        return (row, "normal", "white")
    row = list(row)[:3]
    defaults = ["", "normal", "white"]
    row += defaults[len(row):]
    return (str(row[0]), row[1], row[2])


def encode_row(index, text, size="normal", colour="white"):
    """SysEx data bytes (without F0/F7) that set one row."""
    return _HEADER + [_CMD_SET_ROW, index & 0x7F, _style(size, colour)] + _ascii(text, ROW_CHARS)


def encode_log(text):
    return _HEADER + [_CMD_LOG] + _ascii(text, LOG_CHARS)


def _wrap(text, width=105):
    text = str(text).rstrip()
    if not text:
        return [""]
    lines = []
    while len(text) > width:
        cut = text.rfind(" ", 0, width)
        if cut <= width // 2:
            cut = width
        lines.append(text[:cut].rstrip())
        text = text[cut:].lstrip()
    lines.append(text)
    return lines


class _MidoPort:
    """Opens the Tracker's MIDI output with mido (python-rtmidi backend)."""

    def __init__(self, name_contains):
        import mido  # imported here so the module loads even without mido installed

        self._mido = mido
        names = [n for n in mido.get_output_names() if name_contains.lower() in n.lower()]
        if not names:
            raise IOError("no MIDI output containing %r" % name_contains)
        # prefer the MIDI port over any other port with the same device name
        names.sort(key=lambda n: ("midi" not in n.lower(), n))
        self.name = names[0]
        self._port = mido.open_output(self.name)

    def send_sysex(self, data):
        self._port.send(self._mido.Message("sysex", data=data))

    def close(self):
        try:
            self._port.close()
        except Exception:
            pass


class TrackerMonitor:
    def __init__(self, port_name="Tracker", resend_every=5.0, retry_every=3.0, port_factory=None):
        self._port_name = port_name
        self._resend_every = resend_every
        self._retry_every = retry_every
        self._factory = port_factory or (lambda: _MidoPort(port_name))
        self._port = None
        self._last_try = -1e9
        self._last_full = 0.0
        self._rows = [("", "normal", "white")] * (TOP_ROWS + BOTTOM_ROWS)
        self._sent_rows = [None] * (TOP_ROWS + BOTTOM_ROWS)
        self._logs = deque(maxlen=LOG_KEEP)
        self._lock = threading.RLock()
        self.port_name_in_use = None
        self.last_error = None

    # -- public API ------------------------------------------------------------------------
    def set_top(self, rows):
        """Rows above the console (up to 16). Rows not given are cleared."""
        self._set_range(0, TOP_ROWS, rows)

    def set_bottom(self, rows):
        """Rows below the console (up to 8). Rows not given are cleared."""
        self._set_range(TOP_ROWS, BOTTOM_ROWS, rows)

    def set_row(self, index, text, size="normal", colour="white"):
        with self._lock:
            self._rows[index] = (str(text), size, colour)
            self._flush()

    def log(self, text):
        """Adds a line to the console (long lines are wrapped)."""
        with self._lock:
            for line in _wrap(text):
                self._logs.append(line)
                self._send(encode_log(line))

    def set_log(self, lines):
        """Replaces the whole console with these lines (oldest first)."""
        with self._lock:
            wrapped = [w for line in lines for w in _wrap(line)][-LOG_KEEP:]
            self._logs = deque(wrapped, maxlen=LOG_KEEP)
            if self._ensure_port():
                self._send(_HEADER + [_CMD_CLEAR_LOG])
                for line in wrapped:
                    self._send(encode_log(line))

    def heartbeat(self):
        """Tells the Tracker the program is still running (resets 'NO DATA FOR ...')."""
        with self._lock:
            self._flush()
            self._send(_HEADER + [_CMD_HEARTBEAT])

    def clear(self):
        with self._lock:
            self._rows = [("", "normal", "white")] * (TOP_ROWS + BOTTOM_ROWS)
            self._logs.clear()
            self._send(_HEADER + [_CMD_CLEAR])
            self._sent_rows = list(self._rows)

    @property
    def connected(self):
        return self._port is not None

    def close(self):
        with self._lock:
            if self._port is not None:
                self._port.close()
            self._port = None

    # -- internals ---------------------------------------------------------------------------
    def _set_range(self, start, count, rows):
        rows = list(rows or [])[:count]
        with self._lock:
            for i in range(count):
                self._rows[start + i] = _normalise_row(rows[i] if i < len(rows) else None)
            self._flush()

    def _ensure_port(self):
        if self._port is not None:
            return True
        now = time.monotonic()
        if now - self._last_try < self._retry_every:
            return False
        self._last_try = now
        try:
            self._port = self._factory()
            self.port_name_in_use = getattr(self._port, "name", None)
            self.last_error = None
        except Exception as e:  # not connected, mido missing, port busy ...
            self._port = None
            self.last_error = e
            return False
        # a fresh connection (or a restarted Tracker): send everything
        self._sent_rows = [None] * len(self._rows)
        self._last_full = 0.0
        try:
            self._port.send_sysex(_HEADER + [_CMD_CLEAR])
            for line in self._logs:
                self._port.send_sysex(encode_log(line))
        except Exception as e:
            self._drop(e)
            return False
        return True

    def _drop(self, error):
        self.last_error = error
        if self._port is not None:
            self._port.close()
        self._port = None

    def _send(self, data):
        if not self._ensure_port():
            return False
        try:
            self._port.send_sysex(data)
            return True
        except Exception as e:
            self._drop(e)
            return False

    def _flush(self):
        if not self._ensure_port():
            return
        now = time.monotonic()
        full = now - self._last_full >= self._resend_every
        for i, row in enumerate(self._rows):
            if full or row != self._sent_rows[i]:
                text, size, colour = row
                if not self._send(encode_row(i, text, size, colour)):
                    return
                self._sent_rows[i] = row
        if full:
            self._last_full = now


# ------------------------------------------------------------------------------------------------
# Mirroring a tkinter window

_PALETTE = {
    "white": (242, 244, 246), "grey": (154, 164, 174), "green": (76, 217, 100),
    "red": (255, 90, 90), "yellow": (245, 197, 66), "cyan": (90, 200, 250), "orange": (255, 159, 67),
}


def _nearest_colour(rgb):
    r, g, b = rgb
    if max(r, g, b) - min(r, g, b) < 40:  # no real colour: white or grey by brightness
        return "white" if max(r, g, b) > 200 else "grey"
    best, dist = "white", None
    for name, (pr, pg, pb) in _PALETTE.items():
        if name in ("white", "grey"):
            continue
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if dist is None or d < dist:
            best, dist = name, d
    return best


def _size_for(points, bold):
    """Tk font size (points, or negative = pixels) to one of SIZES."""
    if points < 0:
        points = -points * 0.75
    if points >= 24:
        s = "big"
    elif points >= 18:
        s = "title"
    elif points >= 14:
        s = "large"
    elif points >= 11:
        s = "bold" if bold else "normal"
    else:
        s = "normal" if bold else "small"
    return s


def _walk(widget):
    yield widget
    try:
        children = widget.winfo_children()
    except Exception:
        children = []
    for child in children:
        yield from _walk(child)


def _widget_class(w):
    try:
        return w.winfo_class()
    except Exception:
        return ""


def _label_text(w):
    try:
        var = str(w.cget("textvariable") or "")
        if var:
            return str(w.getvar(var))
    except Exception:
        pass
    try:
        return str(w.cget("text"))
    except Exception:
        return ""


def _label_style(w):
    size, bold = 10, False
    try:
        import tkinter.font as tkfont

        f = tkfont.Font(font=w.cget("font"))
        size = f.actual("size")
        bold = f.actual("weight") == "bold"
    except Exception:
        pass
    colour = "white"
    try:
        fg = w.cget("foreground")
        r, g, b = (v // 257 for v in w.winfo_rgb(fg))
        colour = _nearest_colour((r, g, b))
    except Exception:
        pass
    return _size_for(size, bold), colour


def read_window(root, console_lines=16):
    """Reads (top rows, console lines, bottom rows) from a Tk window, in screen order."""
    labels, consoles = [], []
    for w in _walk(root):
        cls = _widget_class(w)
        try:
            if not w.winfo_ismapped():
                continue
        except Exception:
            continue
        if cls in ("Label", "TLabel"):
            text = _label_text(w).strip()
            if text:
                labels.append((w.winfo_rooty(), w.winfo_rootx(), w, text))
        elif cls == "Text":
            consoles.append((w.winfo_rooty(), w))

    console_y = consoles[0][0] if consoles else None
    if consoles:
        consoles.sort(key=lambda c: -(c[1].winfo_height() * c[1].winfo_width()))  # the biggest
        console_y, console = consoles[0]
        raw = console.get("1.0", "end-1c").splitlines()
        lines = [l for l in raw if l.strip()][-console_lines:]
    else:
        lines = []

    labels.sort(key=lambda l: (l[0], l[1]))
    top, bottom = [], []
    for y, _x, w, text in labels:
        for part in text.splitlines():
            if not part.strip():
                continue
            size, colour = _label_style(w)
            row = (part.strip(), size, colour)
            if console_y is None or y < console_y:
                top.append(row)
            else:
                bottom.append(row)
    return top[:TOP_ROWS], lines, bottom[:BOTTOM_ROWS]


def mirror_tk(root, monitor=None, every_ms=1000):
    """Copies the Tk window `root` to the Tracker once per `every_ms`. Returns the TrackerMonitor."""
    mon = monitor or TrackerMonitor()
    state = {"log": None}

    def tick():
        try:
            top, lines, bottom = read_window(root)
            mon.set_top(top)
            mon.set_bottom(bottom)
            if lines != state["log"]:
                mon.set_log(lines)
                state["log"] = lines
            mon.heartbeat()
        except Exception as e:  # never disturb the program being mirrored
            mon.last_error = e
        try:
            root.after(every_ms, tick)
        except Exception:
            pass  # window closed

    root.after(every_ms, tick)
    return mon


if __name__ == "__main__":
    # Demo: a page like a trading bot's, updated every second. Run with the Tracker connected and
    # Config > Trade monitor > Open monitor on its screen.
    import random

    mon = TrackerMonitor()
    pnl = 0.0
    n = 0
    print("Sending to the Tracker... (Ctrl+C to stop)")
    while True:
        n += 1
        pnl += random.uniform(-2, 2)
        mon.set_top([
            ("NSDQ100 | INVESTED AMOUNT: 1000", "title"),
            ("Code running for: 0.0 days %d minutes" % (n // 60), "small", "grey"),
            ("No trade open" if n % 20 < 10 else "LONG trade open", "big"),
            ("Stoploss: -14.17", "bold"),
            ("Shadowtrade stoploss: -16.67", "bold"),
            ("Take profit: 333330.00", "bold"),
            ("Main timeout remaining, seconds: %d" % max(0, 900 - n), "normal"),
            ("Extra timeout remaining, seconds: 120", "normal"),
        ])
        mon.set_bottom([
            ("Loss/Profit API reference: %+.2f" % pnl, "bold", "cyan"),
            ("Shadowtrades lost: 0, Shadowtrades recovered: 0", "normal", "yellow"),
            ("Loss/Profit since last code run: %+.2f" % pnl, "large", "green" if pnl >= 0 else "red"),
            ("Profit/Day: %+.2f | DD left: 531.62/566.67 used 35.05" % pnl, "normal", "grey"),
        ])
        if n % 3 == 0:
            mon.log("[LOG] Total number of normal votes:: %d at %s" % (random.randint(0, 12),
                                                                       time.strftime("%Y-%m-%d %H:%M:%S")))
        mon.heartbeat()
        if not mon.connected:
            print("Tracker not found (%s), retrying..." % mon.last_error)
        time.sleep(1)
