#!/usr/bin/env python3
"""portrait_states.py: a party member's portrait changing with how hurt they are.

    tools/hdui/portrait_states.py header [--states <dir>]       src/game/portrait_states.h again
    tools/hdui/portrait_states.py pack <out folder> [--states <dir>]
                                                                  a texture pack of just the states' pictures (for trying them without
                                                                  rebuilding the whole art pack)

The pictures are art/portraits/party/states/Portrait<Name>-<n>.png (n 1 to 4, 320 x 320: the portrait with its damage painted on; see
art/README.md), n being how hurt: 1 tired (61 to 80 % of the hit points left), 2 roughed up (41 to 60), 3 bleeding (21 to 40), 4 near
collapse (20 or less). The fresh portrait (more than 80 %) is the ordinary one.

The game draws a party member's portrait from one picture (CharSheet +0x44), and the art pack replaces a picture by the hash of its pixels, so a
state needs a picture of its own to be a texture of its own, as the items' icons do (item_icons.py): for state n the (n - 1)th palette entry the
picture doesn't use takes the colour of its middle texel and the middle texel points at it. It draws the same but hashes differently.
src/game/patches.cpp ("Portrait states") makes that change in the sheet's own picture when the hit points cross a line. The header says which
portraits (by the hash of their pixels, the middle texel left out) have which states painted, so none is shown without its picture.
"""
import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import item_icons  # noqa: E402
import portraits  # noqa: E402
import redone  # noqa: E402
import romassets  # noqa: E402
from pngio import read_png, write_png  # noqa: E402

PROJECT = HERE.parents[1]
STATES = PROJECT / "art/portraits/party/states"
HEADER = PROJECT / "src/game/portrait_states.h"
PARTY = ["Abrecan", "Alaron", "Arturo", "Baird", "Becan", "Brenna", "Donovan", "Dougal", "Godrik", "Keelin", "Niesen", "Rheda", "Sholeh"]
MAX_STATE = 4


def fnv(pixels, mid):
    """FNV-1a 64 of the pixels with the middle texel taken as 0 (what a state changes)."""
    h = 0xCBF29CE484222325
    for i, b in enumerate(pixels):
        h = ((h ^ (0 if i == mid else b)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def found(states_dir):
    """{name: {n: path}} of the state pictures there are."""
    out = {}
    for p in sorted(Path(states_dir).glob("Portrait*-?.png")) if Path(states_dir).is_dir() else []:
        m = re.fullmatch(r"(Portrait[A-Za-z]+)-([1-4])", p.stem)
        if m:
            out.setdefault(m.group(1), {})[int(m.group(2))] = p
    return out


def party_images(rom, names):
    """{name: (index, (fmt, w, h, pixels, palette))} of the party's portraits."""
    by_name = {n: i for i, n in names.items()}
    out = {}
    for who in PARTY:
        name = "Portrait" + who
        if name in by_name:
            out[name] = (by_name[name], rom.image(by_name[name]))
    return out


def write_header(rom_path, states_dir):
    rom = romassets.Rom(rom_path)
    names = romassets.borg8_names(PROJECT / "decomp")
    have = found(states_dir)
    lines = ["// portrait_states.h: written by tools/hdui/portrait_states.py (don't edit by hand).",
             "// The party's portraits (by the FNV-1a hash of their CI8 pixels with the middle texel as 0) and which damage states have a",
             "// picture painted (bit n of `states` for state n, 1 to 4): src/game/patches.cpp, \"Portrait states\".",
             "#pragma once", "#include <cstdint>", "",
             "struct PortraitStateInfo {", "    uint64_t hash;", "    uint16_t w, h;", "    uint8_t states;", "    const char* name;", "};", "",
             "constexpr PortraitStateInfo kPortraitStates[] = {"]
    for name, (index, (fmt, w, h, pixels, palette)) in party_images(rom, names).items():
        if fmt != 4:
            continue
        mask = sum(1 << n for n in have.get(name, {}))
        lines.append(f"    {{ 0x{fnv(pixels, (h // 2) * w + w // 2):016X}ull, {w}, {h}, 0x{mask:02X}, \"{name}\" }},")
    lines += ["};", ""]
    HEADER.write_text("\n".join(lines))
    print(f"portrait_states.py: wrote {HEADER} ({sum(len(v) for v in have.values())} state pictures)")


def state_image(img, n):
    """The portrait's picture for state n: drawn the same, a texture of its own (item_icons.variant)."""
    fmt, w, h, pixels, palette = img
    p, pal = item_icons.variant(fmt, w, h, pixels, palette, n - 1)
    return fmt, w, h, p, pal


def entries(rom_path, out, states_dir, tool, scale=redone.SCALE, crop_scale=4, log=print):
    """Writes the states' replacement pictures into `out`; returns their rt64.json entries."""
    import numpy as np
    rom = romassets.Rom(rom_path)
    names = romassets.borg8_names(PROJECT / "decomp")
    images = party_images(rom, names)
    result = []
    for name, picts in found(states_dir).items():
        if name not in images:
            log(f"portrait_states.py: {name}: not a party portrait (skipped)")
            continue
        _, (fmt, w, h, pixels, palette) = images[name]
        for n, path in sorted(picts.items()):
            img = state_image((fmt, w, h, pixels, palette), n)
            UW, UH, up = redone.load(path, w, h)
            arr = np.frombuffer(bytes(up), np.uint8).reshape(UH, UW, 4).astype(np.uint32)
            per = UW // w
            js = portraits.jobs_for(img)
            hs = portraits.hashes(tool, [j for _, j in js])
            for ((x0, tw), _), hash_ in zip(js, hs):
                whole = (x0, tw) == (0, w)
                s = min(per, scale if whole else crop_scale)
                step = per // s
                box = arr.reshape(h * s, step, w * s, step, 4).mean(axis=(1, 3)).round().astype(np.uint8) if step > 1 else arr.astype(np.uint8)
                cut = np.ascontiguousarray(box[:, x0 * s:(x0 + tw) * s])
                write_png(Path(out) / f"{hash_}.png", cut.shape[1], cut.shape[0], cut.tobytes())
                result.append({"path": f"{hash_}.png", "hashes": {"rt64": hash_}, "operation": "preload" if whole else "stream"})
        log(f"portrait_states.py: {name}: states {sorted(picts)}")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("what", choices=["header", "pack"])
    ap.add_argument("out", nargs="?", type=Path)
    ap.add_argument("--states", type=Path, default=STATES)
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    args = ap.parse_args()
    if args.what == "header":
        write_header(args.rom, args.states)
        return
    if args.out is None:
        sys.exit("portrait_states.py pack <out folder>")
    import json
    import shutil
    tool = PROJECT / "build/cmake/tmemhash"
    shutil.rmtree(args.out, ignore_errors=True)
    args.out.mkdir(parents=True)
    ents = entries(args.rom, args.out, args.states, tool)
    db = {"configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream", "defaultShift": "none", "hashVersion": 5},
          "textures": ents, "operationFilters": [], "shiftFilters": [], "extraFiles": []}
    (args.out / "rt64.json").write_text(json.dumps(db, indent=1) + "\n")
    print(f"portrait_states.py: {len(ents)} replacements in {args.out}")


if __name__ == "__main__":
    main()
