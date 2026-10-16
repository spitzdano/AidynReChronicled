"""hudart.py: small UI pictures redrawn as clean shapes for the HD packs (used by romimages.py): the time-of-day dial's
pieces, the pause pages' arrows and the HitPoints bar. Drawn here, not taken from the ROM, so art_pack.py ships them.

The dial (Sundial::Draw) is built from small white pictures the game tints as the day goes by: the rays
(SundailCross, 50x50) and six moon phases and a big and a small sun (20x20 each). Upscaled, they came out lumpy, and a
moon's disc touches the edges of its 20x20 picture, so at high resolution it showed cut flat on four sides. Here they
are drawn as shapes measured from the originals, at 8 pixels a texel (supersampled 4x, box-filtered):
  SundailCross     12 kite-shaped rays at 30-degree steps: the four main ones from 14 to 22 texels out, the others
                   from 10.4 (or 12.1) to 17.3
  SundailMoon0..5  a disc (radius 9.3, inside its picture now) lit as the phase, its dark side a glowing rim:
                   0 new, 1 crescent lit on the right, 2 on the left, 3 right half, 4 left half, 5 full
  SundailSunBig    a solid core of 4.5 texels in a glow that fades out by 9.5
  SundailSunSmall  a dot of 2.3 texels
All white (the game tints them), alpha only.

The pause pages' arrows (SliderArrowL 36x8, SliderArrowR 36x8, SliderArrowLR 47x8; white, tinted dark red): upscaled
from 8 texels they came out as blurred zigzags. Redrawn: a 1.1-texel shaft, a notched arrowhead 6.2 tall with a small
cross-guard behind it, a ring at the tail; the double arrow has a head at each end and a ring in the middle.

HealthBar (77x8 colour, drawn stretched to 80x10, cut from the left to show the hit points left): a twisted rope.
Its original's faint middle stretch upscaled into a smear. Redrawn: strands slanting down to the right every 3
texels, each shaded round (light crest, dark groove), the rope shaded darker towards its top and bottom edges.
"""
import math

import numpy as np

NAMES = ({"SundailCross", "SundailSunSmall", "SundailSunBig"} | {f"SundailMoon{i}" for i in range(6)}
         | {"SliderArrowL", "SliderArrowR", "SliderArrowLR", "HealthBar"})
SCALE = 8
SS = 4  # supersampling


def _coords(w, h):
    """Texel coordinates of every supersample, origin at the picture's centre, y up."""
    n = SCALE * SS
    ys, xs = np.mgrid[0:h * n, 0:w * n]
    return (xs + 0.5) / n - w / 2.0, h / 2.0 - (ys + 0.5) / n


def _down(alpha, w, h):
    return alpha.reshape(h * SCALE, SS, w * SCALE, SS).mean(axis=(1, 3))


def _rays(w, h):
    x, y = _coords(w, h)
    r, ang = np.hypot(x, y), np.arctan2(y, x)
    alpha = np.zeros_like(r)
    for k in range(12):
        deg = k * 30
        if deg % 90 == 0:
            r0, r1, width = 14.0, 22.0, 3.6
        elif deg in (30, 150, 210, 330):
            r0, r1, width = 10.4, 17.3, 3.0
        else:  # 60, 120, 240, 300
            r0, r1, width = 12.1, 17.3, 3.0
        a = math.radians(deg)
        along = x * math.cos(a) + y * math.sin(a)
        across = np.abs(-x * math.sin(a) + y * math.cos(a))
        t = (along - r0) / (r1 - r0)
        half = np.where(t < 0.6, t / 0.6, (1 - t) / 0.4) * width / 2  # kite: widest 60% of the way out
        alpha = np.maximum(alpha, ((t >= 0) & (t <= 1) & (across <= half)).astype(np.float32))
    return alpha


def _moon(w, h, phase):
    x, y = _coords(w, h)
    R = 9.3
    r = np.hypot(x, y)
    inside = r <= R
    rim = np.clip((r - (R - 5.5)) / 5.5, 0.0, 1.0) ** 1.2  # the dark side: a glowing limb
    yy = np.clip(1.0 - (y / R) ** 2, 0.0, 1.0)
    k = 0.35 * R * np.sqrt(yy)
    lit = {0: np.zeros_like(r, bool), 1: x > k, 2: x < -k, 3: x > 0, 4: x < 0, 5: np.ones_like(r, bool)}[phase]
    return np.where(inside, np.where(lit, 1.0, rim), 0.0).astype(np.float32)


def _sun(w, h, core, glow):
    x, y = _coords(w, h)
    r = np.hypot(x, y)
    t = np.clip((r - core) / (glow - core), 0.0, 1.0)
    return np.where(r <= core, 1.0, (1 - t) ** 2).astype(np.float32)


def _grid(w, h):
    """Texel coordinates of every supersample, origin top left, y down."""
    n = SCALE * SS
    ys, xs = np.mgrid[0:h * n, 0:w * n]
    return (xs + 0.5) / n, (ys + 0.5) / n


def _polygon(x, y, pts):
    """Inside the polygon (even-odd)."""
    inside = np.zeros(x.shape, bool)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        crosses = (y0 > y) != (y1 > y)
        xc = x0 + (y - y0) * (x1 - x0) / ((y1 - y0) if y1 != y0 else 1e-9)
        inside ^= crosses & (x < xc)
    return inside


def _arrow(w, h, left, right, middle):
    """An arrow along y = h/2: a head at the left end and/or the right end (else a ring there), a ring in the middle."""
    x, y = _grid(w, h)
    cy = h / 2.0
    m = np.zeros(x.shape, bool)

    def head(tip, d):  # d = +1: points left (tip at the left end); -1: points right
        bx, nx, gx = tip + d * 5.6, tip + d * 4.2, tip + d * 6.4
        pts = [(tip, cy), (bx, cy - 3.1), (nx, cy), (bx, cy + 3.1)]
        guard = (np.abs(x - gx) <= 0.42) & (np.abs(y - cy) <= 1.7)
        return _polygon(x, y, pts) | guard, nx

    def ring(cx, r):
        d = np.hypot(x - cx, y - cy)
        return (d <= r) & (d >= r - 0.75), cx

    ends = []
    for side, is_head in ((0, left), (1, right)):
        tip = 0.5 if side == 0 else w - 0.5
        d = 1 if side == 0 else -1
        if is_head:
            shape, inner = head(tip, d)
        else:
            shape, c = ring(tip + d * 1.5, 1.5)
            inner = c + d * 1.5
        m |= shape
        ends.append(inner)
    segments = [(ends[0], ends[1])]
    if middle:
        shape, c = ring(w / 2.0, 2.1)
        m |= shape
        segments = [(ends[0], c - 2.1), (c + 2.1, ends[1])]
    for a, b in segments:
        m |= (x >= min(a, b)) & (x <= max(a, b)) & (np.abs(y - cy) <= 0.55)
    return m.astype(np.float32)


def _rope(w, h):
    """The HitPoints bar: (colour H x W x 3 at 8x, alpha)."""
    x, y = _grid(w, h)
    top, bottom = 0.55, h - 0.55
    half = (bottom - top) / 2.0
    cy = (top + bottom) / 2.0
    period, slant = 3.0, 0.85
    u = x - (y - cy) * slant
    f = np.mod(u, period) / period
    crest = np.sin(np.pi * f) ** 0.7                        # 0 in the groove, 1 on a strand's crest
    across = np.clip(1.0 - ((y - cy) / half) ** 2, 0.0, 1.0) ** 0.5  # the rope's roundness, top to bottom
    light = np.clip(0.15 + 0.62 * crest * (0.35 + 0.65 * across) + 0.25 * np.clip(1 - f * 2.2, 0, 1) * crest, 0, 1)
    stops = [(0.0, (0x2c, 0x14, 0x08)), (0.22, (0x5e, 0x2f, 0x15)), (0.5, (0xa8, 0x6a, 0x3a)),
             (0.78, (0xdc, 0xab, 0x74)), (1.0, (0xf6, 0xdc, 0xae))]
    colour = np.zeros(x.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        t = np.clip((light - p0) / (p1 - p0), 0, 1)[..., None]
        seg = ((light >= p0) & (light <= p1))[..., None]
        colour = np.where(seg, np.array(c0, np.float32) * (1 - t) + np.array(c1, np.float32) * t, colour)
    groove = (f < 0.07) | (f > 0.93)
    colour = np.where(groove[..., None], np.array((0x24, 0x10, 0x06), np.float32), colour)
    edge = 1.0 - np.clip((np.abs(y - cy) - (half - 0.45)) / 0.45, 0, 1)  # dark rim along the top and bottom
    colour *= (0.55 + 0.45 * edge)[..., None]
    alpha = ((y >= top) & (y <= bottom)).astype(np.float32)
    return colour, alpha


def redraw(name, w, h, rgba=None):
    """(W, H, rgba bytes) at 8x for one of NAMES."""
    if name == "HealthBar":
        colour, alpha = _rope(w, h)
        n = SCALE
        c = colour.reshape(h * n, SS, w * n, SS, 3).mean(axis=(1, 3))
        a = _down(alpha, w, h)
        out = np.zeros(a.shape + (4,), np.uint8)
        out[..., :3] = np.clip(c + 0.5, 0, 255).astype(np.uint8)
        out[..., 3] = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
        return w * SCALE, h * SCALE, bytearray(out.tobytes())
    if name.startswith("SliderArrow"):
        kind = name[len("SliderArrow"):]
        alpha = _arrow(w, h, "L" in kind, "R" in kind, kind == "LR")
    elif name == "SundailCross":
        alpha = _rays(w, h)
    elif name == "SundailSunBig":
        alpha = _sun(w, h, 4.5, 9.5)
    elif name == "SundailSunSmall":
        alpha = _sun(w, h, 2.3, 2.3 + 1.0 / SCALE)
    else:
        alpha = _moon(w, h, int(name[-1]))
    a = _down(alpha, w, h)
    out = np.full(a.shape + (4,), 255, np.uint8)
    out[..., 3] = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return w * SCALE, h * SCALE, bytearray(out.tobytes())
