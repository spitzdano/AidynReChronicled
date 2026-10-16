#!/usr/bin/env python3
"""export_textures.py: the game's 3D textures (characters, land, buildings, items) as a package, for redrawing them.

    tools/hdui/export_textures.py [--rom ROM] [--no-hd]   -> build/art-export/AidynTextures/ and AidynTextures.zip

Every world texture (Borg1 asset, romtextures.py), sorted by what uses it. The models are found the way the game
loads them: a character is a Borg7 actor (named in the decomp's Borgindecies.h; the entity tables give every person
and monster one of these) whose Borg6 animations each name a Borg5 model; an area's land and buildings are the Borg5
map chunks named after it (Overworld_A04…); the props an area places (trees, houses, furniture) are the scene
objects in its Borg9 collision maps (voxel objects of type Scene: up to three Borg5 or Borg7 indices each). A Borg5
lists its textures as Borg1 indices. Spell effects are the actors and models the decomp's spell visuals name.

    characters/<Name>/   one model's textures (party, people, monsters; the model's decomp name)
    world/<Area>/        one area's land, buildings and props (areas as the game names its map lists; also
                         cutscene sets: CoronationScene, Dreamscape, Campsite…; and Loot: chests and sacks)
    items/<Name>/        weapons, shields and other carried things
    effects/<Name>/      spells (named after the spell), potions, combat markers
    <category>/_shared/  used by several models of the category (characters, items, effects)
    other/               used by no model found, or only by models nothing names (loaded by the code itself)

A texture is in one folder only, even when several models use it (the ROM stores it once: one redraw changes it
everywhere): the most specific category that uses it (items, characters, effects, world), and within world
the area that uses it most. manifest.json lists every model and area that uses each texture.

Identical textures (several ROM copies with the same pixels: one RT64 hash, one HD replacement) are one file, named
after the first copy (tex<ROM index>.png); textures under 4x4 texels (flat colours) are left out. Written three ways:
original/ (1 pixel per texel), x8/ (8x nearest-neighbour), current-hd/ (the hd-world pack's picture for it, 8x;
--no-hd leaves it out), plus redone/ (empty), contact/ sheets, manifest.json and README.md
(export_textures_README.md). It's the ROM's art: it stays in build/.
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from collections import defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import romassets  # noqa: E402
import romtextures  # noqa: E402
from build_pack import PROJECT  # noqa: E402
from pngio import write_png  # noqa: E402
from world import arr, hashes, load_decoder  # noqa: E402

OUT = PROJECT / "build/art-export"
PACK = Path.home() / ".local/share/AidynReChronicled/texture-packs/hd-world"
SCALE = 8
MODEL, ACTOR, ANIMATION, MAP = 5, 7, 6, 9
FORMATS = {0: "RGBA16", 1: "IA16", 2: "CI8", 3: "IA8", 4: "I8", 5: "CI4", 6: "IA4", 7: "I4", 8: "RGBA32"}
# include/borgMaps.h's map lists; a map chunk's name starts with its area.
AREAS = ["Overworld", "GoblinCamp", "PortSaiid", "Lighthouse", "GwerniaCastle", "GwerniaInteriors", "MiscInteriors",
         "Erromon", "Terminor", "Barrow", "RoogsCave", "JundarInteriors", "EhudTower", "Ugarit", "Ugarit2", "ChaosIsle",
         "Battlefeild"]
SCENERY = {"LighthouseBeacon"}  # actors that are part of an area, not characters
EFFECTS = ("PotionFX", "PauseMenu", "Combat", "SpellMarker", "Teleporter")
ITEMS = ("Weapon", "Shield", "Stormbreaker", "HasteAmulet", "HatKendall", "HornKynon", "LetterTxomin",
         "GwerniaBannerItem")
SCENES = {"ErromonMapChunk": "Erromon"}  # named models no map places, as world folders (else the name, minus digits)
PRIORITY = ["items", "characters", "effects", "world", "other"]
TILING = {0: "wrap", 1: "mirror", 2: "clamp", 3: "clamp"}


def u16(d, o):
    return int.from_bytes(d[o:o + 2], "big")


def u32(d, o):
    return int.from_bytes(d[o:o + 4], "big")


def borg_names(decomp):
    """{borg type: {index: name}} from include/Borgindecies.h."""
    import re
    names = defaultdict(dict)
    for line in (Path(decomp) / "include/Borgindecies.h").read_text().splitlines():
        m = re.match(r"#define\s+BORG(\d+)_(\w+)\s+(0x[0-9a-fA-F]+|\d+)", line)
        if m:
            names[int(m.group(1))].setdefault(int(m.group(3), 0), m.group(2))
    return names


def model_textures(rom, index):
    """A Borg5 model's Borg1 indices (Borg5Data: borg1Count at 0xC, borg1i offset at 0x30)."""
    d = rom.data(index)
    count, off = u32(d, 0xC), u32(d, 0x30)
    return [u32(d, off + 4 * k) for k in range(count)]


def actor_models(rom, index):
    """A Borg7 actor's Borg5 models: each of its animations (Borg7Data sub[] at 12, 12 bytes, Borg6 index first)
    names one (Borg6Data borg5 at 0)."""
    d = rom.data(index)
    out = set()
    for k in range(u32(d, 4)):
        b6 = u32(d, 12 + 12 * k)
        if b6 < rom.count and rom.entry(b6)[0] == ANIMATION:
            b5 = u32(rom.data(b6), 0)
            if b5 < rom.count and rom.entry(b5)[0] == MODEL:
                out.add(b5)
    return out


def placed(rom, index):
    """[(type, index)] of the models a Borg9 map places: voxel objects (108 bytes from Borg9Data+0x44, count at
    +0x28) of type 0 (Scene) hold up to 3 SceneVoxelModels (12 bytes from +0x28, index at +4); count at +0x68,
    flags at +0x6A (2: Borg7, else Borg5)."""
    d = rom.data(index)
    out = []
    count, off = u16(d, 0x28), u32(d, 0x44)
    for k in range(count):
        o = off + 108 * k
        if u16(d, o + 22) != 0:
            continue
        n, flags = u16(d, o + 0x68), u16(d, o + 0x6A)
        kind = ACTOR if flags & 2 else MODEL
        for j in range(min(n, 3)):
            b = u32(d, o + 0x28 + 12 * j + 4)
            if b < rom.count and rom.entry(b)[0] == kind:
                out.append((kind, b))
    return out


def spell_models(decomp):
    """{Borg7 or Borg5 index: [spell names]} from the decomp's spell visuals (src_pseudo/combat/spellvisuals.cpp:
    switches of case SpellInd_<Name>: ... <var> = 0x<index>;)."""
    import re
    out, pending = defaultdict(list), []
    for line in (Path(decomp) / "src_pseudo/combat/spellvisuals.cpp").read_text().splitlines():
        m = re.search(r"case SpellInd_(\w+):", line)
        if m:
            pending.append(m.group(1))
        m = re.search(r"\b(?:ind|[BuU]Var\d+)\s*=\s*(0x[0-9a-fA-F]+);", line)
        if m and pending:
            for p in pending:
                if p not in out[int(m.group(1), 16)]:
                    out[int(m.group(1), 16)].append(p)
            pending = []
        if "break;" in line or "return" in line:
            pending = []
    return out


def area_of(name):
    head = name.split("_")[0]
    return head if head in AREAS else None


def owners(rom, names):
    """{Borg5 index: {(category, group, user label)}}: who uses each model."""
    users = defaultdict(set)
    n5, n7, n9 = names[MODEL], names[ACTOR], names[MAP]
    # Characters and named effect actors.
    for a in (i for i in range(rom.count) if rom.entry(i)[0] == ACTOR):
        name = n7.get(a)
        if name is None or name in SCENERY:
            continue
        for m in actor_models(rom, a):
            users[m].add(("effects" if name.startswith(EFFECTS) else "characters", name, name))
    # Areas: map chunks and what their maps place (models, and actors other than characters: animated scenery).
    for i in range(rom.count):
        kind = rom.entry(i)[0]
        if kind == MODEL and i in n5 and area_of(n5[i]):
            users[i].add(("world", area_of(n5[i]), n5[i]))
        elif kind == MAP and i in n9 and area_of(n9[i]):
            for k, b in placed(rom, i):
                if k == ACTOR and b in n7 and n7[b] not in SCENERY:
                    continue
                label = (n5 if k == MODEL else n7).get(b) or f"{'model' if k == MODEL else 'actor'} {b}"
                for m in ([b] if k == MODEL else actor_models(rom, b)):
                    users[m].add(("world", area_of(n9[i]), label))
    # Named models no map places: items, effects, loot containers (world/Loot), scenes (world/<name>).
    for i, name in n5.items():
        if rom.entry(i)[0] != MODEL or users.get(i):
            continue
        if name.startswith(ITEMS):
            users[i].add(("items", name, name))
        elif name.startswith(EFFECTS):
            users[i].add(("effects", name, name))
        elif name.startswith("Loot"):
            users[i].add(("world", "Loot", name))
        else:
            users[i].add(("world", SCENES.get(name, name.rstrip("0123456789")), name))
    # Spell effects: actors or models.
    for b, spells in spell_models(PROJECT / "decomp").items():
        if b < rom.count and rom.entry(b)[0] in (ACTOR, MODEL):
            for m in actor_models(rom, b) if rom.entry(b)[0] == ACTOR else [b]:
                users[m].add(("effects", spells[0], "spell " + ", ".join(spells)))
    # The rest (no map, entity table or name points to them: models the code loads itself).
    for a in (i for i in range(rom.count) if rom.entry(i)[0] == ACTOR and i not in n7):
        for m in actor_models(rom, a):
            if not users.get(m):
                users[m].add(("other", None, f"actor {a}"))
    for m in (i for i in range(rom.count) if rom.entry(i)[0] == MODEL):
        if not users.get(m):
            users[m].add(("other", None, f"model {m}"))
    return users


def folder(uses):
    """(category, group) for a texture used by uses = {(category, group, label)}: one folder only, the most specific
    category that uses it (PRIORITY). Within world, the area that uses it most; elsewhere the one model, or _shared."""
    if not uses:
        return "other", None
    cat = min({c for c, _, _ in uses}, key=PRIORITY.index)
    if cat == "other":
        return cat, None
    mine = [(g, label) for c, g, label in uses if c == cat]
    if cat == "world":
        per_area = defaultdict(int)
        for g, _ in mine:
            per_area[g] += 1
        return cat, min(per_area, key=lambda g: (-per_area[g], g))
    groups = {g for g, _ in mine}
    return cat, groups.pop() if len(groups) == 1 else "_shared"


def seen_tiling(work):
    """{RT64 hash: (cms, cmt)} from the texture dumps collected while playing (build/hdui/dumps)."""
    modes = {}
    for info in (work / "dumps").glob("*.tile.json") if (work / "dumps").exists() else []:
        t = json.loads(info.read_text())["tile"]
        modes[info.name.split(".")[0]] = (t["cms"], t["cmt"])
    return modes


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    ap.add_argument("--no-hd", action="store_true", help="leave out current-hd/ (the zip is much smaller)")
    args = ap.parse_args()
    tool = PROJECT / "build/cmake/tmemhash"
    if not tool.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    rom = romassets.Rom(args.rom)
    names = borg_names(PROJECT / "decomp")
    model_users = owners(rom, names)
    tex_users = defaultdict(set)
    for m in (i for i in range(rom.count) if rom.entry(i)[0] == MODEL):
        for t in model_textures(rom, m):
            tex_users[t] |= model_users.get(m, set())

    # Level 0 of every texture, its RT64 hash and pixels; copies with the same hash are one texture.
    decode = load_decoder()
    texinfo = dict(romtextures.textures(rom))
    with tempfile.TemporaryDirectory() as tmp:
        jobs = [(i["tmem"], *i["tiles"][0][:4], i["w"], i["h"], i["tiles"][0][6]) for i in texinfo.values()]
        level0 = dict(zip(texinfo, hashes(tool, jobs, tmp)))
    by_hash = defaultdict(list)
    for index in sorted(texinfo):
        by_hash[level0[index]].append(index)
    tiling = seen_tiling(PROJECT / "build/hdui")

    root = OUT / "AidynTextures"
    shutil.rmtree(root, ignore_errors=True)
    entries, skipped, missing_hd = [], 0, 0
    for h, copies in by_hash.items():
        index, info = copies[0], texinfo[copies[0]]
        w, ht = info["w"], info["h"]
        if w * ht < 16:
            skipped += 1
            continue
        uses = set().union(*(tex_users.get(c, set()) for c in copies))
        cat, group = folder(uses)
        rel = Path(cat) / (group or "") / f"tex{index:05d}.png"
        fmt, siz, line, words, _, _, tlut = info["tiles"][0]
        img = arr(w, ht, decode(info["tmem"], {"fmt": fmt, "siz": siz, "line": line, "tmem": words, "palette": 0},
                                w, ht, "RGBA16" if tlut else "None"))
        big = np.repeat(np.repeat(img, SCALE, axis=0), SCALE, axis=1)
        for variant, im in (("original", img), ("x8", big)):
            (root / variant / rel).parent.mkdir(parents=True, exist_ok=True)
            write_png(root / variant / rel, im.shape[1], im.shape[0], np.ascontiguousarray(im).tobytes())
        (root / "redone" / rel.parent).mkdir(parents=True, exist_ok=True)
        hd = PACK / f"{h}.png"
        if not args.no_hd:
            if hd.exists():
                (root / "current-hd" / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(hd, root / "current-hd" / rel)
            else:
                missing_hd += 1
        alpha = img[..., 3]
        t = info["type"]
        mode = tiling.get(h)
        entries.append({
            "file": rel.as_posix(), "rom_index": index, "copies": copies[1:], "hash": h,
            "width": w, "height": ht, "format": FORMATS[t], "mip_levels": len(info["tiles"]),
            "transparency": "none" if alpha.min() == 255 else ("cut-out" if t == 0 or set(np.unique(alpha)) <= {0, 255}
                                                               else "soft"),
            "scrolls": info["moving"],
            "tiling": [TILING[mode[0]], TILING[mode[1]]] if mode else None,
            "used_by": sorted({label for _, _, label in uses}),
            "areas": sorted({g for c, g, _ in uses if c == "world"}),
            "redo_size": [w * SCALE, ht * SCALE]})

    (root / "manifest.json").write_text(json.dumps({
        "game": "Aidyn Chronicles: The First Mage (N64, NTSC-U 1.0)",
        "scale": SCALE,
        "variants": {"original": "as stored in the ROM (mip level 0)", "x8": "nearest-neighbour 8x",
                     "current-hd": "the hd-world pack's picture, 8x", "redone": "new versions go here"},
        "textures": entries}, indent=1) + "\n")
    shutil.copyfile(HERE / "export_textures_README.md", root / "README.md")

    # Contact sheets, one per folder: the x8 pictures shrunk to 128 pixels, labelled with the file name and size.
    groups = defaultdict(list)
    for e in entries:
        groups[str(Path(e["file"]).parent)].append(e)
    (root / "contact").mkdir()
    for g, es in sorted(groups.items()):
        files = []
        for e in sorted(es, key=lambda e: e["file"]):
            files += ["-label", f"{Path(e['file']).stem[3:]}  {e['width']}x{e['height']}", str(root / "x8" / e["file"])]
        subprocess.run(["magick", "montage", *files, "-tile", "8x", "-geometry", "128x128+6+6", "-filter", "point",
                        "-background", "#2b2b2b", "-fill", "#e8e2cc", "-pointsize", "11",
                        str(root / "contact" / f"{g.replace('/', '-')}.png")], check=True)

    zip_path = OUT / "AidynTextures.zip"
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(root.rglob("*")):
            z.write(f, f.relative_to(OUT))
    counts = defaultdict(int)
    for e in entries:
        counts[e["file"].split("/")[0]] += 1
    print(f"export_textures.py: {len(entries)} textures ({', '.join(f'{k} {v}' for k, v in sorted(counts.items()))}); "
          f"{skipped} flat colours left out" + (f"; {missing_hd} without an HD version" if missing_hd else ""))
    print(f"export_textures.py: {root}\n                    {zip_path} ({zip_path.stat().st_size // 1024 // 1024} MB)")


if __name__ == "__main__":
    main()
