"""font.py: redraw the game's two font sheets (36x414 IA8, 3 columns x 46 rows of 12x9 cells) with a TrueType font.

Used by build_pack.py. Only letters, digits and plain punctuation are redrawn; the controller-button icons and the
rarer symbols keep the smoothed original. Each glyph keeps the game's metrics: the game measures every glyph's
advance from row 0 of its cell in the original sheet (Font::SetupBorg8), which a texture pack can't change, so the
new letter is drawn inside the original letter's ink box (narrowed when the font's letter is wider), capitals 7
texels tall from row 0, lowercase as the font's small capitals. Rendering is done by ImageMagick at 16 pixels per
texel and box-filtered down to the pack's scale.
"""
import subprocess
import tempfile
from pathlib import Path

from pngio import read_png

COLS, ROWS, CW, CH = 3, 46, 12, 9
SS = 16                                      # supersampling: pixels per texel while rendering
CAP_TEXELS = 6.85                            # capital height (rows 0-6, a little under 7 so round letters fit)

# struct_unk_.textIndexies (decomp/include/unkStructs.h): character -> cell index (column-major, 46 rows).
INDEX = {**{c: i for i, c in enumerate("0123456789")},
         **{c: 0x11 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")},
         **{c: 0x3F + i for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")},
         ":": 0x0A, "?": 0x0C, '"': 0x0D, "(": 0x0E, ")": 0x0F, ".": 0x2E, ",": 0x2F, "!": 0x30, "-": 0x31,
         "+": 0x32, "=": 0x33, "/": 0x38, "'": 0x3B}


def cell_origin(index):
    return (index // ROWS) * CW, (index % ROWS) * CH


def ink_box(rgba, w, index):
    """Columns of the cell that hold the original glyph (any alpha), or None."""
    x0, y0 = cell_origin(index)
    xs = [x for x in range(CW) for y in range(CH) if rgba[((y0 + y) * w + x0 + x) * 4 + 3]]
    return (min(xs), max(xs)) if xs else None


def is_font_sheet(img):
    return img["fmt"] == 5 and (img["w"], img["h"]) == (COLS * CW, ROWS * CH)


def _magick(args):
    subprocess.run(["magick", *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


BOLD = 0.32           # extra stroke weight in texels (the game's pixel font is heavy; book fonts are light)
STRETCH = (0.65, 1.45)  # how far a letter may be narrowed or widened to fill the original letter's width


def _render(font, char, pointsize, tmp):
    """One character, white on transparent, baseline at y=BASE, pen at x=PEN. Returns (W, H, rgba)."""
    out = tmp / "glyph.png"
    text = "\\\\" if char == "\\" else char
    _magick(["-size", f"{CANVAS_W}x{CANVAS_H}", "xc:none", "-fill", "white", "-stroke", "white",
             "-strokewidth", f"{BOLD * SS:.2f}", "-font", str(font),
             "-pointsize", f"{pointsize:.3f}", "-annotate", f"+{PEN}+{BASE}", text, f"PNG32:{out}"])
    return read_png(out)


CANVAS_W, CANVAS_H, PEN, BASE = 600, 400, 100, 300


def _bbox(W, H, rgba):
    xs, ys = [], []
    for y in range(H):
        row = rgba[y * W * 4:(y + 1) * W * 4]
        for x in range(W):
            if row[x * 4 + 3] > 8:
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


def redraw(rgba, w, h, font, scale):
    """rgba: the decoded sheet. Returns {index: (scale*CW, scale*CH, rgba)}: new cells at `scale` pixels per texel."""
    font = Path(font)
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        # Point size that makes a capital H CAP_TEXELS tall at SS pixels per texel.
        W, H, px = _render(font, "H", 200, tmp)
        bx = _bbox(W, H, px)
        pointsize = 200 * (CAP_TEXELS * SS) / (bx[3] - bx[1] + 1)
        cells = {}
        for char, index in INDEX.items():
            box = ink_box(rgba, w, index)
            if box is None:
                continue
            W, H, px = _render(font, char, pointsize, tmp)
            gb = _bbox(W, H, px)
            if gb is None:
                continue
            # Horizontal: fill the original letter's ink box, within STRETCH; centred in it when it can't.
            avail = (box[1] - box[0] + 1) * SS
            gw = gb[2] - gb[0] + 1
            sx = min(max(avail / gw, STRETCH[0]), STRETCH[1])
            # Capitals that can't fill the box sit at its right: they mostly start words, so the slack joins the
            # space before the word instead of opening a gap inside it ("S hort").
            slack = avail - gw * sx
            left = box[0] * SS + (slack if char.isupper() else slack / 2)
            # Vertical: the font's baseline at the bottom of row 6 (cap top at row 0).
            base_y = 7 * SS
            big = bytearray(CW * SS * CH * SS * 4)
            BW = CW * SS
            for y in range(CH * SS):
                sy = BASE - base_y + y
                if not 0 <= sy < H:
                    continue
                for x in range(BW):
                    fx = gb[0] + (x - left) / sx
                    ix = int(fx)
                    if not gb[0] <= ix <= gb[2]:
                        continue
                    o = (sy * W + ix) * 4
                    big[(y * BW + x) * 4:(y * BW + x) * 4 + 4] = px[o:o + 4]
            # Box filter down to `scale` pixels per texel.
            f = SS // scale
            ow, oh = CW * scale, CH * scale
            small = bytearray(ow * oh * 4)
            for y in range(oh):
                for x in range(ow):
                    a = 0
                    for yy in range(f):
                        row = ((y * f + yy) * BW + x * f) * 4
                        for xx in range(f):
                            a += big[row + xx * 4 + 3]
                    small[(y * ow + x) * 4:(y * ow + x) * 4 + 4] = bytes((255, 255, 255, a // (f * f)))
            cells[index] = (ow, oh, small)
        return cells
