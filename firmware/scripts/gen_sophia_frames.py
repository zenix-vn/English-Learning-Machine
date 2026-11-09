#!/usr/bin/env python3
"""Cuts the Sophia sprite sheet into the frames embedded in the firmware (main/home/assets/sophia_*.z565).

The sheet is a 4 x 2 grid on a solid green (#00FF00) background, in this order:
  IDLE  BLINK  TALK1  TALK2 / LISTEN  THINK  HAPPY  WAVE
The green is keyed out (soft edges, the green spill is removed from the outlines), every cell is scaled to
280 x 280 and written in the format the display uses, RGB565 followed by an 8-bit alpha plane (LVGL's RGB565A8),
compressed with raw deflate. The firmware inflates a frame straight into PSRAM with the chip's ROM decoder, which needs
no working memory (decoding PNGs through LVGL's allocator failed after a few frames). A 100 x 100 version of IDLE is
written for the home screen.

Run: python3 scripts/gen_sophia_frames.py "<sprite sheet>.png"
Needs: Pillow and numpy.
"""
import os
import sys
import zlib

import numpy as np
from PIL import Image

NAMES = ["idle", "blink", "talk1", "talk2", "listen", "think", "happy", "wave"]
COLUMNS, ROWS = 4, 2
SIZE = 280
SMALL = 100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main", "home", "assets")


def key_green(rgb: np.ndarray) -> np.ndarray:
    """RGB uint8 (h, w, 3) -> RGBA uint8 with the green background removed."""
    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    greenness = g - np.maximum(r, b)                      # 255 on the background, <= 0 on the character
    alpha = np.clip((200.0 - greenness) / (200.0 - 20.0), 0.0, 1.0)
    alpha[alpha < 0.04] = 0.0
    # edge pixels are a mix of character and green: undo the mix, P = a*F + (1-a)*green
    a = np.maximum(alpha, 1e-3)[..., None]
    bg = np.array([0.0, 255.0, 0.0], dtype=np.float32)
    fg = np.clip((f - (1.0 - a) * bg) / a, 0.0, 255.0)
    fg[..., 1] = np.minimum(fg[..., 1], np.maximum(fg[..., 0], fg[..., 2]))      # no green fringe left
    out = np.dstack([fg, alpha * 255.0])
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def to_z565(img: Image.Image, path: str):
    a = np.asarray(img.convert("RGBA"))
    r, g, b, alpha = (a[..., i].astype(np.uint16) for i in range(4))
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    raw = rgb565.astype("<u2").tobytes() + alpha.astype(np.uint8).tobytes()
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)              # raw deflate, no zlib header
    with open(path, "wb") as f:
        f.write(comp.compress(raw) + comp.flush())


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sheet = Image.open(sys.argv[1]).convert("RGB")
    w, h = sheet.size
    os.makedirs(OUT, exist_ok=True)
    cell_w, cell_h = w / COLUMNS, h / ROWS
    side = int(min(cell_w, cell_h))
    for i, name in enumerate(NAMES):
        c, r = i % COLUMNS, i // COLUMNS
        x0 = int(round(c * cell_w + (cell_w - side) / 2))
        y0 = int(round(r * cell_h + (cell_h - side) / 2))
        cell = sheet.crop((x0, y0, x0 + side, y0 + side))
        rgba = Image.fromarray(key_green(np.asarray(cell)))
        frame = rgba.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        path = os.path.join(OUT, f"sophia_{name}.z565")
        to_z565(frame, path)
        print(f"{name:7s} {os.path.getsize(path):6d} bytes")
        if name == "idle":
            small = rgba.resize((SMALL, SMALL), Image.Resampling.LANCZOS)
            path = os.path.join(OUT, "sophia_small.z565")
            to_z565(small, path)
            print(f"{'small':7s} {os.path.getsize(path):6d} bytes")


if __name__ == "__main__":
    main()
