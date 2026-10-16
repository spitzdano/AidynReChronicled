#!/usr/bin/env python3
"""build_pack.py: build the HD UI texture pack from a dump session (see tools/hdui/README.md).

Inputs (all under build/hdui, gitignored; made by running the game with AIDYN_DUMP_TEXTURES and AIDYN_UI_LOG):
  dumps/   RT64 texture dumps (<hash>.v5.tmem, .tile.json, .rice.json)
  ui/      every 2D image the game drew, whole (images.jsonl, <address>.bin, .pal)

1. Decodes the dumps to PNG (toolkit scripts/rt64_dumps_to_png.py) and the UI images with pngio.decode_borg8.
2. Finds, for every dumped texture, the UI image and rectangle it was loaded from: the load's RDRAM address and
   tile origin give the position, and the texture's pixels must equal that rectangle of the image exactly, so a
   texture that isn't UI (or a stale address from another session) is never matched.
3. Upscales 4x with Real-ESRGAN (toolkit scripts/get-upscaler.sh): images that arrive as full-width strips are
   upscaled whole so the strips join without seams; atlas cells (icons) one by one, padded. Font glyphs (greyscale
   atlas cells) are smooth-scaled instead (pngio.upscale_glyph): crisp edges that stay inside the glyph's cell.
   Portraits are doubled (nearest) before the 4x upscale, giving 8x: as single texels the upscaler repaints facial
   details (a blue iris came out white); as 2x2 blocks they survive.
   Tiny full-colour text labels are left out: it misspells small lettering.
4. Cuts each texture's rectangle out of the upscaled image as <hash>.png and writes rt64.json.

Output: ~/.local/share/AidynReChronicled/texture-packs/hd-ui (or --out). Everything in it is derived from the ROM,
so it never goes in the repository.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pngio import BPP, decode_borg8, read_png, upscale_glyph, write_png  # noqa: E402
import font as fontsheet  # noqa: E402
import labels as wordlabels  # noqa: E402
import repaint  # noqa: E402
import portraits as romportraits  # noqa: E402
import romassets  # noqa: E402
import romimages  # noqa: E402
import buttons  # noqa: E402
import bigstrips  # noqa: E402
from pngio import restore_colours, upscale_bicubic  # noqa: E402

PROJECT = Path(__file__).resolve().parents[2]


def image_key(fmt, w, h, pixels, palette):
    """An image's identity: format, size, pixels and palette (same as load_ui's key for captured images)."""
    return hashlib.sha1(bytes([fmt]) + w.to_bytes(2, "big") + h.to_bytes(2, "big") + pixels
                        + (palette or b"")).hexdigest()[:16]
LABELS = {k: v for k, v in json.loads((Path(__file__).resolve().parent / "labels.json").read_text()).items() if k != "_"}
SCALE = 4


def toolkit():
    if os.environ.get("N64RECOMP_TOOLKIT"):
        return Path(os.environ["N64RECOMP_TOOLKIT"])
    env = (PROJECT / "tools/toolkit.env").read_text()
    for line in env.splitlines():
        if line.startswith("TOOLKIT="):  # TOOLKIT=${N64RECOMP_TOOLKIT:-/path}
            value = line.split("=", 1)[1].strip().strip('"')
            if value.startswith("${") and ":-" in value:
                value = value.split(":-", 1)[1].rstrip("}")
            return Path(value)
    sys.exit("tools/toolkit.env has no TOOLKIT=")


def load_ui(ui_dir):
    """Unique UI images: key -> {fmt, w, h, rgba}, and every logged (address, size, key) instance."""
    images, instances = {}, []
    for line in (ui_dir / "images.jsonl").read_text().splitlines():
        j = json.loads(line)
        name = j.get("file", j["pixels"])  # "file": address_contenthash, since a picture can load where another was
        fmt, w, h = j["format"], j["width"], j["height"]
        if w * h <= 4:
            continue  # 1x1 / 2x2 fill images: nothing to upscale
        if j["borg"] != -1:
            continue  # loaded art has -1 here; anything else is an image the game made at runtime (pause background and
                      # save thumbnail are captured frames), different every time
        data = (ui_dir / f"{name}.bin").read_bytes()
        pal_path = ui_dir / f"{name}.pal"
        pal = pal_path.read_bytes() if pal_path.exists() else None
        key = hashlib.sha1(bytes([fmt]) + w.to_bytes(2, "big") + h.to_bytes(2, "big") + data + (pal or b"")).hexdigest()[:16]
        if fmt in (1, 2, 4, 7) and h <= 9 and key not in LABELS:
            continue  # tiny full-colour labels not listed in labels.json keep their original pixels: the upscaler
                      # misspells small lettering. The listed ones are redrawn (or upscaled) by name.
        if key not in images:
            images[key] = {"fmt": fmt, "w": w, "h": h, "rgba": decode_borg8(fmt, w, h, data, pal)}
        address = int(j["pixels"], 16) & 0x1FFFFFFF
        instances.append((address, w * h * BPP[fmt] // 8, key))
    return images, instances


def locate(rice, tile_json, img, address):
    """Top-left texel of the loaded rectangle inside the image, from the load operation, or None."""
    bpp = BPP[img["fmt"]]
    offset_texels = (rice["texture"]["address"] & 0x1FFFFFFF) - address
    offset_texels = offset_texels * 8 // bpp
    t = rice["tile"]
    if rice["type"] == "Tile":
        row, col = divmod(offset_texels, max(rice["texture"]["width"], 1))
        return col + (t["uls"] >> 2), row + (t["ult"] >> 2)
    if rice["type"] == "Block":
        start = offset_texels + (t["uls"] >> 2 if t["uls"] > 3 else t["uls"])
        row, col = divmod(start, img["w"])
        return col, row + t["ult"]
    return None


def matches(img, x0, y0, tw, th, tex):
    """True when every texel of the texture that lies inside the image equals the image (and some do)."""
    w, h, rgba = img["w"], img["h"], img["rgba"]
    inside = 0
    for y in range(th):
        iy = y0 + y
        if iy < 0 or iy >= h:
            continue
        for x in range(tw):
            ix = x0 + x
            if ix < 0 or ix >= w:
                continue
            a, b = (iy * w + ix) * 4, (y * tw + x) * 4
            ta, ia = tex[b + 3], rgba[a + 3]
            if ta == 0 and ia == 0:
                inside += 1
                continue  # colour of fully transparent texels doesn't matter
            if rgba[a:a + 4] != tex[b:b + 4]:
                return False
            inside += 1
    return inside > 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--work", default=str(PROJECT / "build/hdui"))
    ap.add_argument("--out", default=str(Path.home() / ".local/share/AidynReChronicled/texture-packs/hd-ui"))
    ap.add_argument("--model", default="realesrgan-x4plus-anime")
    ap.add_argument("--font", default=str(Path(__file__).resolve().parent / "fonts/MarcellusSC-Regular.ttf"),
                    help='TrueType font for the game text (letters, digits, punctuation); "none" keeps the original '
                         "pixel font, smoothed")
    args = ap.parse_args()
    work, out = Path(args.work), Path(args.out)
    tk = toolkit()
    esrgan = tk / "opt/realesrgan/realesrgan-ncnn-vulkan"
    if not esrgan.exists():
        sys.exit(f"no upscaler: run {tk}/scripts/get-upscaler.sh")

    subprocess.run([sys.executable, "-I", str(tk / "scripts/rt64_dumps_to_png.py"), str(work / "dumps"), str(work / "png")], check=True)
    images, instances = load_ui(work / "ui")
    print(f"build_pack.py: {len(images)} unique UI images from {len(instances)} logged draws")

    # 2. Match dumped textures to UI images.
    mapping = {}  # hash -> (key, x0, y0, w, h)
    for rice_path in sorted((work / "dumps").glob("*.rice.json")):
        base = rice_path.name[: -len(".rice.json")]
        hash_ = base.split(".v")[0]
        rice = json.loads(rice_path.read_text())
        tile = json.loads((work / "dumps" / f"{base}.tile.json").read_text())
        a = rice["texture"]["address"] & 0x1FFFFFFF
        png_path = work / "png" / f"{hash_}.png"
        if not png_path.exists():
            continue
        tex = None
        for address, size, key in instances:
            if not (address <= a < address + size):
                continue
            img = images[key]
            pos = locate(rice, tile, img, address)
            if pos is None:
                continue
            if tex is None:
                tw, th, tex = read_png(png_path)
            if matches(img, pos[0], pos[1], tw, th, tex):
                mapping[hash_] = (key, pos[0], pos[1], tw, th)
                break
    (work / "mapping.json").write_text(json.dumps({h: list(m) for h, m in sorted(mapping.items())}, indent=0) + "\n")
    used = sorted({m[0] for m in mapping.values()})
    print(f"build_pack.py: {len(mapping)} textures matched to {len(used)} UI images")

    # 3. Upscale. A texture that spans its image's full width is a strip of a larger picture (logos, parchment):
    #    those images are upscaled whole so the strips join without seams. Anything narrower is a cell cut from an
    #    atlas (font glyphs, icons): it is upscaled on its own, padded with transparent texels, so the upscaler
    #    can't smear neighbouring cells into it.
    PAD = 4
    whole = {m[0] for m in mapping.values() if m[3] >= images[m[0]]["w"]}
    # Big images (too big for one TMEM load): every one in the ROM, captured or not, drawn as fixed full-width strips
    # (bigstrips.py; the game side is "Big pictures" in src/game/patches.cpp). Upscaled whole like the strips above;
    # clouds smooth-scaled instead (the upscaler gives their soft edges hard outlines).
    big_keys = {}
    for index, (name, img) in bigstrips.big_images(PROJECT / "baserom.z64", PROJECT / "decomp").items():
        key = image_key(*img)
        big_keys[key] = (name, img)
        fmt, w, h, pixels, palette = img
        images.setdefault(key, {"fmt": fmt, "w": w, "h": h, "rgba": decode_borg8(fmt, w, h, pixels, palette)})
    clouds = {key for key, (name, _) in big_keys.items() if name.startswith("Cloud")}
    whole = sorted((whole | set(big_keys)) - clouds)
    cells = {h: m for h, m in mapping.items() if m[0] not in whole and m[0] not in big_keys}
    shutil.rmtree(work / "upscale", ignore_errors=True)
    src, big = work / "upscale/in", work / "upscale/out"
    src.mkdir(parents=True)
    big.mkdir(parents=True)
    painted = {}  # key -> numbers removed before the upscale (labels.json "numbers"), redrawn after it
    for key in whole:
        img = images[key]
        rgba = img["rgba"]
        if "numbers" in LABELS.get(key, {}) and args.font != "none":
            rgba, painted[key] = repaint.clean(img, LABELS[key]["numbers"])
        if "marks" in LABELS.get(key, {}) and args.font != "none":
            rgba = repaint.clear(dict(img, rgba=rgba), [tuple(m[:4]) for m in LABELS[key]["marks"]])
        if "buttons" in LABELS.get(key, {}):
            # Erased a little wider than the icon: its anti-aliased rim is paler than the icon and would bleed back in.
            rgba = repaint.erase(dict(img, rgba=rgba), [(x - 3, y - 2, w + 6, h + 4)
                                                        for x, y, w, h, _ in LABELS[key]["buttons"]])
        write_png(src / f"img_{key}.png", img["w"], img["h"], rgba)
    # Font sheets: every cell smoothed (pngio.upscale_glyph), then letters, digits and punctuation redrawn with
    # --font (font.py). Glyph textures are cut from this 4x sheet.
    # The two font sheets come from the ROM (BORG8_MainFont, BORG8_SlimFont), so every letter is covered, seen or not.
    rom = romassets.Rom(PROJECT / "baserom.z64")
    font_raw, font_name = {}, {}
    for index, name in romassets.borg8_names(PROJECT / "decomp").items():
        if name in ("MainFont", "SlimFont"):
            fmt, w, h, pixels, palette = rom.image(index)
            key = hashlib.sha1(bytes([fmt]) + w.to_bytes(2, "big") + h.to_bytes(2, "big") + pixels).hexdigest()[:16]
            images.setdefault(key, {"fmt": fmt, "w": w, "h": h, "rgba": decode_borg8(fmt, w, h, pixels, None)})
            font_raw[key] = pixels
            font_name[key] = name
    sheets = {}
    for key in sorted(font_raw):
        img = images[key]
        W4 = img["w"] * SCALE
        big4 = bytearray(W4 * img["h"] * SCALE * 4)
        for index in range(fontsheet.COLS * fontsheet.ROWS):
            cx, cy = fontsheet.cell_origin(index)
            cell = bytearray(fontsheet.CW * fontsheet.CH * 4)
            for y in range(fontsheet.CH):
                o = ((cy + y) * img["w"] + cx) * 4
                cell[y * fontsheet.CW * 4:(y + 1) * fontsheet.CW * 4] = img["rgba"][o:o + fontsheet.CW * 4]
            cw4, ch4, px = upscale_glyph(fontsheet.CW, fontsheet.CH, cell, SCALE)
            for y in range(ch4):
                d = ((cy * SCALE + y) * W4 + cx * SCALE) * 4
                big4[d:d + cw4 * 4] = px[y * cw4 * 4:(y + 1) * cw4 * 4]
        if args.font != "none":
            for index, (cw4, ch4, px) in fontsheet.redraw(img["rgba"], img["w"], img["h"], args.font, SCALE).items():
                cx, cy = fontsheet.cell_origin(index)
                for y in range(ch4):
                    d = ((cy * SCALE + y) * W4 + cx * SCALE) * 4
                    big4[d:d + cw4 * 4] = px[y * cw4 * 4:(y + 1) * cw4 * 4]
        if font_name[key] == "MainFont":  # its controller-button icons become Xbox buttons (buttons.py)
            for index, (cw4, ch4, px) in buttons.icons(SCALE).items():
                cx, cy = fontsheet.cell_origin(index)
                for y in range(ch4):
                    d = ((cy * SCALE + y) * W4 + cx * SCALE) * 4
                    big4[d:d + cw4 * 4] = px[y * cw4 * 4:(y + 1) * cw4 * 4]
        sheets[key] = (W4, img["h"] * SCALE, big4)
    print(f"build_pack.py: {len(sheets)} font sheet(s), text font: {Path(args.font).name if args.font != 'none' else 'original'}")

    glyphs = {}
    for hash_, (key, x0, y0, tw, th) in cells.items():
        img = images[key]
        if key in sheets:
            W4, H4, big4 = sheets[key]
            cut = bytearray(tw * SCALE * th * SCALE * 4)
            for y in range(th * SCALE):
                sy = y0 * SCALE + y
                if 0 <= sy < H4:
                    o = (sy * W4 + x0 * SCALE) * 4
                    row = big4[o:o + min(tw * SCALE, W4 - x0 * SCALE) * 4]
                    cut[y * tw * SCALE * 4:y * tw * SCALE * 4 + len(row)] = row
            glyphs[hash_] = (tw * SCALE, th * SCALE, cut)
            continue
        if img["fmt"] in (3, 5, 6, 8, 9):
            # Greyscale atlas cells are font glyphs: smooth-scaled (pngio.upscale_glyph), not AI-upscaled, which
            # thinned the strokes and pushed their edges past the cell, where the game clips them.
            cell = bytearray(tw * th * 4)
            for y in range(th):
                for x in range(tw):
                    ix, iy = x0 + x, y0 + y
                    if ix < img["w"] and iy < img["h"]:
                        o = (iy * img["w"] + ix) * 4
                        cell[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = img["rgba"][o:o + 4]
            glyphs[hash_] = upscale_glyph(tw, th, cell, SCALE)
            continue
        pw, ph = tw + 2 * PAD, th + 2 * PAD
        cell = bytearray(pw * ph * 4)
        for y in range(ph):
            for x in range(pw):
                # Texels outside the cell (or the image) get the nearest texel's colour with alpha 0: no dark halo.
                ix = min(max(x0 + x - PAD, x0), x0 + tw - 1, img["w"] - 1)
                iy = min(max(y0 + y - PAD, y0), y0 + th - 1, img["h"] - 1)
                o = (iy * img["w"] + ix) * 4
                c = img["rgba"][o:o + 4]
                inside = PAD <= x < PAD + tw and PAD <= y < PAD + th and x0 + x - PAD < img["w"] and y0 + y - PAD < img["h"]
                cell[(y * pw + x) * 4:(y * pw + x) * 4 + 4] = c if inside else bytes(c[:3]) + b"\x00"
        write_png(src / f"cell_{hash_}.png", pw, ph, cell)
    r = subprocess.run([str(esrgan), "-i", str(src), "-o", str(big), "-n", args.model, "-s", str(SCALE), "-f", "png"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        sys.exit(f"realesrgan failed:\n{r.stderr[-2000:]}")
    print(f"build_pack.py: upscaled {len(whole)} whole images and {len(cells) - len(glyphs)} cells; smoothed {len(glyphs)} glyphs")

    # 4. Cut out each texture.
    # Built in a side folder and swapped in at the end, so a game that is running keeps a complete pack.
    final, out = out, out.with_name(out.name + ".new")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    upscaled = {key: read_png(big / f"img_{key}.png") for key in whole}
    scale_of = {key: SCALE for key in whole}
    for key in clouds:
        upscaled[key] = upscale_bicubic(images[key]["w"], images[key]["h"], images[key]["rgba"], SCALE)
        scale_of[key] = SCALE
    # Portraits (40x40 full colour) at 8x: each texel is first doubled (nearest), then upscaled 4x. Fed single
    # texels, the upscaler repaints faces (a one-texel blue iris came out white); as 2x2 blocks they survive, and the
    # result is sharp enough for 1440p and up.
    portraits = [key for key in whole if (images[key]["w"], images[key]["h"]) == (40, 40) and images[key]["fmt"] in (1, 2, 4, 7)]
    if portraits:
        psrc, pbig = work / "upscale/portrait_in", work / "upscale/portrait_out"
        psrc.mkdir(parents=True)
        pbig.mkdir(parents=True)
        for key in portraits:
            img = images[key]
            w2, h2 = img["w"] * 2, img["h"] * 2
            doubled = bytearray(w2 * h2 * 4)
            for y in range(h2):
                for x in range(w2):
                    o = ((y // 2) * img["w"] + x // 2) * 4
                    doubled[(y * w2 + x) * 4:(y * w2 + x) * 4 + 4] = img["rgba"][o:o + 4]
            write_png(psrc / f"{key}.png", w2, h2, doubled)
        r = subprocess.run([str(esrgan), "-i", str(psrc), "-o", str(pbig), "-n", args.model, "-s", str(SCALE), "-f", "png"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            sys.exit(f"realesrgan failed on portraits:\n{r.stderr[-2000:]}")
        for key in portraits:
            upscaled[key] = read_png(pbig / f"{key}.png")
            scale_of[key] = SCALE * 2
    print(f"build_pack.py: {len(portraits)} portrait(s) at {SCALE * 2}x")
    # Words drawn as pictures (labels.json): redrawn with the text font, so they match the rest of the text.
    redrawn = 0
    for key in whole:
        entry = LABELS.get(key, {})
        if "text" in entry and args.font != "none":
            upscaled[key] = wordlabels.render_label(images[key], entry["text"], args.font, SCALE, entry.get("dot"))
            scale_of[key] = SCALE
            redrawn += 1
    # Small full-colour pictures (item and slot icons): the upscaler shifts their colours (the short sword's dark blue
    # blade came out lavender), so colours are pulled back to a plain bicubic upscale at 2 output pixels and above.
    icons = [key for key in whole if key not in portraits and "text" not in LABELS.get(key, {})
             and images[key]["fmt"] in (1, 2, 4, 7) and images[key]["w"] * images[key]["h"] <= 64 * 64]
    for key in icons:
        W, H, ai = upscaled[key]
        _, _, ref = upscale_bicubic(images[key]["w"], images[key]["h"], images[key]["rgba"], SCALE)
        upscaled[key] = (W, H, restore_colours(ai, ref, W, H, 2.0))
    for key in whole:  # painted-in button prompts, redrawn as Xbox buttons
        for x0, y0, bw, bh, button in LABELS.get(key, {}).get("buttons", []):
            W, H, rgba = upscaled[key]
            rgba = bytearray(rgba)
            icon = buttons.icon_for(button, bw, bh, SCALE)
            for y in range(bh * SCALE):
                for x in range(bw * SCALE):
                    o = (y * bw * SCALE + x) * 4
                    a = icon[o + 3]
                    if a:
                        d = ((y0 * SCALE + y) * W + x0 * SCALE + x) * 4
                        for c in range(3):
                            rgba[d + c] = (rgba[d + c] * (255 - a) + icon[o + c] * a) // 255
            upscaled[key] = (W, H, rgba)
    for key in whole:  # small text painted into a picture (the title card's TM), redrawn with the text font
        if "marks" in LABELS.get(key, {}) and args.font != "none":
            W, H, rgba = upscaled[key]
            for x0, y0, bw, bh, text in LABELS[key]["marks"]:
                colour = repaint.mark_colour(images[key], (x0, y0, bw, bh))
                rgba = repaint.draw_mark(W, H, rgba, (x0, y0, bw, bh), text, colour, args.font, scale_of[key])
            upscaled[key] = (W, H, rgba)
    for key, numbers in painted.items():
        W, H, rgba = upscaled[key]
        upscaled[key] = (W, H, repaint.draw(W, H, rgba, numbers, args.font, SCALE))
    print(f"build_pack.py: {redrawn} word picture(s) redrawn, {len(icons)} icon(s) colour-corrected, "
          f"{sum(len(n) for n in painted.values())} painted number(s) redrawn")
    tmemhash = PROJECT / "build/cmake/tmemhash"
    if not tmemhash.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    big_entries = bigstrips.cut(out, tmemhash, [(img, upscaled[key], scale_of[key]) for key, (_, img) in big_keys.items()])
    textures = []
    for hash_, (key, x0, y0, tw, th) in sorted(mapping.items()):
        if key in big_keys:
            continue  # drawn as fixed strips now (big_entries)
        scale = SCALE
        if key in upscaled:
            W, H, rgba = upscaled[key]
            scale = scale_of[key]
            ox, oy = x0 * scale, y0 * scale
        elif hash_ in glyphs:
            W, H, rgba = glyphs[hash_]
            ox, oy = 0, 0
        else:
            W, H, rgba = read_png(big / f"cell_{hash_}.png")
            ox, oy = PAD * SCALE, PAD * SCALE
        cw, ch = tw * scale, th * scale
        cut = bytearray(cw * ch * 4)
        for y in range(ch):
            sy = oy + y
            if not 0 <= sy < H:
                continue
            row = rgba[(sy * W + ox) * 4:(sy * W + min(ox + cw, W)) * 4]
            cut[y * cw * 4:y * cw * 4 + len(row)] = row
        write_png(out / f"{hash_}.png", cw, ch, cut)
        # RT64 streams replacements in the background the first time they're needed, so a big page showed its
        # original pixels for its first seconds. Preload what is on screen at rest: whole-width strips of an image,
        # font glyphs and every small image (icons, portraits). Cropped strips, drawn only while a page slides in or
        # out, keep streaming (they're most of the pack's 600+ MiB).
        img = images[key]
        # Blocks of 32x32 texels or more are how a reopened page is drawn (it starts at rest); the thin strips cut
        # while a page slides are the only thing left streaming.
        at_rest = ((x0 == 0 and tw >= img["w"]) or (tw >= 32 and th >= 32) or hash_ in glyphs
                   or img["w"] * img["h"] <= 128 * 128)
        textures.append({"path": f"{hash_}.png", "hashes": {"rt64": hash_},
                         "operation": "preload" if at_rest else "stream"})
    # "defaultShift": "none": RT64's default "half" moves a replacement half a source texel up and left (it suits
    # bilinear-filtered 3D textures); on these flat 2D draws it cut the top and left edge off every letter.
    # Every portrait in the game, from the ROM (portraits.py), including ones never drawn during collection. These
    # replace any captured copies (same hashes).
    rom_entries = big_entries + romportraits.build(PROJECT / "baserom.z64", PROJECT / "decomp", tmemhash, esrgan, args.model, out,
                                     SCALE * 2, SCALE)
    # Every other small image in the ROM (romimages.py): item, skill and spell icons, minimap pictures. Words that
    # labels.json redraws are left to the captured path.
    key_of = image_key
    redrawn_keys = {k for k, v in LABELS.items() if "text" in v or "numbers" in v}
    rom_entries += romimages.build(PROJECT / "baserom.z64", PROJECT / "decomp", tmemhash, esrgan, args.model, out,
                                   SCALE, redrawn_keys, key_of)
    # Every letter of both font sheets, whole and clipped (Font::PrintMain trims a letter's box at a text box's
    # edges: any left/right part at full height, any top/bottom part at full width). Checked against capture: 321 of 321.
    glyph_jobs, glyph_rects = [], []
    new_widths = fontsheet.widths(args.font) if args.font != "none" else {}  # what the game is given (font_widths.h)
    for key, pixels in font_raw.items():
        img = images[key]
        for cell in range(fontsheet.COLS * fontsheet.ROWS):
            cx, cy = fontsheet.cell_origin(cell)
            base, kern = cy * img["w"] + cx, fontsheet.CW
            while kern > 1 and pixels[base + kern - 2] == pixels[base + fontsheet.CW - 1]:
                kern -= 1  # the letter's advance, as Font::SetupBorg8 measures it
            if cell in new_widths and fontsheet.ink_box(img["rgba"], img["w"], cell) is not None:
                kern = new_widths[cell]  # redrawn letters: the width the game is given instead
            rects = {(cx + a, b - a, cy, fontsheet.CH) for a in range(kern) for b in range(a + 1, kern + 1)}
            rects |= {(cx, kern, cy + c, d - c) for c in range(fontsheet.CH) for d in range(c + 1, fontsheet.CH + 1)}
            for x0, tw, y0, th in sorted(rects):
                tmem, line = romimages.tmem_rect(img["fmt"], img["w"], pixels, None, x0, tw, y0, th)
                glyph_jobs.append((tmem, 3, 1, line, tw, th, 0))
                glyph_rects.append((key, x0, tw, y0, th))
    for h_, (key, x0, tw, y0, th) in zip(romportraits.hashes(tmemhash, glyph_jobs), glyph_rects):
        W4, H4, big4 = sheets[key]
        cut = bytearray(tw * SCALE * th * SCALE * 4)
        for y in range(th * SCALE):
            o = ((y0 * SCALE + y) * W4 + x0 * SCALE) * 4
            cut[y * tw * SCALE * 4:(y + 1) * tw * SCALE * 4] = big4[o:o + tw * SCALE * 4]
        write_png(out / f"{h_}.png", tw * SCALE, th * SCALE, cut)
        rom_entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "preload"})
    print(f"build_pack.py: {len(glyph_jobs)} letter textures from the ROM font sheets")
    taken = {e["hashes"]["rt64"] for e in rom_entries}
    textures = [t for t in textures if t["hashes"]["rt64"] not in taken] + rom_entries

    db = {
        "configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream",
                          "defaultShift": "none", "hashVersion": 5},
        "textures": textures, "operationFilters": [], "shiftFilters": [], "extraFiles": [],
    }
    (out / "rt64.json").write_text(json.dumps(db, indent=1) + "\n")
    old = final.with_name(final.name + ".old")
    if old.exists():
        shutil.rmtree(old)
    if final.exists():
        final.rename(old)
    out.rename(final)
    if old.exists():
        shutil.rmtree(old)
    print(f"build_pack.py: {len(textures)} replacements in {final}")


if __name__ == "__main__":
    main()
