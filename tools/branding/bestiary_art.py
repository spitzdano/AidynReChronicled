"""bestiary_art.py: the bestiary's drawings (art/bestiary/<Name>.png: one finished drawing per creature, 1008 x 320, painted by Codex), turned
into the three stages the page shows as Alaron meets more of them, for make_branding.py:

  stage 1  a rough pencil sketch   (after 1 fight)   the outline only, in loose graphite, wobbled and broken, doubled in places
  stage 2  a refined ink sketch    (after 3 fights)  the ink lines and the hatching of the drawing, no colour
  stage 3  the finished drawing    (after 5 fights)  as painted

Each stage is the 126 x 40 unit frame cut into three tiles of 42 x 40 units (a picture is at most one 4 KB load of the N64's texture memory),
named bsd_<slug>_<stage>_<tile>: the slug is the creature's name in lower case without spaces, as src/game/patches.cpp makes it. A tile with
nothing in it is left out (the page draws the ones there are). The stages are made from the finished drawing alone: nothing is drawn by hand.
"""
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
from pngio import read_png  # noqa: E402

SOURCE = Path(__file__).resolve().parents[2] / "art/bestiary"
S = 8
FRAME_W, FRAME_H, TILE_W = 126, 40, 42
INK = np.array([0x4A, 0x34, 0x3A], np.float32) / 255
GRAPHITE = np.array([0x5A, 0x4C, 0x52], np.float32) / 255


def slug(name):
    return "".join(c for c in name.lower() if c.isalnum())


def _load(path):
    w, h, rgba = read_png(path)
    if (w, h) != (FRAME_W * S, FRAME_H * S):
        raise SystemExit(f"bestiary_art.py: {path.name} is {w} x {h}, it must be {FRAME_W * S} x {FRAME_H * S}")
    a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32) / 255
    return a[..., :3], a[..., 3]


def _blur(a, sigma):
    r = max(1, int(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 1, a)
    return np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 0, out)


def _smooth_noise(shape, scale, rng):
    """Low-frequency noise, about -1..1, features of `scale` pixels."""
    small = rng.standard_normal((shape[0] // scale + 3, shape[1] // scale + 3)).astype(np.float32)
    small = _blur(small, 1.0)
    ys = np.linspace(0, small.shape[0] - 1.001, shape[0])
    xs = np.linspace(0, small.shape[1] - 1.001, shape[1])
    y0, x0 = ys.astype(int), xs.astype(int)
    fy, fx = (ys - y0)[:, None], (xs - x0)[None, :]
    n = (small[y0][:, x0] * (1 - fy) * (1 - fx) + small[y0][:, x0 + 1] * (1 - fy) * fx
         + small[y0 + 1][:, x0] * fy * (1 - fx) + small[y0 + 1][:, x0 + 1] * fy * fx)
    return n / max(1e-6, np.abs(n).max())


def _warp(a, dx, dy):
    """a sampled at (x + dx, y + dy), nearest."""
    h, w = a.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w]
    return a[np.clip((ys + dy).astype(int), 0, h - 1), np.clip((xs + dx).astype(int), 0, w - 1)]


def _lines(rgb, alpha):
    """How much ink is at each pixel (0..1): the dark strokes of the painting (its outline and hatching), not its colour washes."""
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    fine = _blur(lum, 1.2)
    local = _blur(lum, 9.0)  # the wash around a stroke: a stroke is what is much darker than that and dark in itself
    dark = np.clip((local - fine) * 5.0, 0, 1) * np.clip((0.55 - fine) * 4.0, 0, 1)
    return np.clip(dark, 0, 1) * (alpha > 0.2)


def _outline(rgb, alpha):
    """The silhouette's edge and the heavy inner strokes: what a first sketch would catch."""
    ink = _lines(rgb, alpha)
    edge = np.abs(_blur(alpha, 2.0) - alpha)  # the picture's own outline, where the painting stops
    heavy = np.clip((_blur(ink, 2.5) - 0.25) * 3.0, 0, 1)  # only where strokes are thick or dense
    return np.clip(edge * 4.0 + heavy * 0.9, 0, 1)


def stage_rough(rgb, alpha, seed):
    rng = np.random.default_rng(seed)
    base = _outline(rgb, alpha)
    out = np.zeros_like(base)
    for k in range(2):  # drawn twice, a little apart, as a hand draws it
        dx = _smooth_noise(base.shape, 40, rng) * 7.0 + _smooth_noise(base.shape, 10, rng) * 2.5
        dy = _smooth_noise(base.shape, 40, rng) * 7.0 + _smooth_noise(base.shape, 10, rng) * 2.5
        stroke = _warp(base, dx, dy)
        gaps = np.clip(_smooth_noise(base.shape, 14, rng) * 2.2 + 0.9 - k * 0.35, 0, 1)  # the pencil lifts: breaks in the line
        grain = np.clip(0.55 + rng.random(base.shape) * 0.7, 0, 1)  # paper tooth
        out = np.maximum(out, stroke * gaps * grain * (0.85 - 0.25 * k))
    out = np.clip(out * 1.5, 0, 1)
    return out, GRAPHITE


def stage_refined(rgb, alpha, seed):
    ink = np.clip(_lines(rgb, alpha) * 1.6, 0, 1)
    edge = np.clip(np.abs(_blur(alpha, 1.5) - alpha) * 5.0, 0, 1)
    return np.clip(np.maximum(ink, edge), 0, 1), INK


def _tiles(img_premul, name, stage):
    out = []
    for t in range(FRAME_W // TILE_W):
        part = img_premul[:, t * TILE_W * S:(t + 1) * TILE_W * S]
        if part[..., 3].max() < 0.02:
            continue  # nothing in this piece
        out.append((f"bsd_{slug(name)}_{stage}_{t}", TILE_W, FRAME_H, part.copy()))
    return out


def _premul(rgb, alpha):
    return np.concatenate([rgb * alpha[..., None], alpha[..., None]], -1).astype(np.float32)


def build(path):
    name = path.stem
    rgb, alpha = _load(path)
    seed = int.from_bytes(name.encode(), "little") % (2 ** 32)
    out = []
    for stage, (maker) in ((1, stage_rough), (2, stage_refined)):
        mask, colour = maker(rgb, alpha, seed)
        out += _tiles(_premul(np.broadcast_to(colour, rgb.shape), mask), name, stage)
    out += _tiles(_premul(rgb, alpha), name, 3)
    return out


def _entry(spec):
    n, w, h, data = spec
    return (n, w, h, lambda ww, hh, d=data: d)


IMAGES = []
if SOURCE.is_dir():
    for _p in sorted(SOURCE.glob("*.png")):
        IMAGES += [_entry(s) for s in build(_p)]
