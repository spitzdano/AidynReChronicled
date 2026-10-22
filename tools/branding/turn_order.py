"""turn_order.py: the combat turn order strip's pieces, cut from spitzdano's concept (art/turn-order/concept.png), for
make_branding.py.

The concept is one painted strip: a left end with the round's plate ("Round 1"), the current fighter's portrait in a
gold frame with a glow, the others' in bronze frames (one silver), a divider, the next round's plate, and a right end.
Each piece is cut at the dark gaps between them, so pieces laid edge to edge make a bar again: a portrait slot is its
frame with the bar on both sides. The concept's portraits and words are taken out: each frame's window is cleared
(the game draws the portrait there), and each plate's "Round N" is filled in from the plate around it (the game writes
the round there). Outside the bar (the ends' tips, the gold frame's ornaments and glow, the divider's spikes) the
concept is painted over black, so there the brightness becomes the coverage (glows and haze fade out, the
bright metal stays).

Every piece keeps the concept's scale: TEXELS_PER_PX texels per concept pixel (a bronze slot 18 texels wide), and
records where the bar's middle row is (BAR_MID) and, for a slot, its portrait window, so the game can line them up.
"""
from pathlib import Path

import numpy as np

PROJECT = Path(__file__).resolve().parents[2]
CONCEPT = PROJECT / "art/turn-order/concept.png"
TEXELS_PER_PX = 18 / 220
BAR = (228, 479)        # the bar's rows in the concept
BAR_MID = (BAR[0] + BAR[1]) / 2

# name: (box x0, y0, x1, y1 in concept pixels, kind, extra)
PIECES = {
    "to_end_l": ((0, 222, 214, 484), "end_l", {"text": [(96, 270, 218, 309), (122, 314, 178, 424)], "tip": 76}),
    "to_plate": ((986, 222, 1158, 484), "plate", {"text": [(1000, 272, 1120, 312), (1020, 312, 1095, 400)]}),
    "to_slot": ((470, 222, 690, 484), "slot", {"window": (490, 263, 669, 444)}),
    "to_gold": ((214, 168, 471, 500), "slot", {"window": (247, 245, 443, 446), "rows": (452, 464, 464)}),
    "to_silver": ((1556, 222, 1772, 484), "slot", {"window": (1575, 263, 1749, 442)}),
    "to_divider": ((914, 168, 1000, 500), "divider", {}),
    "to_end_r": ((1958, 222, 2078, 484), "end_r", {"tip": 1984}),
}


def _load():
    from pngio import read_png
    w, h, rgba = read_png(CONCEPT)
    return np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4)[..., :3].astype(np.float32) / 255.0


def _dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= r * r:
                out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def _box_blur(img, r):
    out = img
    for axis in (0, 1):
        for _ in range(3):
            c = np.cumsum(np.pad(out, [(r + 1, r) if k == axis else (0, 0) for k in range(out.ndim)], mode="edge"),
                          axis=axis)
            out = (np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis)
                   - np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)) / (2 * r + 1)
    return out


def _fill_text(rgb, boxes, x0, y0):
    """The plate's words taken out: the patch is the leather around it blended in smoothly (a blur that leaves the
    words out), with the leather's fine grain borrowed from clean parts of the plate on top."""
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    mask = np.zeros(lum.shape, bool)
    for bx0, by0, bx1, by1 in boxes:
        sub = (slice(by0 - y0, by1 - y0), slice(bx0 - x0, bx1 - x0))
        mask[sub] |= lum[sub] > 0.16
    mask = _dilate(mask, 7)
    keep = (~mask).astype(np.float32)[..., None]
    smooth = _box_blur(rgb * keep, 14) / np.maximum(_box_blur(keep, 14), 1e-3)
    grain = rgb - _box_blur(rgb, 3)
    # Grain only from plain leather: dark, away from the words and from the plate's bright scrollwork.
    leather = ~_dilate(mask, 3) & ~_dilate(lum > 0.12, 4)
    detail = np.zeros_like(rgb)
    todo = mask.copy()
    for k in range(1, 40):
        for dy, dx in ((37 * k, 23 * k), (-37 * k, 23 * k), (37 * k, -23 * k), (-37 * k, -23 * k)):
            dy %= rgb.shape[0]
            dx %= rgb.shape[1]
            src = np.roll(np.roll(grain, -dy, 0), -dx, 1)
            ok = np.roll(np.roll(leather, -dy, 0), -dx, 1)
            take = todo & ok
            detail[take] = src[take]
            todo &= ~take
    out = rgb.copy()
    out[mask] = np.clip(smooth[mask] + detail[mask], 0, 1)
    edge = _dilate(mask, 2) & ~mask  # soften the patch's rim
    out[edge] = (out[edge] + smooth[edge]) / 2
    return out


def cut(name):
    """(w texels, h texels, premultiplied RGBA float at full concept resolution, info) of a piece."""
    (x0, y0, x1, y1), kind, extra = PIECES[name]
    rgb = _load()[y0:y1, x0:x1].copy()
    H, W = rgb.shape[:2]
    info = {"bar_mid": (BAR_MID - y0) * TEXELS_PER_PX}
    if "text" in extra:
        rgb = _fill_text(rgb, extra["text"], x0, y0)
    if "rows" in extra:  # the gold frame's blue team bar, under its window: its blue painted over with the band below
        d0, d1, src = (v - y0 for v in extra["rows"])
        for k in range(d1 - d0):
            row = rgb[d0 + k]
            blue = (row[:, 2] > row[:, 0] + 0.08) | (row[:, 2] > 0.55)
            row[blue] = rgb[src + k * 10 // (d1 - d0)][blue]
    # Coverage: inside the bar solid; outside it (and the ends' tips) the brightness over black.
    soft = 0.14 if kind in ("end_l", "end_r") else 0.45  # the ends' tips are metal; the gold frame's haze fades
    over_black = np.clip((rgb.max(axis=2) - 0.02) / soft, 0, 1)
    rows = np.arange(H)[:, None] + y0
    cols = np.arange(W)[None, :] + x0
    solid = np.broadcast_to((rows >= BAR[0]) & (rows < BAR[1]), (H, W)).copy()
    if kind == "end_l":
        solid &= cols >= extra["tip"]
    if kind == "end_r":
        solid &= cols < extra["tip"]
    alpha = np.where(solid, 1.0, over_black)
    colour = np.where(alpha[..., None] > 1e-3, rgb / np.maximum(alpha[..., None], 1e-3), 0)
    colour = np.clip(np.where(solid[..., None], rgb, colour), 0, 1)
    if kind == "slot":  # the frame's window (measured: its inner edges in the concept), cleared
        wx0, wy0, wx1, wy1 = (v - o for v, o in zip(extra["window"], (x0, y0, x0, y0)))
        alpha[wy0:wy1, wx0:wx1] = 0.0
        info["window"] = tuple(v * TEXELS_PER_PX for v in (wx0, wy0, wx1, wy1))
    img = np.concatenate([colour * alpha[..., None], alpha[..., None]], -1)
    w, h = max(1, round(W * TEXELS_PER_PX)), max(1, round(H * TEXELS_PER_PX))
    return w, h, img, info


def draw(name, SS):
    """make_branding's draw function: the piece at SS pixels a texel."""
    def d(w, h):
        import subprocess, tempfile
        from pngio import read_png, write_png
        _, _, img, _ = cut(name)
        a = img[..., 3:]
        straight = np.where(a > 1e-4, img[..., :3] / np.maximum(a, 1e-4), 0)
        rgba8 = (np.clip(np.concatenate([straight, a], -1), 0, 1) * 255 + 0.5).astype(np.uint8)
        with tempfile.TemporaryDirectory() as t:
            src, dst = Path(t) / "a.png", Path(t) / "b.png"
            write_png(src, img.shape[1], img.shape[0], rgba8.tobytes())
            subprocess.run(["magick", str(src), "-filter", "Lanczos", "-resize", f"{w * SS}x{h * SS}!", f"PNG32:{dst}"],
                           check=True)
            _, _, out = read_png(dst)
        o = np.frombuffer(bytes(out), np.uint8).reshape(h * SS, w * SS, 4).astype(np.float32) / 255.0
        return np.concatenate([o[..., :3] * o[..., 3:], o[..., 3:]], -1)
    return d


def images(SS):
    """[(name, w, h, draw)] for make_branding's IMAGES, and the header lines for the pieces' bar middles and windows."""
    out, lines = [], []
    for name in PIECES:
        w, h, img, info = cut(name)
        out.append((name, w, h, draw(name, SS)))
        # The picture is made exactly w x h texels: its rows and columns scaled a little from the concept's.
        fx, fy = w / (img.shape[1] * TEXELS_PER_PX), h / (img.shape[0] * TEXELS_PER_PX)
        info["bar_mid"] *= fy
        win = info.get("window", (0, 0, 0, 0))
        win = (win[0] * fx, win[1] * fy, win[2] * fx, win[3] * fy)
        lines.append(f'        {{ "{name}", {info["bar_mid"]:.3f}f, {win[0]:.3f}f, {win[1]:.3f}f, {win[2]:.3f}f, {win[3]:.3f}f }},')
    return out, lines
