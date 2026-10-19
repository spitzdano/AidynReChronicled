#!/usr/bin/env python3
"""make_logo.py: the title card (BORG8_TitleCard, the AIDYN CHRONICLES logo) with its subtitle redrawn.

The game's card has THE FIRST MAGE in the decorative capitals of its old page titles, which looked rough enlarged, and
a TM pressed against the picture's right edge, which the HD pack's redrawn TM ran past (cut off). This keeps the HD
pack's card (hd-ui's strips of it, put back together), fills the old subtitle and TM in from the parchment around
them, and writes the subtitle again in Marcellus SC, silver with a dark edge and a blue glow as the original's, with a
small TM after it inside the picture. Out: art/titles/TitleCard.png at 8x (1504x1392), which tools/hdui/art_pack.py
cuts into the strips the game draws the card in.

    python3 -I tools/branding/make_logo.py      (needs build/cmake/tmemhash and the hd-ui pack)
"""
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools/hdui"))
sys.path.insert(0, str(HERE))
import bigstrips  # noqa: E402
import portraits  # noqa: E402
import romassets  # noqa: E402
from pngio import decode_borg8, read_png, write_png  # noqa: E402
from make_branding import MARCELLUS, SS, blur, cap_rows, dilate, gradient, ink_width, over, rgb, text_mask, to_rgba8  # noqa: E402

HD_UI = Path.home() / ".local/share/AidynReChronicled/texture-packs/hd-ui"
OUT = PROJECT / "art/titles/TitleCard.png"
TEXT, TM = "The First Mage", "TM"
BAND = (119, 141)            # rows of the old subtitle and TM (N64 texels)
LEFT, RIGHT = 22, 172        # the new subtitle's ink spans these columns; the TM follows it, inside the 188
SILVER = [(0.0, "#ffffff"), (0.45, "#eef2fa"), (0.8, "#c3cde2"), (1.0, "#9eabc8")]


def card():
    big = bigstrips.big_images(PROJECT / "baserom.z64", PROJECT / "decomp")
    return [img for name, img in big.values() if name == "TitleCard"][0]


def hd_card(img):
    """The hd-ui pack's card, from its strips: (scale, H x W x 4 uint8)."""
    fmt, w, h = img[:3]
    jobs = bigstrips.jobs(img)
    hashes = portraits.hashes(PROJECT / "build/cmake/tmemhash", [j for _, _, j in jobs])
    paths = {t["hashes"]["rt64"]: t["path"] for t in json.loads((HD_UI / "rt64.json").read_text())["textures"]}
    out, scale = None, None
    for (y0, rows, _), h_ in zip(jobs, hashes):
        if h_ not in paths:
            sys.exit(f"make_logo.py: the hd-ui pack has no strip {h_} of the title card")
        W, H, rgba = read_png(HD_UI / paths[h_])
        strip = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4)
        if out is None:
            scale = W // w
            out = np.zeros((h * scale, w * scale, 4), np.uint8)
        out[y0 * scale:y0 * scale + H] = strip[:h * scale - y0 * scale]
    return scale, out


def subtitle_mask(img):
    """Texels of the old subtitle and TM: anything in the band that isn't parchment (the blue edge, the white letters,
    their black outline), grown by a texel."""
    fmt, w, h, pixels, palette = img
    o = np.frombuffer(bytes(decode_borg8(fmt, w, h, pixels, palette)), np.uint8).reshape(h, w, 4).astype(int)
    r, g, b, a = o[..., 0], o[..., 1], o[..., 2], o[..., 3]
    parchment = (r > b + 25) & (a > 0)
    m = np.zeros((h, w), bool)
    m[BAND[0]:BAND[1]] = ~parchment[BAND[0]:BAND[1]] & (a[BAND[0]:BAND[1]] > 0)
    m |= np.roll(m, 1, 0) | np.roll(m, -1, 0)
    m |= np.roll(m, 1, 1) | np.roll(m, -1, 1)
    m[:BAND[0] - 1] = False  # keep CHRONICLES' underline
    return m, a > 0


def hd_subtitle_mask(big):
    """The same at 8x, from the HD card itself: its redrawn TM and the old letters' halo reach past the original's
    texels. Anything in the band that isn't parchment-coloured and isn't clear, grown by a texel."""
    r, g, b, a = (big[..., k].astype(int) for k in range(4))
    m = np.zeros(a.shape, bool)
    y0, y1 = BAND[0] * SS, BAND[1] * SS
    m[y0:y1] = ~(r[y0:y1] > b[y0:y1] + 25) & (a[y0:y1] > 12)
    return dilate(m.astype(np.float32), SS) > 0.5


def fill(rgba, mask, iterations=600):
    """Diffusion fill of the masked pixels (colour and coverage) from their surroundings."""
    f = rgba.astype(np.float32) / 255.0
    pm = np.concatenate([f[..., :3] * f[..., 3:], f[..., 3:]], -1)  # premultiplied
    pm[mask] = 0.0
    known = ~mask
    for _ in range(iterations):
        avg = (np.roll(pm, 1, 0) + np.roll(pm, -1, 0) + np.roll(pm, 1, 1) + np.roll(pm, -1, 1)) / 4.0
        pm = np.where(known[..., None], pm, avg)
    a = pm[..., 3:]
    colour = np.where(a > 1e-4, pm[..., :3] / np.maximum(a, 1e-4), 0.0)
    a = np.where(mask[..., None], np.clip((a - 0.35) / 0.3, 0, 1), a)  # parchment's edge stays crisp
    return np.concatenate([colour * a, a], -1)  # premultiplied float, as make_branding's pictures


def lettering(img_pm, W, H):
    """The subtitle and the TM over the filled card (premultiplied float, 8x)."""
    size = 300
    while ink_width(TEXT, MARCELLUS, size) > (RIGHT - LEFT) * SS:
        size -= 4
    width = ink_width(TEXT, MARCELLUS, size)
    baseline = 137 * SS
    x = LEFT * SS + ((RIGHT - LEFT) * SS - width) // 2
    m = dilate(text_mask(TEXT, MARCELLUS, size, W, H, x, baseline), 3)  # a little heavier than the font
    top, base = cap_rows(MARCELLUS, size, baseline, H)
    over(img_pm, rgb("#3a64ff"), 0.7 * blur(dilate(m, 12), 9))    # blue glow, as the original's edge
    over(img_pm, rgb("#0c1022"), dilate(m, 6))                     # dark outline
    f = (np.arange(H, dtype=np.float32)[:, None] - top) / max(base - top, 1)
    over(img_pm, gradient(SILVER, np.broadcast_to(f, (H, W))), m)
    tm_size = size * 0.34
    tx = x + width + 2 * SS
    if tx + ink_width(TM, MARCELLUS, tm_size) > 186 * SS:
        sys.exit("make_logo.py: the TM doesn't fit inside the card")
    t = text_mask(TM, MARCELLUS, tm_size, W, H, tx, top + int((base - top) * 0.45))
    over(img_pm, rgb("#0c1022"), dilate(t, 3))
    over(img_pm, rgb("#f4f6fb"), t)
    return size


def main():
    img = card()
    fmt, w, h = img[:3]
    scale, hd = hd_card(img)
    W, H = w * SS, h * SS
    up = np.asarray(bytearray(hd.tobytes()), np.uint8).reshape(hd.shape)
    # to 8x (Lanczos) with magick
    import subprocess, tempfile
    with tempfile.TemporaryDirectory() as t:
        src, dst = Path(t) / "a.png", Path(t) / "b.png"
        write_png(src, hd.shape[1], hd.shape[0], up.tobytes())
        subprocess.run(["magick", str(src), "-filter", "Lanczos", "-resize", f"{W}x{H}!", f"PNG32:{dst}"], check=True)
        pw, ph, rgba = read_png(dst)
    big = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4)
    mask1, _ = subtitle_mask(img)
    mask = np.kron(mask1, np.ones((SS, SS), bool)) | hd_subtitle_mask(big)
    pm = fill(big, mask)
    size = lettering(pm, W, H)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    write_png(OUT, W, H, to_rgba8(pm).tobytes())
    print(f"make_logo.py: subtitle at size {size}, {mask1.sum()} texels refilled -> {OUT}")


if __name__ == "__main__":
    main()
