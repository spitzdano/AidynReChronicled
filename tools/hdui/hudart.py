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

The journal's arrows (LongUpArrow, LongDownArrow, 8x36, the same design stood up) are SliderArrowL / R drawn 36x8 and
turned clockwise.

HealthBar (77x8 colour, drawn stretched to 80x10, cut from the left to show the hit points left): a twisted rope.
Its original's faint middle stretch upscaled into a smear. Redrawn in the original's reddish browns: a strand every
4.6 texels, each round and lit from the upper left with fine fibres and a little variety from strand to strand; the
grooves pinch the outline in, so the page shows between strands at the edges as in the original.
"""
import math

import numpy as np

NAMES = ({"SundailCross", "SundailSunSmall", "SundailSunBig"} | {f"SundailMoon{i}" for i in range(6)}
         | {"SliderArrowL", "SliderArrowR", "SliderArrowLR", "HealthBar", "LongUpArrow", "LongDownArrow"}
         | {"CompassNorth", "CompassEast", "CompassSouth", "CompassWest"})
LETTERS = {"CompassNorth": "N", "CompassEast": "E", "CompassSouth": "S", "CompassWest": "W"}
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
    cy = h / 2.0
    r = h / 2.0 - 0.35                                     # the rope's radius at a strand's crest
    d = (y - cy) / r                                        # -1 top .. 1 bottom
    period, slant = 4.6, 0.95                               # the original's twist: a strand every 4.6 texels
    u = x + (y - cy) * slant                                # strand boundaries are lines of constant u
    k = np.floor(u / period)                                # which strand
    f = u / period - k                                      # 0..1 across one strand
    lobe = np.sin(np.pi * f) ** 0.55                        # a strand's round cross-section
    radius = 0.80 + 0.20 * np.sin(np.pi * f) ** 0.8         # strands bulge, grooves pinch in: the page shows between
    alpha = (np.abs(d) <= radius).astype(np.float32)
    across = np.sqrt(np.clip(1.0 - (d / radius) ** 2, 0, 1))  # the rope's roundness, top to bottom
    side = np.clip(1.2 - 1.6 * f, 0.35, 1.0)                # each strand's left side faces the light
    top = 0.72 + 0.28 * np.clip(-d, -1, 1)                  # the upper half a little brighter
    jitter = np.random.default_rng(7).uniform(0.9, 1.08, 64)[k.astype(int) % 64]  # strands not all alike
    v = x + (y - cy) * slant * 1.9                          # fibres, a little steeper than the strands
    phase = 2 * np.pi * v / 0.38 + 1.7 * np.sin(2 * np.pi * k / 5.3)
    fibre = 1.0 + 0.18 * (0.6 * np.sin(phase) + 0.4 * np.sin(2.3 * phase + 1.1))
    shade = lobe * across * (0.45 + 0.55 * side) * top
    light = np.clip(shade ** 0.72 * jitter * fibre + 0.3 * np.clip((shade - 0.62) / 0.38, 0, 1) ** 2, 0, 1)
    stops = [(0.0, (0x40, 0x1c, 0x0a)), (0.22, (0x6b, 0x31, 0x18)), (0.45, (0x94, 0x63, 0x4a)),
             (0.7, (0xbd, 0x9c, 0x84)), (1.0, (0xec, 0xdb, 0xc4))]  # the original's reddish browns
    colour = np.zeros(x.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        t = np.clip((light - p0) / (p1 - p0), 0, 1)[..., None]
        seg = ((light >= p0) & (light <= p1))[..., None]
        colour = np.where(seg, np.array(c0, np.float32) * (1 - t) + np.array(c1, np.float32) * t, colour)
    return colour, alpha


def _letter(text, w, h):
    """A compass letter in the menus' font (Marcellus SC): white, capitals 10 texels tall (as the original's), centred, with a soft dark
    edge so it reads on grass and sky (the game tints it and fades the ones facing away)."""
    import subprocess
    import tempfile
    from pathlib import Path
    from pngio import read_png
    font = Path(__file__).resolve().parent / "fonts/MarcellusSC-Regular.ttf"
    W, H = w * SCALE, h * SCALE
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "l.png"
        subprocess.run(["magick", "-background", "none", "-fill", "white", "-font", str(font), "-pointsize", "300",
                        f"label:{text}", "-trim", "+repage", "-resize", f"x{10 * SCALE}", f"PNG32:{out}"],
                       check=True, stdout=subprocess.DEVNULL)
        lw, lh, l = read_png(out)
    glyph = np.frombuffer(bytes(l), np.uint8).reshape(lh, lw, 4)[..., 3].astype(np.float32) / 255
    a = np.zeros((H, W), np.float32)
    x0, y0 = (W - lw) // 2, (H - lh) // 2
    a[y0:y0 + lh, x0:x0 + lw] = glyph
    # the edge: the glyph grown by about a texel, blurred
    edge = a.copy()
    for _ in range(SCALE):
        edge = np.maximum.reduce([edge, np.roll(edge, 1, 0), np.roll(edge, -1, 0), np.roll(edge, 1, 1), np.roll(edge, -1, 1)])
    k = SCALE // 2
    for axis in (0, 1):
        edge = sum(np.roll(edge, d, axis) for d in range(-k, k + 1)) / (2 * k + 1)
    alpha = np.maximum(a, edge * 0.7)
    out = np.zeros((H, W, 4), np.uint8)
    out[..., :3] = np.clip(255 * a / np.maximum(alpha, 1e-6) + 0.5, 0, 255).astype(np.uint8)[..., None]
    out[..., 3] = np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8)
    return W, H, bytearray(out.tobytes())


def redraw(name, w, h, rgba=None):
    """(W, H, rgba bytes) at 8x for one of NAMES."""
    if name in LETTERS:
        return _letter(LETTERS[name], w, h)
    if name == "HealthBar":
        colour, alpha = _rope(w, h)
        n = SCALE
        cover = alpha.reshape(h * n, SS, w * n, SS).sum(axis=(1, 3))[..., None]  # colour of the rope's samples only
        c = (colour * alpha[..., None]).reshape(h * n, SS, w * n, SS, 3).sum(axis=(1, 3)) / np.maximum(cover, 1e-6)
        a = _down(alpha, w, h)
        out = np.zeros(a.shape + (4,), np.uint8)
        out[..., :3] = np.clip(c + 0.5, 0, 255).astype(np.uint8)
        out[..., 3] = np.clip(a * 255.0 + 0.5, 0, 255).astype(np.uint8)
        return w * SCALE, h * SCALE, bytearray(out.tobytes())
    if name.startswith("SliderArrow"):
        kind = name[len("SliderArrow"):]
        alpha = _arrow(w, h, "L" in kind, "R" in kind, kind == "LR")
    elif name in ("LongUpArrow", "LongDownArrow"):  # the journal's: the page arrow stood up (8x36), turned clockwise
        alpha = np.rot90(_arrow(h, w, name == "LongUpArrow", name == "LongDownArrow", False), -1)
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
