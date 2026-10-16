"""bigstrips.py: the fixed strips of every big 2D image in the ROM, for the HD UI pack (used by build_pack.py).

An image that doesn't fit TMEM in one load (page backgrounds, title cards, the Controller Pak screen, map pieces,
clouds; not the two font sheets) is always drawn the same way in the port: src/game/patches.cpp ("Big pictures")
widens every draw of it to the full width and to the strip grid, then clips the rectangles back. So each such image
has one fixed set of strips: rows [k*step, k*step + step + 1) (each strip loads one row more than it draws, for
filtering) while that stays above the last strip, which is the rows left at the bottom. step is
N64BorgImageDraw's own rule (strip_step, same as the C++).

For each strip the TMEM contents are rebuilt the way RT64 loads them (RDP::loadTileOperation / loadToTMEMCommon:
LoadTile from column 0 at TMEM 0, `line` 64-bit words per row, odd rows' 32-bit words swapped, RGBA32 split into
red-green in the low half and blue-alpha in the high half, a CI palette's entries x4 at 0x800), hashed with RT64's
TMEMHasher (build/cmake/tmemhash), and cut from the image upscaled whole.
"""
from pathlib import Path

import numpy as np

import portraits
import romassets
from pngio import write_png

BPP = portraits.BPP
FMT = portraits.FMT  # Borg8 format -> (RDP fmt, siz)
FONT_SHEET = (36, 414)


def capacity(fmt):
    return 0x800 if fmt in (4, 7) else 0x1000


def is_big(fmt, w, h):
    return (w, h) != FONT_SHEET and w * h * BPP[fmt] // 8 > capacity(fmt)


def strip_step(fmt, w):
    def pad(n, m):
        return (n + m - 1) // m * m
    if fmt == 1:
        return 0x1000 // (pad(w, 2) * 4) - 1
    if fmt in (2, 3):
        return 0x1000 // (pad(w, 4) * 2) - 1
    if fmt == 4:
        return 0x800 // pad(w, 8) - 1
    if fmt in (5, 6):
        return 0x1000 // pad(w, 8) - 1
    if fmt == 7:
        return 0x800 // (pad(w, 16) // 2) - 1
    return 0x1000 // (pad(w, 16) // 2) - 1


def strips(fmt, w, h):
    """[(y0, rows)] every strip a full-width, grid-aligned draw can load."""
    step = strip_step(fmt, w)
    rest = h % step or step
    last = h - rest
    return [(y, step + 1) for y in range(0, last, step)] + [(last, rest)]


def line_words(fmt, w):
    siz = FMT[fmt][1]
    if siz == 3:  # RGBA32: line counts one half of TMEM (2 bytes a texel)
        return (w * 2 + 7) // 8
    return (w * BPP[fmt] + 63) // 64


def tmem_strip(fmt, w, h, pixels, palette, y0, rows):
    """4096 TMEM bytes after LoadTile of columns 0..w-1, rows y0..y0+rows-1 (RT64's loadToTMEMCommon)."""
    siz = FMT[fmt][1]
    rgba32 = siz == 3
    bytes_per_row = (w << siz) >> 1
    words_per_row = ((w - 1) >> (4 - siz)) + 1
    stride = line_words(fmt, w) * 8
    mask, advance = (2047, 4) if rgba32 else (4095, 8)
    data = bytes(pixels) + bytes(64)  # a word past the image's end reads beyond it (never hashed: it's row padding)
    tmem = bytearray(4096)
    for r in range(rows):
        xor = 4 if r & 1 else 0
        src = (y0 + r) * bytes_per_row
        addr = (r * stride) & mask
        for _ in range(words_per_row):
            word = data[src:src + 8]
            if rgba32:
                for i, b in enumerate((0, 1, 4, 5)):
                    tmem[(addr + i) ^ xor] = word[b]
                for i, b in enumerate((2, 3, 6, 7)):
                    tmem[((addr + i) ^ xor) | 0x800] = word[b]
            else:
                for i in range(8):
                    tmem[(addr + i) ^ xor] = word[i]
            src += 8
            addr = (addr + advance) & mask
    if palette:
        for k in range(len(palette) // 2):
            tmem[0x800 + k * 8:0x800 + k * 8 + 8] = palette[k * 2:k * 2 + 2] * 4
    return tmem


def jobs(img):
    """[(y0, rows, hash job)] for one big image (fmt, w, h, pixels, palette)."""
    fmt, w, h, pixels, palette = img
    rdp_fmt, siz = FMT[fmt]
    tlut = portraits.G_TT_RGBA16 if palette else 0
    out = []
    for y0, rows in strips(fmt, w, h):
        tmem = tmem_strip(fmt, w, h, pixels, palette, y0, rows)
        out.append((y0, rows, (tmem, rdp_fmt, siz, line_words(fmt, w), w, rows, tlut)))
    return out


def big_images(rom_path, decomp):
    """{ROM index: (name, (fmt, w, h, pixels, palette))} for every big image."""
    rom = romassets.Rom(rom_path)
    names = romassets.borg8_names(decomp)
    out = {}
    for index in range(rom.count):
        if rom.entry(index)[0] != romassets.IMAGE:
            continue
        img = rom.image(index)
        if is_big(*img[:3]):
            out[index] = (names.get(index, f"image{index}"), img)
    return out


def cut(out, tool, items, log=print):
    """items: [(img, (W, H, rgba upscaled whole), scale)]. Writes <hash>.png per strip; returns rt64.json entries."""
    all_jobs, owners = [], []
    for n, (img, _, _) in enumerate(items):
        for y0, rows, job in jobs(img):
            all_jobs.append(job)
            owners.append((n, y0, rows))
    hashes = portraits.hashes(tool, all_jobs)
    entries, seen = [], set()
    for h_, (n, y0, rows) in zip(hashes, owners):
        if h_ in seen:
            continue
        seen.add(h_)
        img, (W, H, rgba), scale = items[n]
        big = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4)
        part = np.ascontiguousarray(big[y0 * scale:min((y0 + rows) * scale, H)])
        if part.shape[0] < rows * scale:  # never: strips end at the image's bottom
            part = np.concatenate([part, np.zeros((rows * scale - part.shape[0], W, 4), np.uint8)])
        write_png(Path(out) / f"{h_}.png", W, rows * scale, part.tobytes())
        entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "preload"})
    log(f"bigstrips.py: {len(items)} big images, {len(entries)} strips")
    return entries
