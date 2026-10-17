#!/usr/bin/env python3
"""triage_textures.py: which exported textures would gain from a painted redraw, and which an AI upscale serves.

    uv run --no-project --with pillow --with numpy python tools/hdui/triage_textures.py
        -> build/art-export/triage/ (triage.csv, triage.json, sheets/)

Reads build/art-export/AidynTextures (export_textures.py: manifest.json and original/), measures every texture's
original pixels and sorts it into one of:
  painted   detailed material the eye lands on: characters, items, and world surfaces with real structure (stone,
            wood, bark, foliage, ground, roofs, cloth). An upscale only blurs these.
  optional  some structure, or little seen (few users): a painted version helps, but late in the queue.
  upscale   nothing for a painter to add: flat colours and gradients, tiny textures, greyscale pictures the game
            tints (glows, smoke, light, sparkles), soft see-through effects, and scrolling water/fog/lava (their
            sideways wrap has to stay exact).
  done      already redrawn (art/textures).
Measures: detail = mean luminance gradient (0..255 per texel) over the opaque part, contrast = its luminance standard
deviation, colours = distinct colours; plus format, transparency, size, scrolling and how many models and areas use it.
Each row says why. Contact sheets per bucket (and per category in painted) let a person check the sort by eye.
"""
import csv
import json
import subprocess
from pathlib import Path

import numpy as np

PROJECT = Path(__file__).resolve().parents[2]
EXPORT = PROJECT / "build/art-export/AidynTextures"
OUT = PROJECT / "build/art-export/triage"
GREY = {"I4", "I8", "IA4", "IA8", "IA16"}


def measure(path):
    from PIL import Image
    return measure_rgba(np.asarray(Image.open(path).convert("RGBA")))


def measure_rgba(rgba):
    """detail, contrast, colours, saturation and opaque share of an H x W x 4 picture (export_textures.py --to-paint
    passes its pixels here)."""
    img = rgba.astype(np.float32)
    a = img[..., 3] / 255.0
    lum = img[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    opaque = a > 0.5
    share = float(opaque.mean())
    if opaque.sum() < 4:
        return {"opaque": share, "detail": 0.0, "contrast": 0.0, "colours": 0, "saturation": 0.0}
    gx = np.abs(np.diff(lum, axis=1))
    gy = np.abs(np.diff(lum, axis=0))
    mx = opaque[:, 1:] & opaque[:, :-1]
    my = opaque[1:, :] & opaque[:-1, :]
    detail = float((gx[mx].sum() + gy[my].sum()) / max(1, mx.sum() + my.sum()))
    rgb = img[..., :3][opaque]
    sat = (rgb.max(axis=1) - rgb.min(axis=1)).mean()
    colours = len({tuple(c) for c in (rgb // 8).astype(np.int32)})  # 5-bit levels: near-identical shades count once
    return {"opaque": share, "detail": detail, "contrast": float(lum[opaque].std()), "colours": colours,
            "saturation": float(sat)}


def classify(e, m, done):
    cat = e["file"].split("/")[0]
    users, areas = len(e["used_by"]), len(e["areas"])
    texels = e["width"] * e["height"]
    detail, contrast = m["detail"], m["contrast"]
    if e["rom_index"] in done:
        return "done", "already redrawn"
    if cat == "effects":
        return "upscale", "spell / potion effect"
    if e["format"] in GREY:
        return "upscale", "greyscale the game tints (glow, smoke, light, shading)"
    if e["transparency"] == "soft" and m["opaque"] < 0.6:
        return "upscale", "soft see-through effect"
    if m["colours"] <= 6 or detail < 4 or (detail < 5 and contrast < 8):
        return "upscale", "flat colour or smooth gradient"
    if e["scrolls"]:
        if detail >= 8 and contrast >= 12:
            return "optional", "scrolling water/lava with structure: a painted one must wrap sideways exactly"
        return "upscale", "scrolling fog/haze: its wrap must stay exact"
    if texels <= 16 * 16:
        if detail >= 12 and users >= 3:
            return "optional", "tiny but detailed and repeated widely (tiled walls/floors)"
        return "upscale", "tiny (16x16 or less)"
    if cat in ("characters", "items"):
        if detail < 7:
            return "optional", f"{cat[:-1]}, smooth (skin, plain cloth): little to paint"
        return "painted", f"{cat[:-1]}, seen up close"
    if cat == "other":
        if detail >= 14 and users > 0:
            return "optional", "used only by models nothing names, but detailed"
        return "upscale", "used by no named model (code-drawn odds and ends)"
    # world: any real structure gains from paint; the most used first
    if users >= 5 or areas >= 2:
        return "painted", "world surface with structure, used widely"
    return "optional", "world surface with structure, used little"


def main():
    manifest = json.loads((EXPORT / "manifest.json").read_text())["textures"]
    done = set()
    for p in (PROJECT / "art/textures").rglob("tex*.png"):
        done.add(int(p.stem[3:]))
    rows = []
    for e in manifest:
        m = measure(EXPORT / "original" / e["file"])
        bucket, why = classify(e, m, done)
        rows.append({"file": e["file"], "rom_index": e["rom_index"], "bucket": bucket, "why": why,
                     "category": e["file"].split("/")[0], "group": "/".join(e["file"].split("/")[1:-1]),
                     "size": f"{e['width']}x{e['height']}", "format": e["format"],
                     "transparency": e["transparency"], "scrolls": e["scrolls"], "users": len(e["used_by"]),
                     "areas": len(e["areas"]), "detail": round(m["detail"], 1), "contrast": round(m["contrast"], 1),
                     "colours": m["colours"], "opaque": round(m["opaque"], 2)})
    OUT.mkdir(parents=True, exist_ok=True)
    order = {"painted": 0, "optional": 1, "upscale": 2, "done": 3}
    rows.sort(key=lambda r: (order[r["bucket"]], -r["users"], r["file"]))
    with open(OUT / "triage.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    (OUT / "triage.json").write_text(json.dumps(rows, indent=1) + "\n")
    # Contact sheets: originals at 8x (x8/), labelled with number, size and detail, per bucket and category.
    sheets = OUT / "sheets"
    sheets.mkdir(exist_ok=True)
    for old in sheets.glob("*.png"):
        old.unlink()
    groups = {}
    for r in rows:
        groups.setdefault(f"{r['bucket']}-{r['category']}", []).append(r)
    for name, rs in groups.items():
        for part in range(0, len(rs), 120):
            chunk = rs[part:part + 120]
            args = []
            for r in chunk:
                args += ["-label", f"{r['rom_index']} d{r['detail']:.0f} u{r['users']}", str(EXPORT / "x8" / r["file"])]
            dst = sheets / f"{name}-{part // 120 + 1}.png"
            subprocess.run(["magick", "montage", *args, "-tile", "12x", "-geometry", "96x96+3+3", "-background",
                            "#2b2b2b", "-fill", "#e8e2cc", "-pointsize", "10", str(dst)], check=True)
    counts = {}
    for r in rows:
        key = (r["bucket"], r["category"])
        counts[key] = counts.get(key, 0) + 1
    for b in order:
        total = sum(v for (bb, _), v in counts.items() if bb == b)
        parts = ", ".join(f"{c} {v}" for (bb, c), v in sorted(counts.items()) if bb == b)
        print(f"{b:9s} {total:5d}  ({parts})")
    print(f"triage_textures.py: {OUT / 'triage.csv'}, sheets in {sheets}")


if __name__ == "__main__":
    main()
