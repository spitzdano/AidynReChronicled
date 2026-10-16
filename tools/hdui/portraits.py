"""portraits.py: every portrait in the game (characters, NPCs, enemies), straight from the ROM, for the HD UI pack.

A portrait (BORG8_Portrait*, 40x40 CI8 with an RGBA16 palette, a few others) is drawn by N64BorgImageDraw with one
LoadTile of the whole image, or of a left or right part of it while a page slides in or out. Each of those loads is
its own RT64 texture, keyed by RT64's hash of the texture memory (TMEM) contents. This module builds those TMEM
contents the way the RDP's LoadTile/LoadTLUT do (odd rows have their 32-bit words swapped; every palette entry is
stored four times in the upper half), hashes them with build/tools/tmemhash (RT64's own hash function), and cuts the
matching part out of the upscaled portrait. verify() checks the whole chain against textures the game really loaded.
"""
import subprocess
import tempfile
from pathlib import Path

import romassets
import redone
from pngio import decode_borg8, read_png, write_png

BPP = {1: 32, 2: 16, 3: 16, 4: 8, 5: 8, 6: 8, 7: 4, 8: 4, 9: 4}
FMT = {1: (0, 3), 2: (0, 2), 3: (3, 2), 4: (2, 1), 5: (3, 1), 6: (4, 1), 7: (2, 0), 8: (3, 0), 9: (4, 0)}  # (fmt, siz)
G_TT_RGBA16 = 0x8000


def variants(w):
    """(x0, width) of every horizontal part the game loads: the whole image, its left parts and its right parts."""
    out = [(0, w)]
    out += [(0, k) for k in range(1, w)]
    out += [(x0, w - x0) for x0 in range(1, w)]
    return out


def tmem_image(fmt, w, h, pixels, palette, x0, tw):
    """4096 TMEM bytes after loading columns x0..x0+tw-1 of an 8-bit image (and its palette), and the tile's line."""
    if BPP[fmt] != 8:
        raise ValueError("only 8-bit images are supported")
    line = (tw + 7) // 8
    tmem = bytearray(4096)
    for r in range(h):
        swap = 4 if r & 1 else 0
        base = r * line * 8
        for i in range(tw):
            tmem[base + (i ^ swap)] = pixels[r * w + x0 + i]
    if palette:
        for k in range(len(palette) // 2):
            entry = palette[k * 2:k * 2 + 2]
            tmem[0x800 + k * 8:0x800 + k * 8 + 8] = entry * 4
    return tmem, line


def hashes(tool, jobs):
    """jobs: [(tmem bytes, fmt, siz, line, width, height, tlut)] -> [hash hex]"""
    with tempfile.TemporaryDirectory() as t:
        lines = []
        for n, (tmem, fmt, siz, line, width, height, tlut) in enumerate(jobs):
            p = Path(t) / f"{n}.tmem"
            p.write_bytes(bytes(tmem))
            lines.append(f"{p} {fmt} {siz} {line} 0 0 {width} {height} {tlut}")
        r = subprocess.run([str(tool)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
        return r.stdout.split()


def jobs_for(img):
    fmt, w, h, pixels, palette = img
    rdp_fmt, siz = FMT[fmt]
    tlut = G_TT_RGBA16 if palette else 0
    out = []
    for x0, tw in variants(w):
        tmem, line = tmem_image(fmt, w, h, pixels, palette, x0, tw)
        out.append(((x0, tw), (tmem, rdp_fmt, siz, line, tw, h, tlut)))
    return out


def verify(rom, index, tool, dumps):
    """How many of the portrait's variants whose hash matches a texture the game dumped (dumps: RT64 dump folder)."""
    img = rom.image(index)
    js = jobs_for(img)
    hs = hashes(tool, [j for _, j in js])
    return sum((Path(dumps) / f"{h}.v5.tmem").exists() for h in hs), len(hs)


def build(rom_path, decomp, tool, esrgan, model, out, scale, crop_scale, art=None, only_art=False, crop_scale_of=None,
          log=print):
    """Upscales every portrait and writes one replacement per variant into `out`. Returns rt64.json texture entries.
    art: {name: path} of redrawn portraits (redone.collect()), used instead of the upscale; only_art: only those.
    crop_scale_of(name): the scale of that portrait's cropped variants, if not crop_scale."""
    rom = romassets.Rom(rom_path)
    names = romassets.borg8_names(decomp)
    art = art or {}
    portraits = {i: n for i, n in names.items() if n.startswith("Portrait") and (n in art or not only_art)}
    entries = []
    with tempfile.TemporaryDirectory() as t:
        src, big = Path(t) / "in", Path(t) / "out"
        src.mkdir()
        big.mkdir()
        images = {}
        for index, name in portraits.items():
            img = rom.image(index)
            fmt, w, h, pixels, palette = img
            if BPP[fmt] != 8:
                log(f"portraits.py: {name}: {BPP[fmt]}-bit image skipped")
                continue
            images[index] = img
            if name in art:
                continue  # redrawn: nothing to upscale
            rgba = decode_borg8(fmt, w, h, pixels, palette)
            # Faces: each texel doubled first, so the upscaler keeps one-texel features (eyes) instead of repainting.
            faces = fmt in (4, 7)
            k = 2 if faces else 1
            doubled = bytearray(w * k * h * k * 4)
            for y in range(h * k):
                for x in range(w * k):
                    o = ((y // k) * w + x // k) * 4
                    doubled[(y * w * k + x) * 4:(y * w * k + x) * 4 + 4] = rgba[o:o + 4]
            write_png(src / f"{index}.png", w * k, h * k, doubled)
        if any(src.iterdir()):
            r = subprocess.run([str(esrgan), "-i", str(src), "-o", str(big), "-n", model, "-s", "4", "-f", "png"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if r.returncode != 0:
                raise RuntimeError(f"realesrgan failed:\n{r.stderr[-2000:]}")
        for index, img in sorted(images.items()):
            fmt, w, h, _, _ = img
            if portraits[index] in art:
                UW, UH, up = redone.load(art[portraits[index]], w, h)
            else:
                UW, UH, up = read_png(big / f"{index}.png")
            per = UW // w  # upscaled pixels per texel (8 for faces, 4 otherwise)
            levels = {}

            def level(s):
                """The upscaled portrait box-filtered to s pixels per texel (computed once per scale)."""
                if s not in levels:
                    step = per // s
                    LW, LH = w * s, h * s
                    px = bytearray(LW * LH * 4)
                    for y in range(LH):
                        for x in range(LW):
                            acc = [0, 0, 0, 0]
                            for yy in range(step):
                                o = ((y * step + yy) * UW + x * step) * 4
                                for xx in range(step):
                                    for c in range(4):
                                        acc[c] += up[o + xx * 4 + c]
                            px[(y * LW + x) * 4:(y * LW + x) * 4 + 4] = bytes(v // (step * step) for v in acc)
                    levels[s] = (LW, px)
                return levels[s]

            js = jobs_for(img)
            hs = hashes(tool, [j for _, j in js])
            for ((x0, tw), _), h_ in zip(js, hs):
                whole = (x0, tw) == (0, w)
                s = min(per, scale if whole else (crop_scale_of(portraits[index]) if crop_scale_of else crop_scale))
                LW, px = level(s)
                cw, ch = tw * s, h * s
                cut = bytearray(cw * ch * 4)
                for y in range(ch):
                    cut[y * cw * 4:(y + 1) * cw * 4] = px[(y * LW + x0 * s) * 4:(y * LW + x0 * s + cw) * 4]
                write_png(Path(out) / f"{h_}.png", cw, ch, cut)
                entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_},
                                "operation": "preload" if whole else "stream"})
        log(f"portraits.py: {len(images)} portraits from the ROM ({sum(portraits[i] in art for i in images)} redrawn), "
            f"{len(entries)} textures")
    return entries
