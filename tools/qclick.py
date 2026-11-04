#!/usr/bin/env python3
"""qclick.py: a mouse click in the QEMU Quadra 800 at a screen point, closed loop.  QEMU's ADB mouse is relative and
the Mac accelerates it, so the pointer is found in a screendump (the standard arrow), moved toward the target in
small steps, found again, and so on until it is within 1 pixel; then the button goes down and up.

    ~/serialenv/bin/python tools/qclick.py <monitor port> <x> <y>
    ~/serialenv/bin/python tools/qclick.py <monitor port> drag <x1> <y1> <x2> <y2>    press at 1, release at 2 (a menu)
    ~/serialenv/bin/python tools/qclick.py <monitor port> sq <n> [b|w]   the centre of checkers square n, Checkers XL's
                                                                        board, you black (b, the default) or white
Checkers XL's board: the window's content starts at (8, 40); the board at (+12, +(H - 52 - 256) / 2), 32-pixel
squares, your side at the bottom (as black the board is turned round)."""
import os, socket, sys, tempfile, time
import numpy as np
from PIL import Image

ARROW = [0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00, 0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000]
MASK = [0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80, 0xFFC0, 0xFFE0, 0xFE00, 0xEF00, 0xCF00, 0x8780, 0x0780, 0x0380]

class Mon:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.read()
    def read(self):
        buf = b""
        self.s.settimeout(0.5)
        try:
            while not buf.endswith(b"(qemu) "):
                d = self.s.recv(4096)
                if not d: break
                buf += d
        except socket.timeout:
            pass
        return buf
    def cmd(self, c):
        self.s.sendall((c + "\n").encode()); return self.read()

def shot(m):
    fd, path = tempfile.mkstemp(suffix=".ppm"); os.close(fd)
    m.cmd("screendump " + path); time.sleep(0.3)
    im = np.array(Image.open(path).convert("L")); os.unlink(path)
    return im < 128                                    # True = black

def find(black):
    """The arrow's hot spot (1, 1 in the cursor), or None."""
    H, W = black.shape
    cand = np.argwhere(black[:H - 16, :W - 16])
    for y, x in cand:                                  # (y, x) is the arrow's row 1, column 1: the tip
        y0, x0 = y - 1, x - 1
        if y0 < 0 or x0 < 0: continue
        ok = True
        for r in range(16):
            for c in range(10):
                bit = 0x8000 >> c
                if ARROW[r] & bit:
                    if not black[y0 + r, x0 + c]: ok = False; break
                elif MASK[r] & bit:
                    if black[y0 + r, x0 + c]: ok = False; break
            if not ok: break
        if ok: return x, y
    return None

def main():
    port = int(sys.argv[1])
    m = Mon(port)
    black = shot(m)
    if sys.argv[2] == "sq":
        n, side = int(sys.argv[3]), (sys.argv[4] if len(sys.argv) > 4 else "b")
        H = black.shape[0]
        bx, by = 8 + 12, 40 + max(4, (H - 52 - 256) // 2)
        row = (n - 1) // 4
        col = 2 * ((n - 1) % 4) + (1 if row % 2 == 0 else 0)
        sr, sc = (7 - row, 7 - col) if side == "b" else (row, col)
        tx, ty = bx + sc * 32 + 16, by + sr * 32 + 16
    elif sys.argv[2] != "drag":
        tx, ty = int(sys.argv[2]), int(sys.argv[3])
    if sys.argv[2] == "drag":
        x1, y1, x2, y2 = map(int, sys.argv[3:7])
        black = goto(m, black, x1, y1); m.cmd("mouse_button 1"); time.sleep(0.4)
        goto(m, shot(m), x2, y2); m.cmd("mouse_button 0"); time.sleep(0.2)
        print("qclick: dragged", x1, y1, x2, y2); return
    goto(m, black, tx, ty)
    m.cmd("mouse_button 1"); time.sleep(0.15); m.cmd("mouse_button 0"); time.sleep(0.2)
    print("qclick: clicked", tx, ty)

def goto(m, black, tx, ty):
    p = None
    for _ in range(80):
        p = find(black)
        if p is None:                                  # not seen (over a dark area?): nudge it
            m.cmd("mouse_move 7 7"); time.sleep(0.2); black = shot(m); continue
        dx, dy = tx - p[0], ty - p[1]
        if abs(dx) <= 1 and abs(dy) <= 1: return black
        sx = dx if abs(dx) <= 3 else max(-40, min(40, int(dx * 0.5)))
        sy = dy if abs(dy) <= 3 else max(-40, min(40, int(dy * 0.5)))
        m.cmd("mouse_move %d %d" % (sx, sy)); time.sleep(0.15)
        black = shot(m)
    print("qclick: did not reach", tx, ty, "pointer at", p); sys.exit(1)

main()
