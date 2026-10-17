"""font.py: redraw the game's two font sheets (36x414 IA8, 3 columns x 46 rows of 12x9 cells) with a TrueType font.

Used by build_pack.py. Only letters, digits and plain punctuation are redrawn; the controller-button icons and the
rarer symbols keep the smoothed original. The game measures every glyph's width from its cell in the original sheet
(Font::SetupBorg8) and both draws that many columns of the cell and advances by it; the original pixel font is wide
and loosely spaced, so a book font squeezed into those widths looks uneven. So the letters get the font's own
proportions: each width is the font's advance plus TRACK texels, the glyph drawn unstretched and centred in it, and
the game is given the new widths (src/game/font_widths.h, written by `font.py --header`; src/game/patches.cpp,
"HD text widths", applies them when the HD UI pack is in use). Capitals are 7 texels tall from row 0, lowercase are
the font's small capitals. Rendering is done by ImageMagick at 16 pixels per texel and box-filtered down to the
pack's scale.

    tools/hdui/font.py --header    (re)write src/game/font_widths.h from the font
"""
import subprocess
import sys
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
TRACK = 0.6           # texels of letter-spacing on top of each letter's own advance (small caps read better open)
DEFAULT_FONT = Path(__file__).resolve().parent / "fonts/MarcellusSC-Regular.ttf"
# In an hd-ui pack built with a font: the id of the widths its letters were drawn for (widths_id). The game gives
# the font sheets its own widths only when this matches its kWidthsId, so an older pack, or one with the original
# letters (build_pack.py --font none), keeps the original widths instead of cutting its letters off.
PACK_MARKER = "aidyn-text-widths.txt"
HEADER = Path(__file__).resolve().parents[2] / "src/game/font_widths.h"


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


def _pointsize(font, tmp):
    """Point size that makes a capital H CAP_TEXELS tall at SS pixels per texel."""
    W, H, px = _render(font, "H", 200, tmp)
    bx = _bbox(W, H, px)
    return 200 * (CAP_TEXELS * SS) / (bx[3] - bx[1] + 1)


def _advances(font, pointsize, extra=""):
    """{char: advance in texels}: the width of "H<c>H" less "HH", so the font's own side bearings count."""
    def width(text):
        out = subprocess.run(["magick", "-font", str(font), "-pointsize", f"{pointsize:.3f}", "label:" + text,
                              "-format", "%w", "info:"], check=True, capture_output=True, text=True).stdout
        return int(out)
    hh = width("HH")
    return {c: (width("H" + c + "H") - hh) / SS for c in list(INDEX) + list(extra)}


SPACE_CELL = 0x10  # textIndexies[' ']: never drawn, but the game advances by its width


def widths(font=DEFAULT_FONT):
    """{cell index: width in texels} for every redrawn character and the space: the font's advance plus TRACK."""
    with tempfile.TemporaryDirectory() as t:
        adv = _advances(font, _pointsize(Path(font), Path(t)), extra=" ")
    table = {INDEX[c]: max(1, min(CW, round(a + TRACK))) for c, a in adv.items() if c in INDEX}
    table[SPACE_CELL] = max(1, min(CW, round(adv[" "] + TRACK)))
    return table


def redraw(rgba, w, h, font, scale):
    """rgba: the decoded sheet. Returns {index: (scale*CW, scale*CH, rgba)}: new cells at `scale` pixels per texel,
    each glyph unstretched and centred in its new width (widths())."""
    font = Path(font)
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        pointsize = _pointsize(font, tmp)
        adv = _advances(font, pointsize)
        cells = {}
        for char, index in INDEX.items():
            if ink_box(rgba, w, index) is None:
                continue  # not in this sheet
            W, H, px = _render(font, char, pointsize, tmp)
            if _bbox(W, H, px) is None:
                continue
            new_w = max(1, min(CW, round(adv[char] + TRACK)))
            pen = (new_w - adv[char]) / 2 * SS   # the glyph's origin in the cell: the spare space split both sides
            base_y = 7 * SS                       # the font's baseline at the bottom of row 6 (cap top at row 0)
            BW = CW * SS
            big = bytearray(BW * CH * SS * 4)
            for y in range(CH * SS):
                sy = BASE - base_y + y
                if not 0 <= sy < H:
                    continue
                for x in range(new_w * SS):
                    sx = PEN + int(x - pen)
                    if 0 <= sx < W:
                        o = (sy * W + sx) * 4
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


def widths_id(table):
    """A short id of a width table ({cell: width}), the same in font_widths.h and an hd-ui pack's PACK_MARKER."""
    import hashlib
    return hashlib.sha1(",".join(f"{i}:{w}" for i, w in sorted(table.items())).encode()).hexdigest()[:12]


def write_header(path=HEADER, font=DEFAULT_FONT):
    table = widths(font)
    rows = ", ".join(f"{{0x{i:02X}, {wd}}}" for i, wd in sorted(table.items()))
    path.write_text(f"""// src/game/font_widths.h: generated by tools/hdui/font.py --header from {Path(font).name}; don't edit.
// The HD text's letter widths in texels ({{cell index in the font sheets, width}}): the font's own advance plus
// {TRACK} texels, for both sheets (MainFont, SlimFont). src/game/patches.cpp ("HD text widths") applies them.
#pragma once
#include <cstdint>

namespace aidyn::font {{
    struct Width {{ uint8_t cell, width; }};
    inline constexpr Width kWidths[] = {{ {rows} }};
    // An hd-ui pack drawn for these widths has {PACK_MARKER} holding this id (tools/hdui/font.py widths_id).
    inline constexpr char kPackMarker[] = "{PACK_MARKER}";
    inline constexpr char kWidthsId[] = "{widths_id(table)}";
}}
""")
    print(f"font.py: {len(table)} widths -> {path}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--header"]:
        write_header()
    else:
        sys.exit(__doc__)
