#!/usr/bin/env python3
# Drags with the left button on $DISPLAY through XTest, like a real mouse:
#
#   xdrag.py FROM_X FROM_Y TO_X TO_Y
#
# Presses at FROM, moves to TO in small steps, waits there and releases.
# Used by folders_drag.mjs: Chromium's tab drags need real input.
import ctypes
import sys
import time

STEPS = 20

x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
display = ctypes.c_void_p(x11.XOpenDisplay(None))
if not display:
    sys.exit('xdrag.py: cannot open $DISPLAY')


def move(x, y):
    xtst.XTestFakeMotionEvent(display, -1, int(x), int(y), 0)
    x11.XFlush(display)
    time.sleep(0.05)


def button(down):
    xtst.XTestFakeButtonEvent(display, 1, down, 0)
    x11.XFlush(display)
    time.sleep(0.2)


from_x, from_y, to_x, to_y = map(float, sys.argv[1:5])
move(from_x, from_y)
button(True)
for i in range(1, STEPS + 1):
    move(from_x + (to_x - from_x) * i / STEPS, from_y + (to_y - from_y) * i / STEPS)
time.sleep(0.5)
button(False)
x11.XCloseDisplay(display)
