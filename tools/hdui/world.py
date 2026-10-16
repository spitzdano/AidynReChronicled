#!/usr/bin/env python3
"""world.py: build the "hd-world" texture pack (terrain, buildings, characters, monsters, water) for the whole game.

Sources:
  1. Every texture in the ROM (romtextures.py): all 2,000-odd static world textures with each of their mip levels,
     and every scroll position of the scrolling ones (water, fog), keyed by the RT64 hash the game will produce
     (rebuilt TMEM, hashed by build/cmake/tmemhash). This needs no playing: every area is covered.
  2. Anything else seen while collecting (texture dumps in build/hdui/dumps that are neither 2D art nor in the ROM
     set: particles, effects). Their mip levels are found as dumps with byte-identical TMEM whose pixels are a
     box-filtered copy of the largest (is_level). (Grouping by RDRAM address mixed unrelated textures: the game
     reuses texture memory.)

Each image is padded with a margin of itself as it repeats in game (wrap, mirror or clamp per axis; from the dump
when the texture was seen, wrap otherwise), doubled with a bicubic filter, upscaled 4x with Real-ESRGAN's general
model (8x in all; the anime model used for menus flattens stone and grass), and cropped back, so it tiles without
seams. An unsharp mask then crisps grass blades, pebbles and cracks. Fine detail the model smoothed away is put
back pixel by pixel from a bicubic upscale (keep_grain: where the AI result has less detail than the bicubic, the
bicubic's is restored in proportion; sharpened edges, bark and brick keep the AI result). Smaller mip levels are the
upscaled image box-filtered down; scroll positions are the upscaled image rotated. Textures with 1-bit alpha (RGBA16
cut-outs: leaves, fences) get hard alpha edges back.

Replacements stream in when first used (RT64 loads them in the background), so startup stays quick.
Output: ~/.local/share/AidynReChronicled/texture-packs/hd-world (settings.json texture_packs, after hd-ui).
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_pack import PROJECT, toolkit  # noqa: E402
from pngio import read_png, write_png  # noqa: E402
import romassets  # noqa: E402
import romtextures  # noqa: E402

SCALE = 8       # output texels per original texel: bicubic 2x, then Real-ESRGAN 4x
AI_SCALE = 4


def load_decoder():
    """rt64_dumps_to_png.decode from the toolkit: decodes TMEM exactly as RT64's shader samples it."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("rt64png", toolkit() / "scripts/rt64_dumps_to_png.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.decode


def arr(w, h, rgba):
    return np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4)


def pad_tile(img, cms, cmt):
    """3x3 copies of an H x W x 4 image, each axis repeated as the RDP would (bit 0 mirror, bit 1 clamp)."""
    def axis(a, n, mode, ax):
        if mode & 2:
            return np.pad(a, [(n, n) if i == ax else (0, 0) for i in range(3)], mode="edge")
        if mode & 1:
            flipped = np.flip(a, axis=ax)
            return np.concatenate([flipped, a, flipped], axis=ax)
        return np.concatenate([a, a, a], axis=ax)
    h, w = img.shape[:2]
    return axis(axis(img, w, cms, 1), h, cmt, 0)


def _gauss(img, sigma):
    """Separable Gaussian blur of an H x W x C float array, edges repeated."""
    r = max(1, int(sigma * 3 + 0.5))
    k = np.exp(-np.arange(-r, r + 1) ** 2 / (2 * sigma * sigma))
    k /= k.sum()
    p = np.pad(img, ((r, r), (0, 0), (0, 0)), mode="edge")
    img = sum(k[i] * p[i:i + img.shape[0]] for i in range(2 * r + 1))
    p = np.pad(img, ((0, 0), (r, r), (0, 0)), mode="edge")
    return sum(k[i] * p[:, i:i + img.shape[1]] for i in range(2 * r + 1))


def sharpen(img, amount=0.8, sigma=1.5):
    """Unsharp mask on the colour channels (sigma in output pixels): crisper blades, pebbles and cracks."""
    f = img[..., :3].astype(np.float32)
    out = img.copy()
    out[..., :3] = np.clip(f + amount * (f - _gauss(f, sigma)) + 0.5, 0, 255).astype(np.uint8)
    return out


def keep_grain(ai, ref, sigma):
    """ai, ref: H x W x 4 uint8 (AI and bicubic upscales of the same padded texture). Per pixel: where the AI result
    has less fine detail (what a blur of `sigma` removes) than the bicubic upscale, the bicubic's detail is put back,
    in proportion to what was lost; where the AI has as much or more (sharpened edges), it is kept."""
    a = ai[..., :3].astype(np.float32)
    b = ref[..., :3].astype(np.float32)
    ha, hb = a - _gauss(a, sigma), b - _gauss(b, sigma)
    la = _gauss((ha ** 2).sum(axis=2, keepdims=True), sigma * 1.5)
    lb = _gauss((hb ** 2).sum(axis=2, keepdims=True), sigma * 1.5)
    m = np.clip((lb - la) / (lb + 1.0), 0.0, 1.0)
    out = ai.copy()
    out[..., :3] = np.clip(a + m * (hb - ha) + 0.5, 0, 255).astype(np.uint8)
    return out


def box_down(img, w, h):
    """Box-filter an H x W x 4 image down to h x w (integer ratios)."""
    H, W = img.shape[:2]
    return img.reshape(h, H // h, w, W // w, 4).mean(axis=(1, 3)).round().astype(np.uint8)


def is_level(big, small):
    """True when `small` looks like `big` box-filtered down: a mip level of it."""
    h, w = small.shape[:2]
    return np.abs(box_down(big, w, h)[..., :3].astype(int) - small[..., :3].astype(int)).mean() < 8


def hashes(tool, jobs, tmp):
    """jobs: [(tmem bytes, fmt, siz, line, tmem words, width, height, tlut)] -> [hash hex]"""
    lines, files = [], {}
    for tm, fmt, siz, line, words, w, h, tlut in jobs:
        key = id(tm)
        if key not in files:
            files[key] = Path(tmp) / f"t{len(files)}.tmem"
            files[key].write_bytes(bytes(tm))
        lines.append(f"{files[key]} {fmt} {siz} {line} {words} 0 {w} {h} {tlut}")
    r = subprocess.run([str(tool)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    return r.stdout.split()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--work", default=str(PROJECT / "build/hdui"))
    ap.add_argument("--out", default=str(Path.home() / ".local/share/AidynReChronicled/texture-packs/hd-world"))
    ap.add_argument("--model", default="realesrgan-x4plus")
    args = ap.parse_args()
    work, out = Path(args.work), Path(args.out)
    esrgan = toolkit() / "opt/realesrgan/realesrgan-ncnn-vulkan"
    tool = PROJECT / "build/cmake/tmemhash"
    if not tool.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    decode = load_decoder()
    dumps, pngs = work / "dumps", work / "png"

    # Wrap modes and "is 2D art" from what was seen in game.
    seen_modes = {}
    if dumps.exists():
        subprocess.run([sys.executable, "-I", str(toolkit() / "scripts/rt64_dumps_to_png.py"), str(dumps), str(pngs)],
                       check=True, stdout=subprocess.DEVNULL)
        for info in dumps.glob("*.tile.json"):
            t = json.loads(info.read_text())
            seen_modes[info.name.split(".")[0]] = (t["tile"]["cms"], t["tile"]["cmt"])
    mapping = work / "mapping.json"
    ui = set(json.loads(mapping.read_text())) if mapping.exists() else set()

    # sources: name -> {"img": H x W x 4, "modes": (cms, cmt), "alpha1": bool,
    #                   "outputs": [(hash, w, h, rotation)]}
    sources = {}
    with tempfile.TemporaryDirectory() as tmp:
        # 1. The ROM.
        rom = romassets.Rom(PROJECT / "baserom.z64")
        jobs, owners = [], []
        texinfo = {}
        for index, info in romtextures.textures(rom):
            texinfo[index] = info
            for level, (fmt, siz, line, words, w, h, tlut) in enumerate(info["tiles"]):
                frames = info["frames"] if level == 0 else [(0, info["tmem"])]
                for rotation, tm in frames:
                    jobs.append((tm, fmt, siz, line, words, w, h, tlut))
                    owners.append((index, level, w, h, rotation))
        rom_hash = hashes(tool, jobs, tmp)
        for h, (index, level, w, hh, rotation) in zip(rom_hash, owners):
            info = texinfo[index]
            src = sources.get(f"rom{index}")
            if src is None:
                fmt, siz, line, words, w0, h0, tlut = info["tiles"][0]
                tile = {"fmt": fmt, "siz": siz, "line": line, "tmem": words, "palette": 0}
                img = arr(w0, h0, decode(info["tmem"], tile, w0, h0, "RGBA16" if tlut else "None"))
                src = sources[f"rom{index}"] = {"img": img, "modes": None, "alpha1": info["type"] == 0,
                                                "outputs": []}
            src["outputs"].append((h, w, hh, rotation))
            if h in seen_modes and level == 0:
                src["modes"] = seen_modes[h]
        rom_set = set(rom_hash)
        print(f"world.py: {len(texinfo)} ROM textures, {len(rom_set)} hashes "
              f"({sum(i['moving'] for i in texinfo.values())} scrolling)")

        # 2. Dumped world textures the ROM set doesn't cover.
        groups = defaultdict(list)
        for info in sorted(dumps.glob("*.tile.json")) if dumps.exists() else []:
            base = info.name[: -len(".tile.json")]
            h = base.split(".v")[0]
            t = json.loads(info.read_text())
            if h in ui or h in rom_set or t["width"] >= 320 or (t["width"], t["height"]) == (29, 29) \
                    or not (pngs / f"{h}.png").exists():
                continue
            groups[hashlib.sha1((dumps / f"{base}.tmem").read_bytes()).hexdigest()].append((h, t))
        extra = 0
        for members in groups.values():
            rest = sorted(members, key=lambda m: -(m[1]["width"] * m[1]["height"]))
            while rest:
                (h, t), rest = rest[0], rest[1:]
                big = arr(*read_png(pngs / f"{h}.png"))
                outputs, keep = [(h, t["width"], t["height"], 0)], []
                for mh, mt in rest:
                    small = arr(*read_png(pngs / f"{mh}.png"))
                    if t["width"] % mt["width"] == 0 and t["height"] % mt["height"] == 0 and \
                            (mt["width"], mt["height"]) != (t["width"], t["height"]) and is_level(big, small):
                        outputs.append((mh, mt["width"], mt["height"], 0))
                    else:
                        keep.append((mh, mt))
                rest = keep
                if t["width"] * t["height"] < 16:
                    continue
                sources[f"dump{h}"] = {"img": big, "modes": (t["tile"]["cms"], t["tile"]["cmt"]),
                                       "alpha1": t["tile"]["fmt"] == 0 and t["tile"]["siz"] == 2, "outputs": outputs}
                extra += 1
        print(f"world.py: {extra} more from texture dumps")

        # 3. Upscale.
        src_dir, big_dir = Path(tmp) / "in", Path(tmp) / "out"
        src_dir.mkdir()
        big_dir.mkdir()
        todo = {}
        for name, s in sources.items():
            h0, w0 = s["img"].shape[:2]
            if w0 * h0 < 16:
                continue  # 1x1 .. 2x2: a flat colour, nothing to gain
            # A margin of a quarter tile (at least 4 texels) of the repeat on each side: enough context for the
            # upscaler to tile without seams, a quarter of the work of a full 3x3.
            m = max(4, min(w0, h0) // 4)
            full = pad_tile(s["img"], *(s["modes"] or (0, 0)))
            padded = np.ascontiguousarray(full[h0 - m:2 * h0 + m, w0 - m:2 * w0 + m])
            write_png(src_dir / f"{name}.png", padded.shape[1], padded.shape[0], padded.tobytes())
            s["margin"] = m
            todo[name] = s
        print(f"world.py: upscaling {len(todo)} images")
        orig_dir = Path(tmp) / "orig"
        shutil.copytree(src_dir, orig_dir)  # undoubled copies, for keep_grain's bicubic reference
        subprocess.run(["magick", "mogrify", "-filter", "Catrom", "-resize", "200%", *map(str, src_dir.glob("*.png"))],
                       check=True)
        r = subprocess.run([str(esrgan), "-i", str(src_dir), "-o", str(big_dir), "-n", args.model, "-s", str(AI_SCALE),
                            "-f", "png"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            sys.exit(f"realesrgan failed:\n{r.stderr[-2000:]}")

        # 4. Write every output.
        final, out = out, out.with_name(out.name + ".new")  # built aside, swapped in at the end
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        textures = []
        for name, s in todo.items():
            h0, w0 = s["img"].shape[:2]
            ai = arr(*read_png(big_dir / f"{name}.png"))
            subprocess.run(["magick", str(orig_dir / f"{name}.png"), "-filter", "Catrom", "-resize",
                            f"{ai.shape[1]}x{ai.shape[0]}!", f"PNG32:{Path(tmp) / 'ref.png'}"], check=True)
            ref = arr(*read_png(Path(tmp) / "ref.png"))
            up = sharpen(keep_grain(ai, ref, SCALE * 1.5))
            W, H, M = w0 * SCALE, h0 * SCALE, s["margin"] * SCALE
            centre = np.ascontiguousarray(up[M:M + H, M:M + W])
            if s["alpha1"]:
                centre[..., 3] = np.where(centre[..., 3] >= 128, 255, 0)
            for h, lw, lh, rotation in s["outputs"]:
                if (lw, lh) == (w0, h0):
                    img = np.roll(centre, rotation * SCALE, axis=1) if rotation else centre
                elif w0 % lw == 0 and h0 % lh == 0:
                    img = box_down(centre, lw * SCALE, lh * SCALE)
                else:
                    continue
                write_png(out / f"{h}.png", img.shape[1], img.shape[0], np.ascontiguousarray(img).tobytes())
                textures.append({"path": f"{h}.png", "hashes": {"rt64": h}})
    db = {
        "configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream",
                          "defaultShift": "half", "hashVersion": 5},
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
    print(f"world.py: {len(textures)} replacements in {final}")


if __name__ == "__main__":
    main()
