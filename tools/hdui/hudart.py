"""hudart.py: the time-of-day dial's pieces, redrawn as clean shapes for the HD UI pack (used by romimages.py).

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
"""
import math

import numpy as np

NAMES = {"SundailCross", "SundailSunSmall", "SundailSunBig"} | {f"SundailMoon{i}" for i in range(6)}
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


def redraw(name, w, h, rgba=None):
    """(W, H, rgba bytes) at 8x for one of NAMES."""
    if name == "SundailCross":
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
