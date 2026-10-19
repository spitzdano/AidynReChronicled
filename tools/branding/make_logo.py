#!/usr/bin/env python3
"""make_logo.py: the title card (BORG8_TitleCard, the AIDYN CHRONICLES logo) with its TM inside the picture.

The game's card has its TM pressed against the picture's right edge (x 174..187 of 188); the HD pack's redrawn TM came
out wider and ran past the edge, cut off. This keeps the HD pack's card as it is (hd-ui's strips of it, put back
together: the map and THE FIRST MAGE untouched), clears that TM (grey letters over nothing) and writes a slightly
smaller one in Marcellus that ends inside the card. Out: art/titles/TitleCard.png at 8x (1504x1392), which
tools/hdui/art_pack.py cuts into the strips the game draws the card in.

    python3 -I tools/branding/make_logo.py      (needs build/cmake/tmemhash and the hd-ui pack)
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools/hdui"))
sys.path.insert(0, str(HERE))
import bigstrips  # noqa: E402
import portraits  # noqa: E402
from pngio import read_png, write_png  # noqa: E402
from make_branding import MARCELLUS, SS, dilate, ink_width, over, rgb, text_mask, to_rgba8  # noqa: E402

HD_UI = Path.home() / ".local/share/AidynReChronicled/texture-packs/hd-ui"
OUT = PROJECT / "art/titles/TitleCard.png"
TM_BOX = (173, 126, 188, 142)  # x0, y0, x1, y1 (N64 texels): the old TM, over nothing
TM_RIGHT, TM_BASE = 185.5, 138.5  # the new TM ends here, on this baseline (the old one's)
TM_CAPS = 5.5                     # its capitals' height in texels (the old one's were 7)


def card():
    big = bigstrips.big_images(PROJECT / "baserom.z64", PROJECT / "decomp")
    return [img for name, img in big.values() if name == "TitleCard"][0]


def hd_card(img):
    """The hd-ui pack's card, from its strips: H x W x 4 uint8 at the pack's scale."""
    fmt, w, h = img[:3]
    jobs = bigstrips.jobs(img)
    hashes = portraits.hashes(PROJECT / "build/cmake/tmemhash", [j for _, _, j in jobs])
    paths = {t["hashes"]["rt64"]: t["path"] for t in json.loads((HD_UI / "rt64.json").read_text())["textures"]}
    out = None
    for (y0, rows, _), h_ in zip(jobs, hashes):
        if h_ not in paths:
            sys.exit(f"make_logo.py: the hd-ui pack has no strip {h_} of the title card")
        W, H, rgba = read_png(HD_UI / paths[h_])
        strip = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4)
        scale = W // w
        if out is None:
            out = np.zeros((h * scale, w * scale, 4), np.uint8)
        out[y0 * scale:y0 * scale + H] = strip[:h * scale - y0 * scale]
    return out


def to_8x(hd, W, H):
    with tempfile.TemporaryDirectory() as t:
        src, dst = Path(t) / "a.png", Path(t) / "b.png"
        write_png(src, hd.shape[1], hd.shape[0], hd.tobytes())
        subprocess.run(["magick", str(src), "-filter", "Lanczos", "-resize", f"{W}x{H}!", f"PNG32:{dst}"], check=True)
        _, _, rgba = read_png(dst)
    return np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4).copy()


def main():
    img = card()
    w, h = img[1], img[2]
    W, H = w * SS, h * SS
    big = to_8x(hd_card(img), W, H)
    # Clear the old TM: its grey letters and their dark edge, not the blue glow of MAGE's E beside it.
    x0, y0, x1, y1 = (v * SS for v in TM_BOX)
    box = big[y0:y1, x0:x1].astype(int)
    blue = box[..., 2] > box[..., 0] + 30
    box[~blue] = 0
    big[y0:y1, x0:x1] = box.astype(np.uint8)
    # The new TM.
    f = big.astype(np.float32) / 255.0
    pm = np.concatenate([f[..., :3] * f[..., 3:], f[..., 3:]], -1)
    size = 60.0
    while True:  # point size for capitals TM_CAPS texels tall
        m = text_mask("T", MARCELLUS, size, 400, 400, 50, 300)
        rows = np.nonzero(m.max(axis=1) > 0.5)[0]
        if rows.max() - rows.min() + 1 >= TM_CAPS * SS:
            break
        size += 1.0
    x = int(TM_RIGHT * SS) - ink_width("TM", MARCELLUS, size)
    t = text_mask("TM", MARCELLUS, size, W, H, x, int(TM_BASE * SS))
    over(pm, rgb("#16182a"), dilate(t, 3))
    over(pm, rgb("#eef0f6"), t)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    write_png(OUT, W, H, to_rgba8(pm).tobytes())
    print(f"make_logo.py: TM at size {size:.0f}, from x {x / SS:.1f} to {TM_RIGHT} -> {OUT}")


if __name__ == "__main__":
    main()
