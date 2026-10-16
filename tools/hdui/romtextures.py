"""romtextures.py: every world texture (Borg1 asset) in the ROM, with the RT64 hash of each of its mip levels.

A Borg1 asset decompresses to Borg1Data {u16 type, u16 flag, u8 width, u8 height, u8 lods, u8 move,
u32 dList, u32 bmp, u32 palette, u32 unused} (offsets from its start; decomp include/borg/borg1.h). The game loads it
(decomp n64Borg/anim.cpp borganim_LoadTextureImage + loadTextureImage) with one LoadBlock of all the mip levels,
stored one after the other, to TMEM 0 (dxt 0: the data is stored pre-interleaved, so it's copied as is), the palette
with LoadTLUT to TMEM 0x800, and one tile per level: line = the level's width rounded up to 64 bits, tmem = the sum of
the previous levels' line x height, width and height halved each level (half(): to the nearest even number).
Textures flagged Moving (water, fog; 16-bit, no mip levels) scroll sideways at runtime (moveBitmap16): every row of
the image is rotated by a few texels per step. Each scroll position is its own RT64 texture, so one TMEM image is
made per position ("frames": rotation in texels -> TMEM); the odd rows' 32-bit word swap is undone around the
rotation. 32-bit moving textures (3 in the game) are left out.
"""
import struct

import romassets

TEXTURE = 1
# BORG1type -> (RDP fmt, siz, bits per texel)
TYPES = {0: (0, 2, 16), 1: (3, 2, 16), 2: (2, 1, 8), 3: (3, 1, 8), 4: (4, 1, 8), 5: (2, 0, 4), 6: (3, 0, 4),
         7: (4, 0, 4), 8: (0, 3, 32)}
MOVING, PROCEDURAL = 0x200, 0x100
G_TT_RGBA16 = 0x8000


def half(x):
    r = x >> 1
    if r != 1 and r & 1:
        r -= 1
    return r


def levels(t, w, h, count):
    """[(tmem words, line, width, height)] for mip levels 0..count (the last may go unused)."""
    fmt, siz, bits = TYPES[t]
    texels_per_word = 64 // bits
    out, tmem = [], 0
    for _ in range(count + 1):
        if w == 0 or h == 0:
            break
        if bits == 32:
            line = (w + w % 2) >> 2  # RGBA32 is split over both TMEM halves: 16 bits per texel per half
        else:
            line = (w + texels_per_word - 1) // texels_per_word
        out.append((tmem, line, w, h))
        tmem += line * h
        w, h = half(w), half(h)
    return out


def textures(rom):
    """Yields (index, info) for every static Borg1 texture: info has type, flag, w, h, lods, tmem (bytearray 4096),
    tiles [(hash input: fmt, siz, line, tmem, width, height, tlut)]."""
    for index in range(rom.count):
        if rom.entry(index)[0] != TEXTURE:
            continue
        d = rom.data(index)
        t, flag, w, h, lods, move, dlist, bmp, pal, _ = struct.unpack(">HHBBBBIIII", d[:24])
        if t not in TYPES or w == 0 or h == 0:
            continue
        moving = bool(flag & MOVING)
        if moving and t not in (0, 1):
            continue
        fmt, siz, bits = TYPES[t]
        tmem = bytearray(4096)
        end = pal if pal and pal > bmp else len(d)
        data = d[bmp:end]
        limit = 2048 if t in (2, 5) else (2048 if bits == 32 else 4096)
        tmem[:min(len(data), limit)] = data[:limit]
        tlut = 0
        if t in (2, 5) and pal:
            entries = 256 if t == 2 else 16
            p = d[pal:pal + entries * 2]
            for k in range(len(p) // 2):
                tmem[0x800 + k * 8:0x800 + k * 8 + 8] = p[k * 2:k * 2 + 2] * 4
            tlut = G_TT_RGBA16
        tiles = [(fmt, siz, line, tm, lw, lh, tlut) for tm, line, lw, lh in levels(t, w, h, max(lods, 0))]
        frames = [(0, tmem)]
        if moving:
            frames = [(r, scrolled(tmem, w, h, r)) for r in range(w)]
        yield index, {"type": t, "flag": flag, "w": w, "h": h, "lods": lods, "tmem": tmem, "tiles": tiles,
                      "frames": frames, "moving": moving}


def scrolled(tmem, w, h, r):
    """TMEM of a 16-bit texture with every row rotated r texels (row-major, odd rows' texel j stored at j ^ 2)."""
    out = bytearray(tmem)
    for y in range(h):
        swap = 2 if y & 1 else 0
        row = [tmem[(y * w + (j ^ swap)) * 2:(y * w + (j ^ swap)) * 2 + 2] for j in range(w)]
        for j in range(w):
            src = row[(j - r) % w]
            o = (y * w + (j ^ swap)) * 2
            out[o:o + 2] = src
    return out
