"""labels.py: redraw a small pre-drawn word image (map place names, page titles) with a TrueType font.

The original is split into its parts by connected components: the town dot (the outermost component on the side
labels.json names) and the letters. The word is drawn over the letters' box (same height, stretched
to the same width) in the letters' average colour, and the dot as a ring of the same size. Everything is rendered by
ImageMagick at 16 pixels per texel and box-filtered down to the pack's scale.
"""
import subprocess
import tempfile
from pathlib import Path

from pngio import read_png

SS = 16


def _components(w, h, rgba):
    seen, comps = set(), []
    for y in range(h):
        for x in range(w):
            if (x, y) in seen or rgba[(y * w + x) * 4 + 3] == 0:
                continue
            stack, comp = [(x, y)], []
            seen.add((x, y))
            while stack:
                cx, cy = stack.pop()
                comp.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen and rgba[(ny * w + nx) * 4 + 3]:
                        seen.add((nx, ny))
                        stack.append((nx, ny))
            comps.append(comp)
    return comps


def _box(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def render_label(img, text, font, scale, dot=None):
    """img: build_pack image dict; dot: "below", "above", "right" or "left" (where the town dot is) or None.
    Returns (W, H, rgba) at `scale` pixels per texel."""
    w, h, rgba = img["w"], img["h"], img["rgba"]
    comps = _components(w, h, rgba)
    dots = []
    if dot:
        pick = {"below": lambda c: _box(c)[3], "above": lambda c: -_box(c)[1],
                "right": lambda c: _box(c)[2], "left": lambda c: -_box(c)[0]}[dot]
        dots = [max(comps, key=pick)]
    pts = [p for c in comps if c not in dots for p in c]
    if not pts:
        raise ValueError(f"label {text!r}: no letters found")
    lx0, ly0, lx1, ly1 = _box(pts)
    n = len(pts)
    colour = tuple(sum(rgba[(y * w + x) * 4 + c] for x, y in pts) // n for c in range(3))
    hexcol = "#%02x%02x%02x" % colour

    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        word = tmp / "word.png"
        # The word, trimmed to its ink, then stretched over the letters' box.
        # Rendered at 400 pt, then shrunk to a few texels: the stroke adds the weight a book font lacks at that size.
        subprocess.run(["magick", "-background", "none", "-fill", hexcol, "-stroke", hexcol, "-strokewidth", "12",
                        "-font", str(font), "-pointsize", "400",
                        f"label:{text}", "-trim", "+repage", "-resize",
                        f"{(lx1 - lx0 + 1) * SS}x{(ly1 - ly0 + 1) * SS}!", f"PNG32:{word}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        draw = []
        for comp in dots:
            x0, y0, x1, y1 = _box(comp)
            cx, cy = (x0 + x1 + 1) / 2 * SS, (y0 + y1 + 1) / 2 * SS
            r = (max(x1 - x0, y1 - y0) + 1) / 2 * SS - SS * 0.45
            draw += ["-fill", "none", "-stroke", hexcol, "-strokewidth", f"{SS * 0.9:.1f}",
                     "-draw", f"circle {cx:.1f},{cy:.1f} {cx + r:.1f},{cy:.1f}"]
        out = tmp / "label.png"
        subprocess.run(["magick", "-size", f"{w * SS}x{h * SS}", "xc:none", str(word), "-geometry",
                        f"+{lx0 * SS}+{ly0 * SS}", "-composite", *draw, "-filter", "box", "-resize",
                        f"{w * scale}x{h * scale}!", f"PNG32:{out}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        return read_png(out)
