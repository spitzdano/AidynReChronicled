#!/usr/bin/env python3
"""make_logo.py: the title card (BORG8_TitleCard, the AIDYN RECHRONICLED logo) and its shadow, from spitzdano's redesign.

art/titles/source/aidyn-rechronicled-logo.png (the logo redrawn whole: the parchment, AIDYN, RECHRONICLED and THE
FIRST MAGE, on nothing; the name was AIDYN CHRONICLES until 1.0.0) is fitted into the card's 188x174 texels, centred, and written at 8x as
art/titles/TitleCard.png. The card's shadow (BORG8_TitleCardShadow, 213x180: the card's outline 19 texels right and 3
down, softened, 60% dark) is made again from the new outline: art/titles/TitleCardShadow.png. tools/hdui/art_pack.py
cuts both into the strips the game draws them in. With the redesign on screen the game no longer draws its own
"Rechronicled" under the card (src/game/patches.cpp, "Title screen").

    python3 -I tools/branding/make_logo.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools/hdui"))
from pngio import read_png, write_png  # noqa: E402

SS = 8
SOURCE = PROJECT / "art/titles/source/aidyn-rechronicled-logo.png"
CARD, SHADOW = (188, 174), (213, 180)
SHADOW_OFFSET = (19, 3)     # texels: where the card's outline sits in the shadow picture
SHADOW_ALPHA = 0.6          # the original's darkest (153 of 255)


def load(path):
    w, h, rgba = read_png(path)
    return np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32) / 255.0


def resized(img, w, h):
    with tempfile.TemporaryDirectory() as t:
        a, b = Path(t) / "a.png", Path(t) / "b.png"
        write_png(a, img.shape[1], img.shape[0], (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
        subprocess.run(["magick", str(a), "-filter", "Lanczos", "-resize", f"{w}x{h}!", f"PNG32:{b}"], check=True)
        return load(b)


def blur(m, r):
    for axis in (0, 1):
        for _ in range(3):
            c = np.cumsum(np.pad(m, [(r + 1, r) if a == axis else (0, 0) for a in (0, 1)], mode="constant"), axis=axis)
            m = (np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis)
                 - np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)) / (2 * r + 1)
    return m


def main():
    logo = load(SOURCE)
    ys, xs = np.nonzero(logo[..., 3] > 0.02)
    logo = logo[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    W, H = CARD[0] * SS, CARD[1] * SS
    margin = 1 * SS
    scale = min((W - 2 * margin) / logo.shape[1], (H - 2 * margin) / logo.shape[0])
    lw, lh = round(logo.shape[1] * scale), round(logo.shape[0] * scale)
    small = resized(logo, lw, lh)
    card = np.zeros((H, W, 4), np.float32)
    x0, y0 = (W - lw) // 2, (H - lh) // 2
    card[y0:y0 + lh, x0:x0 + lw] = small
    out = PROJECT / "art/titles/TitleCard.png"
    write_png(out, W, H, (np.clip(card, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
    print(f"make_logo.py: the logo at {lw / SS:.1f}x{lh / SS:.1f} texels in the card -> {out}")
    # The shadow: the outline, moved, softened, dark.
    SW, SH = SHADOW[0] * SS, SHADOW[1] * SS
    a = np.zeros((SH, SW), np.float32)
    dx, dy = SHADOW_OFFSET[0] * SS, SHADOW_OFFSET[1] * SS
    h, w = min(H, SH - dy), min(W, SW - dx)
    a[dy:dy + h, dx:dx + w] = card[:h, :w, 3]
    a = np.clip(blur(a, SS // 2), 0, 1) * SHADOW_ALPHA
    shadow = np.zeros((SH, SW, 4), np.float32)
    shadow[..., :3] = 17 / 255.0
    shadow[..., 3] = a
    out = PROJECT / "art/titles/TitleCardShadow.png"
    write_png(out, SW, SH, (np.clip(shadow, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
    print(f"make_logo.py: the shadow -> {out}")


if __name__ == "__main__":
    main()
