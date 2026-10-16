"""romart.py: pictures for the manual, read from the player's ROM and upscaled (used by build_manual.py).

Each picture is a Borg8 image named as in the decomp's include/Borgindecies.h, decoded with tools/hdui's readers
and upscaled 4x with Real-ESRGAN (the toolkit's opt/realesrgan, as for the HD UI pack). Small pictures (portraits,
icons) are doubled first, so one-texel details survive (8x in all). Results are cached in build/manual/art, keyed by
the ROM's SHA-1 and this file's PIPELINE, so a rebuild only upscales what changed. Everything here is derived from
the ROM: it stays in build/ and the player's data folder, never in the repository.
"""
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "hdui"))
import romassets  # noqa: E402
from pngio import bleed, decode_borg8, read_png, write_png  # noqa: E402

PIPELINE = "1"  # bump when the upscaling changes
MODEL = "realesrgan-x4plus-anime"


def names(decomp):
    """{name: index} for every BORG8_* define (any spacing; romassets' stricter pattern misses a few)."""
    out = {}
    for line in (Path(decomp) / "include/Borgindecies.h").read_text().splitlines():
        m = re.match(r"#define\s+BORG8_(\w+)\s+(0x[0-9a-fA-F]+)", line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def _double(w, h, rgba, k):
    out = bytearray(w * k * h * k * 4)
    for y in range(h * k):
        row = (y // k) * w
        for x in range(w * k):
            o = (row + x // k) * 4
            out[(y * w * k + x) * 4:(y * w * k + x) * 4 + 4] = rgba[o:o + 4]
    return out


def build(rom_path, decomp, esrgan, wanted, cache, log=print):
    """Upscales each name in `wanted` (cached) and returns {name: png path}."""
    rom_bytes = Path(rom_path).read_bytes()
    stamp = hashlib.sha1(rom_bytes).hexdigest()[:12] + "-" + PIPELINE
    cache = Path(cache) / stamp
    cache.mkdir(parents=True, exist_ok=True)
    index = names(decomp)
    missing = [n for n in wanted if n not in index]
    if missing:
        raise SystemExit(f"romart.py: no such Borg8 image: {', '.join(missing)}")
    todo = [n for n in wanted if not (cache / f"{n}.png").exists()]
    if todo:
        rom = romassets.Rom(rom_path)
        with tempfile.TemporaryDirectory() as t:
            src, big = Path(t) / "in", Path(t) / "out"
            src.mkdir()
            big.mkdir()
            for n in todo:
                fmt, w, h, pixels, palette = rom.image(index[n])
                rgba = bleed(w, h, decode_borg8(fmt, w, h, pixels, palette))
                k = 2 if max(w, h) <= 64 else 1
                write_png(src / f"{n}.png", w * k, h * k, _double(w, h, rgba, k) if k > 1 else rgba)
            log(f"romart.py: upscaling {len(todo)} pictures")
            r = subprocess.run([str(esrgan), "-i", str(src), "-o", str(big), "-n", MODEL, "-s", "4", "-f", "png"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if r.returncode != 0:
                raise SystemExit(f"romart.py: realesrgan failed:\n{r.stderr[-2000:]}")
            for n in todo:  # realesrgan-ncnn upscales the alpha channel too
                shutil.copyfile(big / f"{n}.png", cache / f"{n}.png")
    out = {}
    for n in wanted:
        p = cache / f"{n}.png"
        read_png(p)  # fail here, not in a browser, if a file is broken
        out[n] = p
    return out
