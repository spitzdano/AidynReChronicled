#!/usr/bin/env python3
"""vpad.py: a virtual Xbox 360 pad (uinput) for testing the real pad path (src/main/input.cpp) without a controller.

  vpad.py <command file>     creates the pad, then runs a line at a time from the file as it grows:
      press A B X Y LB RB BACK START L3 R3   (down)         release NAME
      axis LX LY RX RY LT RT <-1..1 or 0..1>               dpad up|down|left|right|none
      tap NAME [secs]       sleep <secs>       quit
Run it with the game started as AIDYN_TEST_PAD=1 AIDYN_PAD_FILE=<file> (the game then reads this pad as well).
"""
import sys, time
from evdev import UInput, AbsInfo, ecodes as e

BTN = {"A": e.BTN_SOUTH, "B": e.BTN_EAST, "X": e.BTN_WEST, "Y": e.BTN_NORTH, "LB": e.BTN_TL, "RB": e.BTN_TR,
       "BACK": e.BTN_SELECT, "START": e.BTN_START, "L3": e.BTN_THUMBL, "R3": e.BTN_THUMBR}
caps = {e.EV_KEY: list(BTN.values()) + [e.BTN_MODE],
        e.EV_ABS: [(e.ABS_X, AbsInfo(0, -32768, 32767, 16, 128, 0)), (e.ABS_Y, AbsInfo(0, -32768, 32767, 16, 128, 0)),
                   (e.ABS_RX, AbsInfo(0, -32768, 32767, 16, 128, 0)), (e.ABS_RY, AbsInfo(0, -32768, 32767, 16, 128, 0)),
                   (e.ABS_Z, AbsInfo(0, 0, 255, 0, 0, 0)), (e.ABS_RZ, AbsInfo(0, 0, 255, 0, 0, 0)),
                   (e.ABS_HAT0X, AbsInfo(0, -1, 1, 0, 0, 0)), (e.ABS_HAT0Y, AbsInfo(0, -1, 1, 0, 0, 0))]}
ui = UInput(caps, name="Microsoft X-Box 360 pad", vendor=0x045E, product=0x028E, version=0x0110, bustype=e.BUS_USB)
AX = {"LX": e.ABS_X, "LY": e.ABS_Y, "RX": e.ABS_RX, "RY": e.ABS_RY}
TR = {"LT": e.ABS_Z, "RT": e.ABS_RZ}

def run(line):
    w = line.split()
    if not w:
        return True
    c = w[0]
    if c == "press": ui.write(e.EV_KEY, BTN[w[1]], 1)
    elif c == "release": ui.write(e.EV_KEY, BTN[w[1]], 0)
    elif c == "tap":
        ui.write(e.EV_KEY, BTN[w[1]], 1); ui.syn(); time.sleep(float(w[2]) if len(w) > 2 else 0.2); ui.write(e.EV_KEY, BTN[w[1]], 0)
    elif c == "axis":
        if w[1] in AX: ui.write(e.EV_ABS, AX[w[1]], int(float(w[2]) * 32767))
        else: ui.write(e.EV_ABS, TR[w[1]], int(float(w[2]) * 255))
    elif c == "dpad":
        d = w[1]
        ui.write(e.EV_ABS, e.ABS_HAT0X, {"left": -1, "right": 1}.get(d, 0))
        ui.write(e.EV_ABS, e.ABS_HAT0Y, {"up": -1, "down": 1}.get(d, 0))
    elif c == "sleep": time.sleep(float(w[1]))
    elif c == "quit": return False
    ui.syn()
    return True

time.sleep(1.0)
path = sys.argv[1]
done = 0
while True:
    lines = open(path).read().splitlines()
    for line in lines[done:]:
        done += 1
        if not run(line):
            ui.close(); sys.exit(0)
    time.sleep(0.05)
