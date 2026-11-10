# Trade monitor for the Tracker Mini

Shows values from a program on your computer on the Tracker Mini's screen, over the USB cable.

## On the Tracker

`Config > Trade monitor > Open monitor`. `Menu` goes back. The Tracker keeps receiving while other
screens are open, so the page is current whenever you open the monitor. The bottom right corner
says how fresh the data is: `live`, `updated 12 s ago`, or in red `NO DATA FOR 95 s` when the
program has stopped sending (30 seconds or more).

## On the computer

```
pip install mido python-rtmidi
```

Copy `tracker_monitor.py` next to your program.

### Easiest: mirror your tkinter window

```python
from tracker_monitor import mirror_tk

root = tk.Tk()
# ... build your window as before ...
mirror_tk(root)      # add this line before root.mainloop()
root.mainloop()
```

Once a second it reads the window's labels from top to bottom, the last lines of its console (the
Text widget) and the labels below the console, and sends what changed. Big labels stay big,
coloured labels keep roughly their colour. Buttons are not sent. Works with plain tkinter, ttk and
customtkinter windows.

### Or send exactly what you want

```python
from tracker_monitor import TrackerMonitor

mon = TrackerMonitor()
mon.set_top([
    ("NSDQ100 | INVESTED AMOUNT: 1000", "title"),
    ("No trade open", "big"),
    ("Stoploss: -14.17", "bold"),
])
mon.set_bottom([("Loss/Profit since last code run: +0.00", "large", "green")])
mon.log("[LOG] Final vote result:: 4")
mon.heartbeat()   # call now and then if nothing else changes, so the Tracker knows you're alive
```

Rows: up to 16 above the console, 8 below it. Sizes: `small normal bold large title big`.
Colours: `white grey green red yellow cyan orange`. Only printable ASCII is shown (`€` becomes
`EUR`, other symbols `?`).

Run `python tracker_monitor.py` on its own for a demo page that changes every second.

## Good to know

* If the Tracker isn't connected, nothing happens and your program keeps running; it reconnects by
  itself every 3 seconds and sends the whole page again.
* On Windows a MIDI port can be open in only one program at a time. If a DAW or another MIDI tool
  has "Tracker Mini" open, the monitor can't connect (`mon.last_error` says why).
* The monitor only shows information. It cannot send anything back or place trades.
* Technically: SysEx messages `F0 7D 54 4D <command> ... F7` on the Tracker's USB MIDI port (see
  `core/interface/tradeMonitor/tradeMonitorData.h`).
