#!/usr/bin/env python3
"""text_pack.py: build the rechronicled-text texture pack: the game's letters redrawn with the text font, and the
controller-button icons drawn as Xbox buttons, in every form the game loads them.

The letters are the only part of the HD menu pack (hd-ui) whose look depends on the game: it gives the font sheets
new widths (src/game/font_widths.h) when the letters were drawn for them, so letters from one version of the pack and
widths from another come out squeezed or gappy. This pack is shipped with every release beside the game, so the
letters always match the widths of the game they came with, whatever hd-ui is installed (or none). It holds nothing
from the game: the letters are rendered from fonts/MarcellusSC-Regular.ttf (font.py), the buttons drawn by buttons.py.
The ROM is needed only to work out which game texture each one replaces (the hashes come from its font sheets).

    tools/hdui/text_pack.py [--rom baserom.z64] [--out <data>/texture-packs/rechronicled-text]

build_pack.py uses the same functions for the hd-ui pack's letters, and also keeps the font sheets' other cells
(rarer symbols, smoothed from the original pixels), which this pack leaves to hd-ui or the original.
Needs build/cmake/tmemhash (cmake --build build/cmake --target tmemhash) and ImageMagick.
"""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pngio import decode_borg8, upscale_glyph, write_png  # noqa: E402
import buttons  # noqa: E402
import font as fontsheet  # noqa: E402
import portraits as romportraits  # noqa: E402
import romassets  # noqa: E402
import romimages  # noqa: E402

PROJECT = Path(__file__).resolve().parents[2]
SCALE = 4
NAME = "rechronicled-text"


def font_sheets(rom_path, decomp, font, scale=SCALE):
    """The ROM's two font sheets (BORG8_MainFont, BORG8_SlimFont), by image key (as build_pack.py's image_key):
    {key: {name, fmt, w, h, pixels, rgba, sheet: (W, H, rgba at `scale`), drawn: cells that are new drawings}}.
    Every cell is smoothed (pngio.upscale_glyph); with a font (not None or "none") the letters, digits and punctuation
    are redrawn with it, and MainFont's controller-button icons become Xbox buttons."""
    rom = romassets.Rom(rom_path)
    redraw = font is not None and str(font) != "none"
    out = {}
    for index, name in romassets.borg8_names(decomp).items():
        if name not in ("MainFont", "SlimFont"):
            continue
        fmt, w, h, pixels, palette = rom.image(index)
        key = hashlib.sha1(bytes([fmt]) + w.to_bytes(2, "big") + h.to_bytes(2, "big") + pixels).hexdigest()[:16]
        rgba = decode_borg8(fmt, w, h, pixels, None)
        W4, H4 = w * scale, h * scale
        big = bytearray(W4 * H4 * 4)

        def put(index, cw, ch, px):
            cx, cy = fontsheet.cell_origin(index)
            for y in range(ch):
                d = ((cy * scale + y) * W4 + cx * scale) * 4
                big[d:d + cw * 4] = px[y * cw * 4:(y + 1) * cw * 4]

        for cell in range(fontsheet.COLS * fontsheet.ROWS):
            cx, cy = fontsheet.cell_origin(cell)
            raw = bytearray(fontsheet.CW * fontsheet.CH * 4)
            for y in range(fontsheet.CH):
                o = ((cy + y) * w + cx) * 4
                raw[y * fontsheet.CW * 4:(y + 1) * fontsheet.CW * 4] = rgba[o:o + fontsheet.CW * 4]
            put(cell, *upscale_glyph(fontsheet.CW, fontsheet.CH, raw, scale))
        drawn = set()
        if redraw:
            for cell, (cw, ch, px) in fontsheet.redraw(rgba, w, h, font, scale).items():
                put(cell, cw, ch, px)
                drawn.add(cell)
        if name == "MainFont":
            for cell, (cw, ch, px) in buttons.icons(scale).items():
                put(cell, cw, ch, px)
                drawn.add(cell)
        out[key] = {"name": name, "fmt": fmt, "w": w, "h": h, "pixels": pixels, "rgba": rgba,
                    "sheet": (W4, H4, big), "drawn": drawn}
    return out


def letter_textures(sheets, new_widths, tmemhash, scale=SCALE, only_drawn=False):
    """Every letter of the font sheets as the game loads it, whole and clipped (Font::PrintMain trims a letter's box at
    a text box's edges: any left/right part at full height, any top/bottom part at full width): [(hash, w, h, rgba)].
    new_widths: {cell: width} the game is given for redrawn letters (font_widths.h). only_drawn: just the cells that
    are new drawings (letters, buttons), not the smoothed originals."""
    jobs, rects = [], []
    for key, f in sheets.items():
        pixels, w = f["pixels"], f["w"]
        for cell in range(fontsheet.COLS * fontsheet.ROWS):
            if only_drawn and cell not in f["drawn"]:
                continue
            cx, cy = fontsheet.cell_origin(cell)
            base, kern = cy * w + cx, fontsheet.CW
            while kern > 1 and pixels[base + kern - 2] == pixels[base + fontsheet.CW - 1]:
                kern -= 1  # the letter's advance, as Font::SetupBorg8 measures it
            if cell in new_widths and fontsheet.ink_box(f["rgba"], w, cell) is not None:
                kern = new_widths[cell]  # redrawn letters: the width the game is given instead
            boxes = {(cx + a, b - a, cy, fontsheet.CH) for a in range(kern) for b in range(a + 1, kern + 1)}
            boxes |= {(cx, kern, cy + c, d - c) for c in range(fontsheet.CH) for d in range(c + 1, fontsheet.CH + 1)}
            for x0, tw, y0, th in sorted(boxes):
                tmem, line = romimages.tmem_rect(f["fmt"], w, pixels, None, x0, tw, y0, th)
                jobs.append((tmem, 3, 1, line, tw, th, 0))
                rects.append((key, x0, tw, y0, th))
    out = []
    for h_, (key, x0, tw, y0, th) in zip(romportraits.hashes(tmemhash, jobs), rects):
        W4, H4, big = sheets[key]["sheet"]
        cut = bytearray(tw * scale * th * scale * 4)
        for y in range(th * scale):
            o = ((y0 * scale + y) * W4 + x0 * scale) * 4
            cut[y * tw * scale * 4:(y + 1) * tw * scale * 4] = big[o:o + tw * scale * 4]
        out.append((h_, tw * scale, th * scale, cut))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", default=str(PROJECT / "baserom.z64"))
    ap.add_argument("--out", default=str(Path.home() / ".local/share/AidynReChronicled/texture-packs" / NAME))
    ap.add_argument("--font", default=str(fontsheet.DEFAULT_FONT))
    args = ap.parse_args()
    tmemhash = PROJECT / "build/cmake/tmemhash"
    if not tmemhash.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    final = Path(args.out)
    out = final.with_name(final.name + ".new")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    sheets = font_sheets(Path(args.rom), PROJECT / "decomp", args.font)
    new_widths = fontsheet.widths(args.font)
    entries, written = [], set()
    for h_, w, h, rgba in letter_textures(sheets, new_widths, tmemhash, only_drawn=True):
        if h_ in written:
            continue  # the same texture from both sheets
        written.add(h_)
        write_png(out / f"{h_}.png", w, h, rgba)
        entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "preload"})
    db = {
        "configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream",
                          "defaultShift": "none", "hashVersion": 5},
        "textures": entries, "operationFilters": [], "shiftFilters": [], "extraFiles": [],
    }
    (out / "rt64.json").write_text(json.dumps(db, indent=1) + "\n")
    # The widths the letters were drawn for: the game gives them to the font sheets only when they match its own.
    (out / fontsheet.PACK_MARKER).write_text(fontsheet.widths_id(new_widths) + "\n")
    old = final.with_name(final.name + ".old")
    if old.exists():
        shutil.rmtree(old)
    if final.exists():
        final.rename(old)
    out.rename(final)
    if old.exists():
        shutil.rmtree(old)
    print(f"text_pack.py: {len(entries)} letter and button textures in {final}")


if __name__ == "__main__":
    main()
