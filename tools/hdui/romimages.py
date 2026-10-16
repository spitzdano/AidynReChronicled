"""romimages.py: every small 2D image in the ROM (item, skill and spell icons, minimap pictures...) for the HD UI pack.

Like portraits.py, for images the game draws with N64BorgImageDraw in one load (8-bit images up to 2 KiB with a
palette, 4 KiB without): each image is loaded whole, or cropped while a page slides (left or right part, full height)
or a list scrolls (top or bottom part, full width). For every such load the TMEM contents are rebuilt and hashed with
RT64's own function (portraits.tmem_image / hashes), and the replacement is cut from the image upscaled once.
The time-of-day dial's pieces are redrawn instead (hudart.py).
Full-colour images (icons) are first doubled with a smooth bicubic filter, then upscaled 4x with the anime model:
8x in all, with cleaner shapes and more detail than a direct 4x (compared on the hat, hide, spice, club and sword).
Their cropped variants (only seen while a page slides) are box-filtered to 4x to keep the pack small. Greyscale images
(minimap pictures) are upscaled 4x directly.
"""
import subprocess
import tempfile
from pathlib import Path

import numpy as np

import hudart
import portraits
import romassets
from pngio import decode_borg8, read_png, upscale_bicubic, write_png

BPP = portraits.BPP
FMT = portraits.FMT


def fits(fmt, w, h):
    if BPP[fmt] != 8:
        return False
    limit = 2048 if fmt == 4 else 4096
    line = (w + 7) // 8
    return line * 8 * h <= limit


def variants(w, h):
    """(x0, tw, y0, th): whole, left/right parts at full height, top/bottom parts at full width."""
    out = [(0, w, 0, h)]
    out += [(0, k, 0, h) for k in range(1, w)] + [(x0, w - x0, 0, h) for x0 in range(1, w)]
    out += [(0, w, 0, k) for k in range(1, h)] + [(0, w, y0, h - y0) for y0 in range(1, h)]
    return out


def tmem_rect(fmt, w, pixels, palette, x0, tw, y0, th):
    line = (tw + 7) // 8
    tmem = bytearray(4096)
    for r in range(th):
        swap = 4 if r & 1 else 0
        base = r * line * 8
        src = (y0 + r) * w + x0
        for i in range(tw):
            tmem[base + (i ^ swap)] = pixels[src + i]
    if palette:
        for k in range(len(palette) // 2):
            tmem[0x800 + k * 8:0x800 + k * 8 + 8] = palette[k * 2:k * 2 + 2] * 4
    return tmem, line


def build(rom_path, decomp, tool, esrgan, model, out, scale, skip_keys, key_of, log=print):
    """Writes replacements for every one-load 8-bit image except portraits and `skip_keys` (image keys handled
    elsewhere, key_of(fmt, w, h, pixels, palette) -> key). Returns rt64.json texture entries."""
    rom = romassets.Rom(rom_path)
    names = romassets.borg8_names(decomp)
    images = {}
    for index in range(rom.count):
        if rom.entry(index)[0] != romassets.IMAGE or names.get(index, "").startswith("Portrait"):
            continue
        fmt, w, h, pixels, palette = img = rom.image(index)
        if w * h <= 4 or not fits(fmt, w, h) or key_of(*img) in skip_keys:
            continue
        images[index] = img
    entries = []
    with tempfile.TemporaryDirectory() as t:
        src, big = Path(t) / "in", Path(t) / "out"
        src.mkdir()
        big.mkdir()
        for index, (fmt, w, h, pixels, palette) in images.items():
            if names.get(index) in hudart.NAMES:
                continue  # redrawn by hudart.py, not upscaled
            rgba = decode_borg8(fmt, w, h, pixels, palette)
            if fmt in (4, 7):
                w, h, rgba = upscale_bicubic(w, h, rgba, 2)
            write_png(src / f"{index}.png", w, h, rgba)
        r = subprocess.run([str(esrgan), "-i", str(src), "-o", str(big), "-n", model, "-s", str(scale), "-f", "png"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"realesrgan failed:\n{r.stderr[-2000:]}")
        jobs, owners = [], []
        for index, (fmt, w, h, pixels, palette) in images.items():
            rdp_fmt, siz = FMT[fmt]
            tlut = portraits.G_TT_RGBA16 if palette else 0
            for x0, tw, y0, th in variants(w, h):
                tmem, line = tmem_rect(fmt, w, pixels, palette, x0, tw, y0, th)
                jobs.append((tmem, rdp_fmt, siz, line, tw, th, tlut))
                owners.append((index, x0, tw, y0, th))
        hs = portraits.hashes(tool, jobs)
        ups = {}
        for index, (fmt, w, h, pixels, palette) in images.items():
            if names.get(index) in hudart.NAMES:
                W, H, up = hudart.redraw(names[index], w, h, decode_borg8(fmt, w, h, pixels, palette))
            else:
                W, H, up = read_png(big / f"{index}.png")
            big_img = np.frombuffer(bytes(up), np.uint8).reshape(H, W, 4)
            per = W // w
            small = big_img
            if per != scale:  # 8x icons: a 4x copy for the cropped variants
                f = per // scale
                small = big_img.reshape(h * scale, f, w * scale, f, 4).mean(axis=(1, 3)).round().astype(np.uint8)
            ups[index] = (per, big_img, small)
        for h_, (index, x0, tw, y0, th) in zip(hs, owners):
            whole = (x0, y0) == (0, 0) and (tw, th) == images[index][1:3]
            per, big_img, small = ups[index]
            img, s_ = (big_img, per) if whole else (small, scale)
            cut = np.ascontiguousarray(img[y0 * s_:(y0 + th) * s_, x0 * s_:(x0 + tw) * s_])
            write_png(Path(out) / f"{h_}.png", tw * s_, th * s_, cut.tobytes())
            entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "preload" if whole else "stream"})
    log(f"romimages.py: {len(images)} images from the ROM ({sum(1 for i in images if names.get(i, '').startswith('Icon'))} "
        f"icons), {len(entries)} textures")
    return entries
