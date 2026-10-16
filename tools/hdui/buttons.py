"""buttons.py: Xbox-controller button icons for the font's N64 button glyphs (used by build_pack.py).

The main font sheet holds 11 button icons that the game prints inside text ("Details [C-down]"): A, B, C-right,
C-left, C-up, C-down, Start, D-pad, Z, L, R, in cells 0x5C..0x66 (struct_unk_.textIndexies), and tints them with
Font::GetButtonColor, which src/game/patches.cpp makes white while this pack is loaded. Each cell is redrawn as the
Xbox button the port maps to that N64 button (src/main/input.cpp), in full colour:

  A -> A (green)   B -> B (red)   C-down -> X (blue)   C-up -> Y (yellow)   C-left -> View   C-right -> RT
  Start -> Menu    D-pad -> D-pad    Z -> LT    L -> LB    R -> RB

Drawn by ImageMagick at 16 pixels per texel inside the original icon's box (8 x 9 texels) and box-filtered down.
"""
import subprocess
import tempfile
from pathlib import Path

from pngio import read_png

SS = 16
CELLS = {0x5C: ("face", "A", "#5cb83c"), 0x5D: ("face", "B", "#d8412f"), 0x61: ("face", "X", "#2f7bdc"),
         0x60: ("face", "Y", "#f0bd2c"), 0x5E: ("trigger", "RT", None), 0x64: ("trigger", "LT", None),
         0x65: ("bumper", "LB", None), 0x66: ("bumper", "RB", None), 0x5F: ("view", None, None),
         0x62: ("menu", None, None), 0x63: ("dpad", None, None)}
DARK, LIGHT = "#3a3d42", "#f2f2f2"


def bold_font():
    r = subprocess.run(["fc-match", "-f", "%{file}", "sans:bold"], capture_output=True, text=True)
    return r.stdout.strip() or None


def draw(kind, text, colour, w, h, scale, font):
    """One icon, w x h texels, at `scale` pixels per texel. Returns rgba."""
    W, H = w * SS, h * SS
    cx, cy = W / 2, H / 2
    r = min(W, H) / 2 - SS * 0.3
    cmd = ["magick", "-size", f"{W}x{H}", "xc:none", "-strokewidth", f"{SS * 0.35:.1f}"]
    def label(t, size, fill=LIGHT):
        return ["-stroke", "none", "-fill", fill, "-font", font, "-pointsize", f"{size:.0f}", "-gravity", "center",
                "-annotate", "+0+0", t, "-gravity", "northwest"]
    if kind == "face":
        cmd += ["-fill", colour, "-stroke", "#202020", "-draw", f"circle {cx},{cy} {cx + r},{cy}"]
        cmd += label(text, r * 1.45)
    elif kind in ("trigger", "bumper"):
        top = cy - r * (0.62 if kind == "bumper" else 0.85)
        bot = cy + r * (0.62 if kind == "bumper" else 0.85)
        rad = r * (0.25 if kind == "bumper" else 0.5)
        cmd += ["-fill", DARK, "-stroke", "#202020",
                "-draw", f"roundrectangle {cx - r},{top} {cx + r},{bot} {rad},{rad}"]
        cmd += label(text, r * 0.95)
    elif kind == "view":
        cmd += ["-fill", DARK, "-stroke", "#202020", "-draw", f"circle {cx},{cy} {cx + r},{cy}",
                "-fill", "none", "-stroke", LIGHT, "-strokewidth", f"{SS * 0.4:.1f}",
                "-draw", f"rectangle {cx - r * 0.5},{cy - r * 0.42} {cx + r * 0.15},{cy + r * 0.12}",
                "-draw", f"rectangle {cx - r * 0.15},{cy - r * 0.12} {cx + r * 0.5},{cy + r * 0.42}"]
    elif kind == "menu":
        cmd += ["-fill", DARK, "-stroke", "#202020", "-draw", f"circle {cx},{cy} {cx + r},{cy}",
                "-stroke", LIGHT, "-strokewidth", f"{SS * 0.5:.1f}"]
        for dy in (-0.35, 0, 0.35):
            cmd += ["-draw", f"line {cx - r * 0.5},{cy + dy * r} {cx + r * 0.5},{cy + dy * r}"]
    elif kind == "dpad":
        a = r * 0.36
        cmd += ["-fill", DARK, "-stroke", "#202020",
                "-draw", f"polygon {cx - a},{cy - r} {cx + a},{cy - r} {cx + a},{cy - a} {cx + r},{cy - a} "
                         f"{cx + r},{cy + a} {cx + a},{cy + a} {cx + a},{cy + r} {cx - a},{cy + r} {cx - a},{cy + a} "
                         f"{cx - r},{cy + a} {cx - r},{cy - a} {cx - a},{cy - a}",
                "-fill", LIGHT, "-stroke", "none", "-draw", f"circle {cx},{cy} {cx + a * 0.45},{cy}"]
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "b.png"
        subprocess.run(cmd + ["-filter", "box", "-resize", f"{w * scale}x{h * scale}!", f"PNG32:{out}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        return read_png(out)[2]


def icons(scale, box_w=8, box_h=9):
    """{cell: (cell w px, cell h px, rgba)}: full 12 x 9 texel cells with the icon in columns 0..box_w-1."""
    font = bold_font()
    out = {}
    for cell, (kind, text, colour) in CELLS.items():
        icon = draw(kind, text, colour, box_w, box_h, scale, font)
        CW, CH = 12 * scale, 9 * scale
        rgba = bytearray(CW * CH * 4)
        for y in range(box_h * scale):
            rgba[y * CW * 4:y * CW * 4 + box_w * scale * 4] = icon[y * box_w * scale * 4:(y + 1) * box_w * scale * 4]
        out[cell] = (CW, CH, rgba)
    return out


N64_TO_XBOX = {"A": 0x5C, "B": 0x5D, "CD": 0x61, "CU": 0x60, "CR": 0x5E, "CL": 0x5F, "START": 0x62, "DPAD": 0x63,
               "Z": 0x64, "L": 0x65, "R": 0x66}


def icon_for(n64_button, w, h, scale):
    """The Xbox icon for an N64 button, w x h texels at `scale` pixels per texel (rgba)."""
    kind, text, colour = CELLS[N64_TO_XBOX[n64_button]]
    return draw(kind, text, colour, w, h, scale, bold_font())
