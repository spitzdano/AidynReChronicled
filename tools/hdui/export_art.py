#!/usr/bin/env python3
"""export_art.py: the game's portraits and item icons as a package, for redrawing them some other way.

    tools/hdui/export_art.py [--rom ROM]     -> build/art-export/AidynArt/ and build/art-export/AidynArt.zip

Every BORG8_Portrait* (characters, people, creatures, signs; not PortraitHighlight, the frame), BORG8_IconItem* and
BORG8_IconSlot* image, read from the ROM (romassets) and written three ways:
  original/     the N64 picture, 1 pixel per texel (40x40 portraits, 16x16 icons)
  x8/           the same, 8x with nearest-neighbour (every texel an 8x8 block): a clean input for redrawing
  current-hd/   what the HD UI pack shows now: Real-ESRGAN, 8x (tools/manual/romart.py, the same model and doubling)
plus redone/ (the same folders, empty: where new versions go), manifest.json (per image: ROM index, size, format,
transparency, the size a new version should have), contact sheets and README.md. File names are the decomp's Borg8
names, which the import side keys on. Everything here is the ROM's art: it stays in build/, never in the repository.
"""
import argparse
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(PROJECT / "tools/manual"))
import romart  # noqa: E402
import romassets  # noqa: E402
from build_manual import toolkit  # noqa: E402
from pngio import decode_borg8, write_png  # noqa: E402

OUT = PROJECT / "build/art-export"
FORMATS = {1: "RGBA32", 2: "RGBA16", 3: "IA16", 4: "CI8", 5: "IA8", 6: "I8", 7: "CI4", 8: "IA4", 9: "I4"}
SCALE = 8
PARTY = {"Alaron", "Brenna", "Abrecan", "Rheda", "Godrik", "Becan", "Arturo", "Keelin", "Niesen", "Donovan", "Baird",
         "Dougal", "Sholeh"}
CREATURES = {"AirElem", "EarthElem", "FireElem", "WaterElem", "Bat", "Bear", "Boar", "Cyclops", "DryadF", "DryadM",
             "Golem", "Gorgon", "Gryphon", "Harpy", "HellHound", "Horse", "Lich", "LizardMan", "LizardBoss", "LizardSgt",
             "Manticore", "MinoLord", "Minotuar", "Ogre", "OgreBoss", "Rat", "Salamander", "SandWorm", "Scorpion",
             "Shadow", "Skeleton", "Crossbone", "Tentacle", "Troll", "Wolf", "Wraith", "Wyvern", "Zombie", "Goblin1",
             "Goblin2", "Hobgoblin"}
SIGNS = {"KingdomSign", "Banner", "Signpost"}


def group_of(name):
    """Folder under portraits/, items/ or slots/ for a Borg8 name, or None to leave it out."""
    if name.startswith("IconItem"):
        return "items"
    if name.startswith("IconSlot"):
        return "slots"
    if name.startswith("Portrait") and name != "PortraitHighlight":
        who = name[len("Portrait"):]
        sub = "party" if who in PARTY else "creatures" if who in CREATURES else "signs" if who in SIGNS else "people"
        return f"portraits/{sub}"
    return None


def nearest(w, h, rgba, k):
    out = bytearray(w * k * h * k * 4)
    for y in range(h * k):
        row = (y // k) * w
        for x in range(w * k):
            o = (row + x // k) * 4
            out[(y * w * k + x) * 4:(y * w * k + x) * 4 + 4] = rgba[o:o + 4]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    args = ap.parse_args()
    rom = romassets.Rom(args.rom)
    names = romart.names(PROJECT / "decomp")
    root = OUT / "AidynArt"
    shutil.rmtree(root, ignore_errors=True)
    images = []
    for name, index in sorted(names.items(), key=lambda kv: kv[0].lower()):
        group = group_of(name)
        if group is None:
            continue
        fmt, w, h, pixels, palette = rom.image(index)
        rgba = decode_borg8(fmt, w, h, pixels, palette)
        transparent = any(rgba[k] < 255 for k in range(3, len(rgba), 4))
        for variant, (W, H, data) in (("original", (w, h, rgba)), ("x8", (w * SCALE, h * SCALE, nearest(w, h, rgba, SCALE)))):
            path = root / variant / group / f"{name}.png"
            path.parent.mkdir(parents=True, exist_ok=True)
            write_png(path, W, H, data)
        (root / "redone" / group).mkdir(parents=True, exist_ok=True)
        images.append({"name": name, "file": f"{group}/{name}.png", "group": group, "rom_index": index,
                       "width": w, "height": h, "format": FORMATS[fmt], "transparent": transparent,
                       "redo_size": [w * SCALE, h * SCALE]})

    esrgan = toolkit() / "opt/realesrgan/realesrgan-ncnn-vulkan"
    hd = romart.build(args.rom, PROJECT / "decomp", esrgan, [i["name"] for i in images], PROJECT / "build/manual/art")
    for i in images:
        dst = root / "current-hd" / i["file"]
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(hd[i["name"]], dst)

    (root / "manifest.json").write_text(json.dumps({
        "game": "Aidyn Chronicles: The First Mage (N64, NTSC-U 1.0)",
        "scale": SCALE,
        "variants": {"original": "as stored in the ROM", "x8": "nearest-neighbour 8x",
                     "current-hd": "Real-ESRGAN 8x, as in the HD UI pack", "redone": "new versions go here"},
        "images": images}, indent=1) + "\n")
    shutil.copyfile(HERE / "export_art_README.md", root / "README.md")

    # Contact sheets: the x8 pictures shrunk to 4x, labelled.
    sheets = {"portraits": sorted(root.glob("x8/portraits/*/*.png"), key=lambda p: (p.parent.name, p.name.lower())),
              "items": sorted(root.glob("x8/items/*.png")) + sorted(root.glob("x8/slots/*.png"))}
    for sheet, files in sheets.items():
        subprocess.run(["magick", "montage", "-label", "%t", *[str(f) for f in files], "-tile", "10x",
                        "-geometry", "160x160+6+6" if sheet == "portraits" else "64x64+6+6", "-filter", "point",
                        "-background", "#2b2b2b", "-fill", "#e8e2cc", "-pointsize", "11" if sheet == "portraits" else "9",
                        str(root / f"contact-{sheet}.png")], check=True)

    zip_path = OUT / "AidynArt.zip"
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(root.rglob("*")):
            z.write(f, f.relative_to(OUT))
    counts = {}
    for i in images:
        counts[i["group"]] = counts.get(i["group"], 0) + 1
    print(f"export_art.py: {len(images)} images ({', '.join(f'{k} {v}' for k, v in sorted(counts.items()))})")
    print(f"export_art.py: {root}\n               {zip_path} ({zip_path.stat().st_size // 1024 // 1024} MB)")


if __name__ == "__main__":
    main()
