"""repaint.py: numbers painted into a background picture (the journal's calendar), redrawn with the text font.

clean(): finds the dark number pixels inside a region, groups them into numbers (reading order gives 1, 2, 3...),
and fills them in from the surrounding parchment, so the picture can be upscaled without them.
draw(): after the upscale, draws each number with the text font, centred where the original was, same height and
colour. Rendering is done by ImageMagick at 16 pixels per texel and box-filtered down.
erase() / word_colour() / draw_word(): a word painted into a picture (the inventory page's HitPoints, Level, Gold,
Details) filled in from the parchment before the upscale and written again with the text font after it.
"""
import subprocess
import tempfile
from pathlib import Path

from pngio import read_png

SS = 16


def _lum(rgba, o):
    return (rgba[o] * 3 + rgba[o + 1] * 6 + rgba[o + 2]) / 10


def clean(img, region, dark=140, fringe=185):
    """Returns (cleaned rgba, numbers): numbers = [(value, (x0, y0, x1, y1), (r, g, b))] in texels."""
    w, rgba = img["w"], bytearray(img["rgba"])
    rx0, ry0, rx1, ry1 = region
    core = {(x, y) for y in range(ry0, ry1) for x in range(rx0, rx1) if _lum(rgba, (y * w + x) * 4) < dark}
    # Group the core pixels into digits, then digits into numbers (side by side, at most 2 texels apart).
    seen, digits = set(), []
    for p in sorted(core, key=lambda p: (p[1], p[0])):
        if p in seen:
            continue
        stack, comp = [p], []
        seen.add(p)
        while stack:
            cx, cy = stack.pop()
            comp.append((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (cx + dx, cy + dy)
                    if q in core and q not in seen:
                        seen.add(q)
                        stack.append(q)
        xs, ys = [c[0] for c in comp], [c[1] for c in comp]
        if len(comp) >= 4:
            digits.append([min(xs), min(ys), max(xs), max(ys), comp])
    digits.sort(key=lambda d: (d[1], d[0]))
    numbers = []
    for d in digits:
        for n in numbers:
            if abs(n[1] - d[1]) <= 2 and 0 <= d[0] - n[2] <= 2:
                n[0], n[1], n[2], n[3] = min(n[0], d[0]), min(n[1], d[1]), max(n[2], d[2]), max(n[3], d[3])
                n[4] += d[4]
                break
        else:
            numbers.append(d)
    # Reading order: rows (by top, within 3 texels), then left to right.
    numbers.sort(key=lambda n: (round(n[1] / 4), n[0]))
    rows = []
    for n in numbers:
        if rows and abs(rows[-1][0][1] - n[1]) <= 3:
            rows[-1].append(n)
        else:
            rows.append([n])
    ordered = [n for row in rows for n in sorted(row, key=lambda n: n[0])]
    # Pixels to repaint: every number's box grown by a texel, where the pixel is darker than the parchment.
    mask = set()
    for n in ordered:
        for y in range(n[1] - 1, n[3] + 2):
            for x in range(n[0] - 1, n[2] + 2):
                if _lum(rgba, (y * w + x) * 4) < fringe:
                    mask.add((x, y))
    result = []
    for value, n in enumerate(ordered, 1):
        pts = n[4]
        colour = tuple(sum(rgba[(y * w + x) * 4 + c] for x, y in pts) // len(pts) for c in range(3))
        result.append((value, (n[0], n[1], n[2], n[3]), colour))
    # Fill from the edges in: each pass sets the masked pixels that touch known ones to their average.
    todo = set(mask)
    while todo:
        done = {}
        for x, y in todo:
            acc, k = [0, 0, 0], 0
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (x + dx, y + dy)
                    if (dx or dy) and q not in todo and 0 <= q[0] < w and 0 <= q[1] < img["h"]:
                        o = (q[1] * w + q[0]) * 4
                        acc = [acc[c] + rgba[o + c] for c in range(3)]
                        k += 1
            if k >= 2:
                done[(x, y)] = [v // k for v in acc]
        if not done:
            break
        for (x, y), c in done.items():
            rgba[(y * w + x) * 4:(y * w + x) * 4 + 3] = bytes(c)
        todo -= set(done)
    return rgba, result


def _digit(font, ch, colour, height, tmp):
    """One digit, trimmed to its ink and scaled to `height` pixels: Marcellus has old-style figures (3, 4, 5, 7, 9
    hang below the line, 0 1 2 are short), which would bob up and down in a calendar grid; each digit gets the same
    height. "-trim" alone would keep the ImageMagick label's padding, so the ink box is found and cropped first."""
    png = tmp / f"d{ch}.png"
    if not png.exists():
        subprocess.run(["magick", "-size", "600x700", "xc:none", "-fill", colour, "-stroke", colour,
                        "-strokewidth", "10", "-font", str(font), "-pointsize", "400", "-annotate", "+100+450", ch,
                        "-trim", "+repage", "-filter", "box", "-resize", f"x{height}", f"PNG32:{png}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return read_png(png)


def draw(W, H, rgba, numbers, font, scale):
    """Draws the numbers onto an upscaled picture (scale pixels per texel). Returns new rgba."""
    out = bytearray(rgba)
    # One height, one colour and one baseline per row for all numbers: the originals vary by a texel and a shade.
    def median(values):
        values = sorted(values)
        return values[len(values) // 2]
    height = median(y1 - y0 + 1 for _, (x0, y0, x1, y1), _ in numbers) * scale
    colour = tuple(median(c[i] for _, _, c in numbers) for i in range(3))
    baseline = {}
    for _, (x0, y0, x1, y1), _ in numbers:
        row = next((r for r in baseline if abs(r - y0) <= 3), y0)
        baseline.setdefault(row, []).append(y1)
    baseline = {r: median(v) for r, v in baseline.items()}
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        hexcol = "#%02x%02x%02x" % colour
        for value, (x0, y0, x1, y1), _ in numbers:
            cache = tmp / hexcol.lstrip("#")
            cache.mkdir(exist_ok=True)
            glyphs = [_digit(font, ch, hexcol, height, cache) for ch in str(value)]
            gap = scale // 2
            total = sum(g[0] for g in glyphs) + gap * (len(glyphs) - 1)
            x = int(round((x0 + x1 + 1) / 2 * scale - total / 2))
            row_y1 = baseline[next(r for r in baseline if abs(r - y0) <= 3)]
            top = (row_y1 + 1) * scale - height
            for gw, gh, px in glyphs:
                for yy in range(gh):
                    for xx in range(gw):
                        X, Y = x + xx, top + yy
                        a = px[(yy * gw + xx) * 4 + 3]
                        if a and 0 <= X < W and 0 <= Y < H:
                            o = (Y * W + X) * 4
                            for c in range(3):
                                out[o + c] = (out[o + c] * (255 - a) + px[(yy * gw + xx) * 4 + c] * a) // 255
                x += gw + gap
    return out


def erase(img, boxes):
    """Fills the given texel boxes [(x0, y0, w, h)] from the surrounding picture, edges inwards. Returns new rgba."""
    w, rgba = img["w"], bytearray(img["rgba"])
    todo = {(x, y) for x0, y0, bw, bh in boxes for y in range(y0, y0 + bh) for x in range(x0, x0 + bw)}
    while todo:
        done = {}
        for x, y in todo:
            acc, k = [0, 0, 0], 0
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (x + dx, y + dy)
                    if (dx or dy) and q not in todo and 0 <= q[0] < w and 0 <= q[1] < img["h"]:
                        o = (q[1] * w + q[0]) * 4
                        acc = [acc[c] + rgba[o + c] for c in range(3)]
                        k += 1
            if k >= 2:
                done[(x, y)] = [v // k for v in acc]
        if not done:
            break
        for (x, y), c in done.items():
            rgba[(y * w + x) * 4:(y * w + x) * 4 + 3] = bytes(c)
        todo -= set(done)
    return rgba


def clear(img, boxes):
    """Makes the given texel boxes [(x0, y0, w, h)] transparent (for marks drawn over nothing). Returns new rgba."""
    w, rgba = img["w"], bytearray(img["rgba"])
    for x0, y0, bw, bh in boxes:
        for y in range(y0, y0 + bh):
            for x in range(x0, x0 + bw):
                rgba[(y * w + x) * 4 + 3] = 0
    return rgba


def mark_colour(img, box):
    """Median colour of the bright texels in a box of the original picture (the mark's own colour)."""
    x0, y0, bw, bh = box
    lit = [img["rgba"][((y * img["w"]) + x) * 4:((y * img["w"]) + x) * 4 + 3]
           for y in range(y0, y0 + bh) for x in range(x0, x0 + bw)
           if img["rgba"][((y * img["w"]) + x) * 4 + 3] and _lum(img["rgba"], ((y * img["w"]) + x) * 4) > 150]
    if not lit:
        return (230, 230, 230)
    return tuple(sorted(c[i] for c in lit)[len(lit) // 2] for i in range(3))


def draw_mark(W, H, rgba, box, text, colour, font, scale):
    """Draws `text` with the text font into an upscaled picture: capitals as tall as the box less a texel, sitting on
    its bottom less a texel, from its left edge; light colour with a thin dark edge like the game's text."""
    import numpy as np
    x0, y0, bw, bh = box
    cap = (bh - 2) * scale
    with tempfile.TemporaryDirectory() as t:
        # Size the font so a capital H is `cap` pixels tall.
        probe = Path(t) / "h.png"
        subprocess.run(["magick", "-background", "none", "-fill", "white", "-font", str(font), "-pointsize", "100",
                        "label:H", "-trim", str(probe)], check=True)
        hh = int(subprocess.run(["magick", "identify", "-format", "%h", str(probe)], capture_output=True, text=True,
                                check=True).stdout)
        size = 100 * cap / max(hh, 1)
        out = Path(t) / "m.png"
        bw4, bh4 = bw * scale, bh * scale
        hexc = "#%02x%02x%02x" % tuple(colour)
        subprocess.run(["magick", "-size", f"{bw4}x{bh4}", "xc:none", "-font", str(font), "-pointsize", f"{size:.1f}",
                        "-fill", "#000000a0", "-stroke", "#000000a0", "-strokewidth", f"{scale * 0.5:.1f}",
                        "-annotate", f"+{scale // 2}+{bh4 - scale}", text,
                        "-fill", hexc, "-stroke", "none", "-annotate", f"+{scale // 2}+{bh4 - scale}", text,
                        f"PNG32:{out}"], check=True)
        from pngio import read_png
        mw, mh, m = read_png(out)
    dst = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4).astype(np.float32).copy()
    src = np.frombuffer(bytes(m), np.uint8).reshape(mh, mw, 4).astype(np.float32)
    region = dst[y0 * scale:y0 * scale + mh, x0 * scale:x0 * scale + mw]
    a_s, a_d = src[..., 3:] / 255.0, region[..., 3:] / 255.0  # straight alpha, mark over picture
    a_o = a_s + a_d * (1 - a_s)
    region[..., :3] = (src[..., :3] * a_s + region[..., :3] * a_d * (1 - a_s)) / np.maximum(a_o, 1e-6)
    region[..., 3:] = 255.0 * a_o
    return bytearray(np.clip(dst + 0.5, 0, 255).astype(np.uint8).tobytes())


def word_colour(img, box, dark=125):
    """Median colour of the dark texels in a box of the original picture (a painted word's ink)."""
    x0, y0, bw, bh = box
    w, rgba = img["w"], img["rgba"]
    ink = sorted(tuple(rgba[(y * w + x) * 4:(y * w + x) * 4 + 3]) for y in range(y0, y0 + bh) for x in range(x0, x0 + bw)
                 if _lum(rgba, (y * w + x) * 4) < dark)
    return ink[len(ink) // 2] if ink else (0x94, 0x5A, 0x4A)


def draw_word(W, H, rgba, box, text, colour, font, scale, align="right"):
    """Draws `text` with the text font into an upscaled picture where a painted word was erased before the upscale:
    its tallest letters as tall as the word's box, sitting on its bottom, ending at its right edge (align "right") or
    starting at its left; in the word's ink, with the stroke weight labels.py gives redrawn words."""
    import numpy as np
    x0, y0, bw, bh = box
    hexc = "#%02x%02x%02x" % tuple(colour)
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "w.png"
        subprocess.run(["magick", "-background", "none", "-fill", hexc, "-stroke", hexc, "-strokewidth", "12",
                        "-font", str(font), "-pointsize", "400", f"label:{text}", "-trim", "+repage",
                        "-resize", f"x{bh * scale}", f"PNG32:{out}"], check=True, stdout=subprocess.DEVNULL)
        mw, mh, m = read_png(out)
    x = (x0 + bw) * scale - mw if align == "right" else x0 * scale
    y = (y0 + bh) * scale - mh
    dst = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4).astype(np.float32).copy()
    src = np.frombuffer(bytes(m), np.uint8).reshape(mh, mw, 4).astype(np.float32)
    sx0, sy0 = max(0, -x), max(0, -y)
    x, y = max(0, x), max(0, y)
    src = src[sy0:sy0 + H - y, sx0:sx0 + W - x]
    region = dst[y:y + src.shape[0], x:x + src.shape[1]]
    a_s, a_d = src[..., 3:] / 255.0, region[..., 3:] / 255.0  # straight alpha, word over picture
    a_o = a_s + a_d * (1 - a_s)
    region[..., :3] = (src[..., :3] * a_s + region[..., :3] * a_d * (1 - a_s)) / np.maximum(a_o, 1e-6)
    region[..., 3:] = 255.0 * a_o
    return bytearray(np.clip(dst + 0.5, 0, 255).astype(np.uint8).tobytes())
