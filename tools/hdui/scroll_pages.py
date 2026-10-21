#!/usr/bin/env python3
"""scroll_pages.py: the pause menu's parchment (PauseBGOptions | PauseBGStats | PauseBGMap) repainted, from a painting.

The three pages are one long strip that slides between the scroll's rollers as the pages turn (src/game/patches.cpp,
"Pause scroll"). art/pages/source/aidyn-scroll-concept.png is a painting of the whole strip (Codex, 2026-10-20, from
a mock-up of the game's layout). This script:

  1. enlarges it 4x with Real-ESRGAN (realesrgan-x4plus, the toolkit's) and places it on the strip at 8 pixels a
     texel (7680x1920) by the scale and shift measured by matching its page contents to the game's (KX, KY: within a
     texel everywhere but the equipment frames);
  2. takes the paper's own torn edges from it (the painting's dark background is transparent), its bottom edge
     stretched down a few texels to where the game's was (the equipment page's prompts sit there);
  3. erases what the game's pictures say must be exact, from the painting: the equipment page's frames (the game draws
     the portrait and the items in them), its words (HitPoints, Level, Gold, DETAILS) and the C-down button, the map
     page's calendar (numbers and rings) and its page arrow; filled in from the parchment around them;
  4. draws them again where the game's own pictures have them: the frames and the calendar's rings in the painting's
     cream, the words, numbers,
     the Xbox X button and the arrow as the HD menu pack draws them (labels.json, repaint.py, buttons.py, hudart.py);
  5. writes art/pages/PauseBGOptions.png, PauseBGStats.png, PauseBGMap.png (2560x1920 each). art_pack.py puts them in
     the redrawn art pack as big pictures (bigstrips.py).

  python3 -I tools/hdui/scroll_pages.py [--rom baserom.z64]
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buttons  # noqa: E402
import hudart  # noqa: E402
import repaint  # noqa: E402
import romassets  # noqa: E402
from build_pack import PROJECT, toolkit  # noqa: E402

S = 8                                  # pixels per texel
PAGES = ["PauseBGOptions", "PauseBGStats", "PauseBGMap"]
SOURCE = PROJECT / "art/pages/source/aidyn-scroll-concept.png"
OUT = PROJECT / "art/pages"
FONT = Path(__file__).resolve().parent / "fonts/MarcellusSC-Regular.ttf"
# The painting (as delivered, 2185x720) on the strip at 4 px a texel: x' = KX[0] x + KX[1], y' = KY[0] y + KY[1].
KX, KY = (1.7613, -7.5), (1.7008, -154.8)
INK = (0.42, 0.22, 0.17)               # the painting's dark red-brown ink (its ruled lines)
BACKDROP = (29, 40, 30)                # the painting's dark green around the paper
BOTTOM = (205, 222, 226)               # below texel 205, texel 222 of the painting goes to 226 (see placed())
LABELS = json.loads((Path(__file__).resolve().parent / "labels.json").read_text())
STATS_LABELS, MAP_LABELS = LABELS["99a10b42821b0510"], LABELS["8bd29ae433464ad6"]
# Texel boxes on a page (x0, y0, x1, y1) where the painting's own frames, words, numbers and marks are erased: a little
# larger than the game's, the painting's may sit a few texels off.
STATS_FRAMES = (14, 2, 152, 238)      # where the game's frames are looked for
STATS_FRAME_ERASE = [(14, 18, 152, 90), (14, 90, 141, 214)]  # the painting's frames (not its braid, x 142..156)
STATS_WORDS = [(150, 26, 222, 47), (176, 47, 222, 66), (176, 62, 222, 81), (226, 194, 300, 216)]
MAP_NUMBERS = (156, 44, 306, 140)
# The painted cords are left out: the HP rope (the game draws its own, HealthBar, over it) and the braid down the
# middle of the equipment page.
STATS_CORDS = [(218, 22, 304, 36), (134, 92, 163, 214)]
# The two lines around the game's INVENTORY title: erased from the painting (it has them 2 texels high) and drawn at
# the game's rows.
STATS_LINES_ERASE = (156, 80, 320, 101)
STATS_LINES = [(86, 161, 318), (97, 171, 308)]     # (row, x0, x1) in texels
# The page's bottom row (the hints along it hung off the torn edge): DETAILS and its button 4 texels higher than the
# game's own (the port's Unequip and Lists hints move with them, src/game/patches.cpp "Inventory page hints").
DETAILS_UP = 4
MAP_ARROW = (136, 190, 184, 210)


def original(rom, index):
    fmt, w, h, pix, pal = rom.image(index)
    from pngio import decode_borg8
    return {"w": w, "h": h, "rgba": bytes(decode_borg8(fmt, w, h, pix, pal))}


def placed(path):
    """The painting enlarged and placed on the strip: (rgb float 0..1, alpha 0..1), 7680x1920."""
    esrgan = toolkit() / "opt/realesrgan/realesrgan-ncnn-vulkan"
    with tempfile.TemporaryDirectory() as t:
        big = Path(t) / "x4.png"
        subprocess.run([str(esrgan), "-i", str(path), "-o", str(big), "-n", "realesrgan-x4plus", "-s", "4", "-f", "png"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        src = Image.open(big).convert("RGB")
    # KX/KY place the painting as delivered at 4 px a texel; here it is 4x larger and the strip is at 8
    a, b = KX[0] * 2 / 4, KX[1] * 2
    c, d = KY[0] * 2 / 4, KY[1] * 2
    W, H = 960 * S, 240 * S
    # inverse map for PIL's affine: source = (x - b) / a
    img = src.transform((W, H), Image.AFFINE, (1 / a, 0, -b / a, 0, 1 / c, -d / c), resample=Image.BICUBIC,
                        fillcolor=BACKDROP)
    rgb = np.asarray(img).astype(np.float32) / 255
    # the painting's bottom edge sits 3 to 4 texels higher than the game's, and the game's prompts along the bottom of
    # the equipment page (Unequip, Lists, Details) would hang off it: below texel BOTTOM[0] it is stretched so that its
    # texel BOTTOM[1] lands on BOTTOM[2]
    y0, src, dst = (v * S for v in BOTTOM)
    rows = np.arange(H, dtype=np.float32)
    srow = np.where(rows < y0, rows, y0 + (rows - y0) * (src - y0) / (dst - y0))
    lo = np.clip(np.floor(srow).astype(int), 0, H - 1)
    hi = np.clip(lo + 1, 0, H - 1)
    t = (srow - lo)[:, None, None]
    rgb = rgb[lo] * (1 - t) + rgb[hi] * t
    # transparent: the painting's dark green backdrop only (by colour, not brightness: the ink is dark too)
    dist = np.sqrt(((rgb - np.array(BACKDROP, np.float32) / 255) ** 2).sum(-1))
    alpha = np.clip((dist - 0.05) / 0.07, 0, 1)
    alpha = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))) / 255.0
    return rgb, alpha.astype(np.float32)


def blur_f(a, sigma):
    """A float array blurred by about a Gaussian of sigma: three box passes along each axis (edges held)."""
    r = max(1, int(round(sigma * 0.9)))
    out = a.astype(np.float32)
    for axis in (0, 1):
        for _ in range(3):
            p = np.pad(out, [(r + 1, r) if i == axis else (0, 0) for i in range(out.ndim)], mode="edge")
            c = np.cumsum(p, axis=axis, dtype=np.float64)
            hi = np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis)
            lo = np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)
            out = ((hi - lo) / (2 * r + 1)).astype(np.float32)
    return out


def pull_push(rgb, keep):
    """Fills the pixels not kept from those around them, from the hole's own edges inwards: the kept pixels averaged
    down a pyramid (halving) until nothing is missing, then each level's holes taken from the coarser level on the way
    back up, smoothed."""
    levels = [(rgb * keep[..., None], keep.astype(np.float32))]
    while levels[-1][1].min() <= 0 and min(levels[-1][1].shape) > 2:
        c, w = levels[-1]
        h2, w2 = (c.shape[0] + 1) // 2, (c.shape[1] + 1) // 2
        c = np.pad(c, ((0, h2 * 2 - c.shape[0]), (0, w2 * 2 - c.shape[1]), (0, 0)))
        w = np.pad(w, ((0, h2 * 2 - w.shape[0]), (0, w2 * 2 - w.shape[1])))
        levels.append((c.reshape(h2, 2, w2, 2, 3).sum(axis=(1, 3)), w.reshape(h2, 2, w2, 2).sum(axis=(1, 3))))
    c, w = levels[-1]
    col = c / np.maximum(w, 1e-6)[..., None]
    for c, w in reversed(levels[:-1]):
        up = np.repeat(np.repeat(col, 2, 0), 2, 1)[:c.shape[0], :c.shape[1]]
        up = blur_f(up, 1.0)
        a = np.clip(w, 0, 1)[..., None]
        col = (c / np.maximum(w, 1e-6)[..., None]) * a + up * (1 - a)
    return col


def local_paper(rgb, keep, sigma):
    """Normalized convolution: the parchment colour around each pixel, from the pixels kept."""
    def blur(a):
        return blur_f(a, sigma)
    k = keep.astype(np.float32)
    num = np.stack([blur(np.ascontiguousarray(rgb[..., i] * k)) for i in range(3)], -1)
    den = blur(k)[..., None]
    return num / np.maximum(den, 1e-4)


def erase(rgb, box, page, kind, alpha=None, size=None):
    """Fills the painting's marks inside a texel box of a page from the parchment around them ("box": the whole box,
    within the paper)."""
    x0, y0, x1, y1 = box
    ox = page * 320 * S
    sl = (slice(y0 * S, y1 * S), slice(ox + x0 * S, ox + x1 * S))
    pad = 6 * S
    big = (slice(max(0, y0 * S - pad), y1 * S + pad), slice(ox + x0 * S - pad, ox + x1 * S + pad))
    region = rgb[big].copy()
    # the paper under the marks: a median at a quarter size, over about 20 texels for ink (wider than any word's
    # letters, so they don't darken it), 10 for the thin frame lines
    im = Image.fromarray((region * 255).astype(np.uint8))
    size = size or (21 if kind == "light" else 41)
    paper = np.asarray(im.resize((im.width // 4, im.height // 4), Image.BOX).filter(ImageFilter.MedianFilter(size))
                       .resize(im.size, Image.BICUBIC)) / 255.0
    diff = region - paper
    lum = diff @ np.array([0.299, 0.587, 0.114], np.float32)
    if kind == "light":    # cream frame lines, lighter than the paper
        mark, grow = lum > 0.02, 15
    elif kind == "dark":   # ink: words, numbers, the arrow
        mark, grow = lum < -0.03, 13
    else:                  # the whole box (a word with its button)
        mark, grow = np.ones(lum.shape, bool), 1
    inner = np.zeros(mark.shape, bool)
    inner[sl[0].start - big[0].start:sl[0].stop - big[0].start, sl[1].start - big[1].start:sl[1].stop - big[1].start] = True
    mark &= inner
    paper_px = np.ones(mark.shape, bool) if alpha is None else alpha[big] > 0.6
    if alpha is not None:  # the parchment to fill from: not its darker burnt rim (5 texels in from the torn edge)
        solid = Image.fromarray(((alpha[big] > 0.98) * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(9))
        sample_px = (np.asarray(solid.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MinFilter(9))
                                .filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MinFilter(9))) > 0)
    else:
        sample_px = paper_px
    mark &= paper_px
    mark = np.asarray(Image.fromarray(mark.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(grow))) > 0
    # (a whole box needs a reach of about its half height, or its middle has no paper to draw from)
    reach = 3 * S if kind != "box" else max(3, (y1 - y0) // 2 + 2) * S
    fill = pull_push(region, ~mark & sample_px) if kind == "box" else local_paper(region, ~mark & sample_px, reach)
    if kind == "box":  # only its large shading: the grain comes from the blank page below
        fill = blur_f(fill, 4 * S)
    # keep the paper's grain: the fill plus the fine detail of the blank Menu page, from its middle (texels 80..300,
    # clear of the painting's rolled end)
    # (one piece, not wrapped: a box is narrower than those 220 texels); a whole box takes its mottling too, not only
    # the fine grain, and blends in over 3 texels
    # (and from its middle rows, 50..170: the same rows near the top or bottom run into its burnt rim)
    cols = np.arange(region.shape[1]) + 100 * S
    rows = ((np.arange(region.shape[0]) + big[0].start) % (120 * S)) + 50 * S
    blank = rgb[rows][:, cols]
    grain = blank - blur_f(blank, (8 if kind == "box" else 2) * S)
    edge = 3 * S if kind == "box" else 2
    soft = np.asarray(Image.fromarray(mark.astype(np.uint8) * 255).filter(ImageFilter.GaussianBlur(edge))) / 255.0
    if kind == "box":
        soft = soft * mark  # feathered inwards: nothing of the painting's marks survives at the box's edge
    region = region * (1 - soft[..., None]) + np.clip(fill + grain, 0, 1) * soft[..., None]
    rgb[big] = region


def frame_boxes(stats):
    """The game's frames on PauseBGStats as texel boxes (x0, y0, x1, y1), each line's texels included: the texels a
    little brighter than the parchment around them, kept where they make up a rectangle (not the speckle)."""
    px = np.frombuffer(stats["rgba"], np.uint8).reshape(240, 320, 4)
    lum = px[..., :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    med = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(ImageFilter.MedianFilter(7))).astype(np.float32)
    mask = (lum - med > 12) & (px[..., 3] > 0)
    x0, y0, x1, y1 = STATS_FRAMES
    keep = np.zeros_like(mask)
    keep[y0:y1, x0:x1] = True
    mask &= keep
    seen, boxes = np.zeros_like(mask), []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        stack, comp = [(y, x)], []
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            comp.append((cy, cx))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < 240 and 0 <= nx < 320 and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        ys, xs = [c[0] for c in comp], [c[1] for c in comp]
        bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if len(comp) >= 30 and bw >= 8 and bh >= 8 and len(comp) >= 1.5 * (bw + bh):  # an outline, not a blob
            boxes.append(snap(mask, min(xs), min(ys), max(xs), max(ys)))
    return boxes


def snap(mask, x0, y0, x1, y1):
    """Each edge of a detected frame moved to the column or row (within 2 texels inwards) where its line is: bright
    speckle touching a frame (the boot's, beside the silhouette) made it a texel or two too wide."""
    def best(cands, count):
        return max(cands, key=lambda c: (count(c), -abs(c - cands[0])))
    col = lambda x: int(mask[y0:y1 + 1, x].sum())
    row = lambda y: int(mask[y, x0:x1 + 1].sum())
    x0n, x1n = best([x0, x0 + 1, x0 + 2], col), best([x1, x1 - 1, x1 - 2], col)
    y0n, y1n = best([y0, y0 + 1, y0 + 2], row), best([y1, y1 - 1, y1 - 2], row)
    return (x0n, y0n, x1n, y1n)


def draw_frames(rgba8, stats):
    """The game's frames, as clean straight outlines on their own texels, in the painting's cream, softly edged."""
    w, h = stats["w"], stats["h"]
    m = np.zeros((h * S, w * S), np.uint8)
    for x0, y0, x1, y1 in frame_boxes(stats):
        X0, Y0, X1, Y1 = x0 * S + 1, y0 * S + 1, (x1 + 1) * S - 1, (y1 + 1) * S - 1  # each line 6 of its texel's 8 pixels
        m[Y0:Y0 + S - 2, X0:X1] = 255
        m[Y1 - (S - 2):Y1, X0:X1] = 255
        m[Y0:Y1, X0:X0 + S - 2] = 255
        m[Y0:Y1, X1 - (S - 2):X1] = 255
    m = np.asarray(Image.fromarray(m).filter(ImageFilter.GaussianBlur(1.0))) / 255.0
    shade = np.asarray(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(3))) / 255.0
    ox = 320 * S
    page = rgba8[:, ox:ox + w * S]
    page[..., :3] = page[..., :3] * (1 - 0.18 * shade[..., None])  # a faint shadow around the line
    cream = np.array([0.97, 0.93, 0.82], np.float32)
    page[..., :3] = page[..., :3] * (1 - 0.92 * m[..., None]) + cream * 0.92 * m[..., None]


def calendar_rings(mp):
    """The calendar's rings (the light circles round some days on PauseBGMap) as (centre x, centre y, radius) texels:
    the texels a little brighter than the parchment around them in the calendar, grouped, each group's centre and mean
    distance from it."""
    px = np.frombuffer(mp["rgba"], np.uint8).reshape(240, 320, 4)
    lum = px[..., :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    med = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(ImageFilter.MedianFilter(7))).astype(np.float32)
    mask = (lum - med > 10) & (px[..., 3] > 0)
    x0, y0, x1, y1 = MAP_LABELS["numbers"]
    keep = np.zeros_like(mask)
    keep[y0:y1, x0:x1] = True
    mask &= keep
    seen, rings = np.zeros_like(mask), []
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        stack, comp = [(y, x)], []
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            comp.append((cy, cx))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        ys, xs = np.array([c[0] for c in comp], np.float32), np.array([c[1] for c in comp], np.float32)
        if len(comp) >= 12 and xs.max() - xs.min() >= 6 and ys.max() - ys.min() >= 6:
            cx, cy = (xs.min() + xs.max()) / 2 + 0.5, (ys.min() + ys.max()) / 2 + 0.5
            rings.append((cx, cy, float(np.hypot(xs + 0.5 - cx, ys + 0.5 - cy).mean())))
    # all the same size (one moon, half lit and half outline, measured a texel small): their median radius
    r = float(np.median([g[2] for g in rings])) if rings else 0.0
    return [(cx, cy, r) for cx, cy, _ in rings]


def moon_lit(mp, cx, cy, r):
    """The lit part of one calendar moon on the original page (texels, float 0..1, over its disc): the texels clearly
    lighter than the parchment around the moon, and the number's ink where it lies inside the lit part (a closing)."""
    px = np.frombuffer(mp["rgba"], np.uint8).reshape(240, 320, 4)
    lum = px[..., :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    yy, xx = np.mgrid[0:240, 0:320].astype(np.float32) + 0.5
    d = np.hypot(xx - cx, yy - cy)
    paper = float(np.median(lum[(d > r + 1.5) & (d < r + 4)]))
    lit = (lum > paper + 18) & (d < r + 0.8)
    m = Image.fromarray(lit.astype(np.uint8) * 255)
    lit = np.asarray(m.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))) > 0
    return lit & (d < r + 0.8)


def moon_phase(mp, cx, cy, r):
    """(k, ux, uy) of one moon: the lit side's direction (ux, uy) and the terminator, lit = u > k sqrt(r^2 - v^2) in
    coordinates along and across that direction (k = 1 new, 0 half, -1 full), from the original's lit texels: the
    lit share of the disc's inside (its outline left out) gives k = 1 - 2 x share, their centroid the direction."""
    lit = moon_lit(mp, cx, cy, r)
    px = np.frombuffer(mp["rgba"], np.uint8).reshape(240, 320, 4)
    lum = px[..., :3].astype(np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)
    yy, xx = np.mgrid[0:240, 0:320].astype(np.float32) + 0.5
    d = np.hypot(xx - cx, yy - cy)
    paper = float(np.median(lum[(d > r + 1.5) & (d < r + 4)]))
    ink = lum < paper - 25  # the number's texels: neither lit nor unlit, left out of the count
    inside = (d < r - 1.2) & ~ink
    share = float(lit[inside].mean()) if inside.any() else 0.0
    sel = lit & inside
    if share < 0.08 or share > 0.92 or not sel.any():
        return (1.0 if share < 0.5 else -1.0), 1.0, 0.0
    dx, dy = float((xx[sel] - cx).mean()), float((yy[sel] - cy).mean())
    n = max(np.hypot(dx, dy), 1e-6)
    return 1.0 - 2.0 * share, dx / n, dy / n


def draw_rings(page, rings, mp):
    """Each moon (the calendar marks days by the moon's phase: full, gibbous, half, crescent, new) drawn smooth: its
    lit part (moon_phase, measured on the original) filled in the painting's cream with a soft glow, and its outline a
    soft cream circle a texel wide (the new moon is the outline alone)."""
    H, W = page.shape[:2]
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32) / S
    cream = np.array([1.0, 0.98, 0.92], np.float32)
    for cx, cy, r in rings:
        k, ux, uy = moon_phase(mp, cx, cy, r)
        x0, x1 = int((cx - r - 4) * S), int((cx + r + 4) * S)
        y0, y1 = int((cy - r - 4) * S), int((cy + r + 4) * S)
        X, Y = xx[y0:y1, x0:x1] - cx, yy[y0:y1, x0:x1] - cy
        d = np.hypot(X, Y)
        u, v = X * ux + Y * uy, -X * uy + Y * ux
        g = u - k * np.sqrt(np.maximum(r * r - v * v, 0.0))
        fill = np.clip(g / 0.5 + 0.5, 0, 1) * np.clip((r + 0.2 - d) / 0.5, 0, 1)
        line = np.clip(1 - np.abs(d - r) / 0.6, 0, 1)
        glow = np.exp(-((d - r) / 1.6) ** 2) * 0.35
        a = np.clip(np.maximum.reduce([fill * 0.92, line * 0.85, glow]), 0, 1)[..., None]
        region = page[y0:y1, x0:x1]
        region[..., :3] = region[..., :3] * (1 - a) + cream * a


def composite(dst, src, x, y):
    """Straight-alpha src (h, w, 4 floats 0..1) over dst at pixel (x, y)."""
    h, w = src.shape[:2]
    region = dst[y:y + h, x:x + w]
    a = src[..., 3:]
    region[..., :3] = region[..., :3] * (1 - a) + src[..., :3] * a


def as_float(W, H, rgba):
    return np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4).astype(np.float32) / 255


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    ap.add_argument("--source", type=Path, default=SOURCE)
    args = ap.parse_args()
    rom = romassets.Rom(args.rom)
    names = {v: k for k, v in romassets.borg8_names(PROJECT / "decomp").items()}
    orig = {p: original(rom, names[p]) for p in PAGES}

    rgb, alpha = placed(args.source)
    # 3. erase the painting's exact parts
    for box in STATS_FRAME_ERASE:
        erase(rgb, box, 1, "light", alpha)
    for box in STATS_WORDS[:3] + STATS_CORDS:
        erase(rgb, box, 1, "box", alpha)
    x0, y0, x1, y1 = STATS_WORDS[3]
    erase(rgb, (x0, y0 - DETAILS_UP, x1, y1), 1, "box", alpha)
    erase(rgb, STATS_LINES_ERASE, 1, "dark")
    erase(rgb, MAP_NUMBERS, 2, "light", size=41)    # the glowing rings (wider than the frame lines), first:
    erase(rgb, MAP_NUMBERS, 2, "dark")              # the numbers inside them show against the paper only then
    erase(rgb, MAP_ARROW, 2, "dark")
    rgba = np.concatenate([rgb, alpha[..., None]], -1)
    # 4. draw them again where the game has them
    draw_frames(rgba, orig["PauseBGStats"])
    W, H = 320 * S, 240 * S
    pages = [np.ascontiguousarray(rgba[:, k * W:(k + 1) * W]) for k in range(3)]
    for (row, lx0, lx1) in STATS_LINES:
        line = np.zeros((H, W), np.float32)
        line[row * S + 2:row * S + 6, lx0 * S:(lx1 + 1) * S] = 1
        ends = np.clip(np.minimum(np.arange(W) - lx0 * S, (lx1 + 1) * S - np.arange(W)) / (3 * S), 0, 1)
        line = blur_f(line * ends[None, :], 0.8)[..., None]
        pages[1][..., :3] = pages[1][..., :3] * (1 - 0.9 * line) + np.array(INK, np.float32) * 0.9 * line
    stats = bytearray((np.clip(pages[1], 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
    for x0, y0, bw, bh, text in STATS_LABELS["words"]:
        colour = repaint.word_colour(orig["PauseBGStats"], (x0, y0, bw, bh))
        if text == "DETAILS":
            y0 -= DETAILS_UP
        stats = repaint.draw_word(W, H, stats, (x0, y0, bw, bh), text, colour, FONT, S)
    pages[1] = as_float(W, H, stats)
    for x0, y0, bw, bh, button in STATS_LABELS["buttons"]:
        y0 -= DETAILS_UP
        icon = as_float(bw * S, bh * S, buttons.icon_for(button, bw, bh, S))
        composite(pages[1], icon, x0 * S, y0 * S)
    draw_rings(pages[2], calendar_rings(orig["PauseBGMap"]), orig["PauseBGMap"])
    _, numbers = repaint.clean(orig["PauseBGMap"], MAP_LABELS["numbers"])
    mp = bytearray((np.clip(pages[2], 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
    mp = repaint.draw(W, H, mp, numbers, FONT, S)
    pages[2] = as_float(W, H, mp)
    for x0, y0, bw, bh, kind in MAP_LABELS["arrows"]:
        o = np.frombuffer(orig["PauseBGMap"]["rgba"], np.uint8).reshape(240, 320, 4)[y0:y0 + bh, x0:x0 + bw, :3]
        ink = o.reshape(-1, 3)[np.argmin(o.reshape(-1, 3).astype(int).sum(-1))] / 255.0
        aw, ah, arrow = hudart.redraw("SliderArrow" + kind, bw, bh)
        a = np.frombuffer(bytes(arrow), np.uint8).reshape(ah, aw, 4)[..., 3].astype(np.float32) / 255
        f = hudart.SCALE // S
        a = a.reshape(bh * S, f, bw * S, f).mean(axis=(1, 3))
        region = pages[2][y0 * S:(y0 + bh) * S, x0 * S:(x0 + bw) * S]
        region[..., :3] = region[..., :3] * (1 - a[..., None]) + ink * a[..., None]
    OUT.mkdir(parents=True, exist_ok=True)
    for name, page in zip(PAGES, pages):
        Image.fromarray((np.clip(page, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(OUT / f"{name}.png", optimize=True)
        print(f"scroll_pages.py: {OUT / name}.png")


if __name__ == "__main__":
    main()
