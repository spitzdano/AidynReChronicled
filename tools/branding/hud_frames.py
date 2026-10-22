"""hud_frames.py: spitzdano's ornate portrait frames and gold ring (art/hud/source) fitted to the HUD.

  frame-gold.png, frame-red.png  a square frame around a portrait (gold: the party, red and silver: enemies)
  ring-gold.png                  a gold ring

Made from them:
  - for make_branding.py (drawn by src/game/patches.cpp, "Portrait frames"): frame_party and frame_enemy, 31x31,
    around a combat column's 23x23 portrait (its hole on texels 4..27); frame_glow, 35x35, a soft band around a
    frame, tinted by the game's colour for the slot (in reach, targeted);
  - for the redrawn art pack (art/hud, written by write_art()): PortraitHighlight (52x52: the white frame of the field's
    party portraits, the dialogue portrait and the combat message box's), the frame fitted whole into it (its hole
    about 39 texels round the 40x40 portrait); PortraitHighlightEnemy, the red one, which the message box shows on an
    enemy's turn (a copy of PortraitHighlight with its middle texel changed, so it has a texture of its own); and
    CompassRing (60x60: the ring of the time-of-day dial, the compass and the combat radar), the gold ring on the
    original's ring (radius 22) with the original's dark disc inside and a soft shadow outside.
"""
from pathlib import Path

import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
from pngio import read_png, write_png  # noqa: E402

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SOURCE = PROJECT / "art/hud/source"
ART = PROJECT / "art/hud"
S = 8  # pixels per texel


def _load(name):
    w, h, rgba = read_png(SOURCE / name)
    a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32) / 255
    a[..., :3] *= a[..., 3:]  # premultiplied
    return a


def _hole(a):
    """The opaque bounding box and the transparent hole round the middle: (x0, y0, x1, y1) each, in pixels."""
    op = a[..., 3] > 0.5
    ys, xs = np.nonzero(op)
    H, W = op.shape

    def run(line):
        i = len(line) // 2
        lo, hi = i, i
        while lo > 0 and not line[lo - 1]:
            lo -= 1
        while hi < len(line) - 1 and not line[hi + 1]:
            hi += 1
        return lo, hi + 1

    hx = np.median([run(op[y])[0] for y in range(H // 3, 2 * H // 3, 8)]), np.median([run(op[y])[1] for y in range(H // 3, 2 * H // 3, 8)])
    hy = np.median([run(op[:, x])[0] for x in range(W // 3, 2 * W // 3, 8)]), np.median([run(op[:, x])[1] for x in range(W // 3, 2 * W // 3, 8)])
    return (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1), (hx[0], hy[0], hx[1], hy[1])


def _place(a, src, dst, size):
    """a's pixel rect src (x0, y0, x1, y1) mapped onto dst (in output pixels) of a canvas size (W, H): premultiplied.
    The source is first box-averaged to about the output's scale, then sampled bilinearly."""
    sx = (src[2] - src[0]) / (dst[2] - dst[0])
    sy = (src[3] - src[1]) / (dst[3] - dst[1])
    f = max(1, int(min(sx, sy)))
    if f > 1:
        H, W = a.shape[0] // f * f, a.shape[1] // f * f
        a = a[:H, :W].reshape(H // f, f, W // f, f, 4).mean(axis=(1, 3))
    sx, sy = sx / f, sy / f
    ox, oy = src[0] / f - dst[0] * sx, src[1] / f - dst[1] * sy
    yy, xx = np.mgrid[0:size[1], 0:size[0]].astype(np.float32) + 0.5
    u, v = xx * sx + ox - 0.5, yy * sy + oy - 0.5
    x0, y0 = np.floor(u).astype(int), np.floor(v).astype(int)
    fx, fy = (u - x0)[..., None], (v - y0)[..., None]

    def at(y, x):
        ok = ((x >= 0) & (x < a.shape[1]) & (y >= 0) & (y < a.shape[0]))[..., None]
        return np.where(ok, a[np.clip(y, 0, a.shape[0] - 1), np.clip(x, 0, a.shape[1] - 1)], 0)

    out = (at(y0, x0) * (1 - fx) * (1 - fy) + at(y0, x0 + 1) * fx * (1 - fy)
           + at(y0 + 1, x0) * (1 - fx) * fy + at(y0 + 1, x0 + 1) * fx * fy)
    return np.clip(out, 0, 1).astype(np.float32)


def _frame_on_hole(name, w, h, hole):
    """The frame drawn w x h texels (at S), its hole on the texel rect hole (x0, y0, x1, y1)."""
    a = _load(name)
    _, src_hole = _hole(a)
    return _place(a, src_hole, tuple(v * S for v in hole), (w * S, h * S))


def _frame_whole(name, w, h, margin=0.0):
    """The frame fitted whole (its corners too) into w x h texels."""
    a = _load(name)
    box, _ = _hole(a)
    return _place(a, box, (margin * S, margin * S, (w - margin) * S, (h - margin) * S), (w * S, h * S))


# The combat columns: a 23x23 portrait in a 31x31 frame, its hole on texels 4..27 (a quarter texel over the picture).
FRAME_W, FRAME_HOLE = 31, (3.8, 3.8, 27.2, 27.2)
GLOW_W = 35


def frame_party(w, h):
    return _frame_on_hole("frame-gold.png", w, h, FRAME_HOLE)


def frame_enemy(w, h):
    return _frame_on_hole("frame-red.png", w, h, FRAME_HOLE)


def frame_glow(w, h):
    """White, alpha only: a soft band from the frame's outer edge (2 texels in) to 2 texels outside it."""
    yy, xx = (np.mgrid[0:h * S, 0:w * S] + 0.5) / S
    c = w / 2.0
    d = np.maximum(np.abs(xx - c), np.abs(yy - c))  # square distance from the middle
    edge = FRAME_W / 2.0
    a = np.clip(1 - np.abs(d - (edge - 0.5)) / 2.0, 0, 1) ** 1.5
    out = np.zeros((h * S, w * S, 4), np.float32)
    out[..., :3] = a[..., None]
    out[..., 3] = a
    return out


def ring(size=60, centre=(29.5, 30.0), radius=22.0):
    """CompassRing: the gold ring's middle line on the original's ring, the original's dark disc inside (alpha 0.3) and
    a soft shadow outside it."""
    a = _load("ring-gold.png")
    box, _ = _hole(a)
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    outer = (box[2] - box[0]) / 2
    band = outer * (1 - 0.13 / 2)  # the ring's middle line (its band is about 13% of the radius)
    k = radius / band  # texels a pixel
    W = size * S
    src = (cx - W / 2 / (k * S), cy - W / 2 / (k * S), cx + W / 2 / (k * S), cy + W / 2 / (k * S))
    dst = ((centre[0] * S) - W / 2, (centre[1] * S) - W / 2, (centre[0] * S) + W / 2, (centre[1] * S) + W / 2)
    gold = _place(a, src, dst, (W, W))
    yy, xx = (np.mgrid[0:W, 0:W] + 0.5) / S
    d = np.hypot(xx - centre[0], yy - centre[1])
    under = np.where(d < radius, 0.30, 0.25 * np.clip(1 - (d - radius) / 6.0, 0, 1))
    out = np.zeros_like(gold)
    out[..., 3] = under
    return gold + out * (1 - gold[..., 3:])  # gold over the dark


def _save(img, path):
    a = img[..., 3:]
    colour = np.where(a > 0, img[..., :3] / np.maximum(a, 1e-6), 0)
    rgba = np.clip(np.concatenate([colour, a], -1) * 255 + 0.5, 0, 255).astype(np.uint8)
    path.parent.mkdir(parents=True, exist_ok=True)
    write_png(path, rgba.shape[1], rgba.shape[0], rgba.tobytes())


def write_art():
    _save(_frame_whole("frame-gold.png", 52, 52, 0.3), ART / "PortraitHighlight.png")
    _save(_frame_whole("frame-red.png", 52, 52, 0.3), ART / "PortraitHighlightEnemy.png")
    _save(ring(), ART / "CompassRing.png")
    return [ART / n for n in ("PortraitHighlight.png", "PortraitHighlightEnemy.png", "CompassRing.png")]
