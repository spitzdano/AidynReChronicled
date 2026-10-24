#!/usr/bin/env python3
"""item_icons.py: an icon of its own for each item that shares one (#25).

    tools/hdui/item_icons.py import <brief folder>   the painted icons into art/items/unique, item_icons.json and
                                                     src/game/item_icons.h from the brief (its items.py lists them)
    tools/hdui/item_icons.py header                  src/game/item_icons.h again from item_icons.json

The game draws 174 items with 31 icons (its item-to-icon table, ROM 0xDF700). Each gets a painted icon of its own
(art/items/unique/Item-<name>.png; the 18 maps share one, the 3 letters another). The game needs a texture of its
own per item for the art pack to replace, so each item's icon is a copy of the shared one with a change that leaves
it looking the same: for the item's variant j (its place among the items sharing that icon), the j-th palette entry
the picture doesn't use gets the colour of its middle texel, and the middle texel points at that entry. Every copy is
then different (pixels and palette both) but draws the same. src/game/patches.cpp ("Item icons") makes that copy when
an item's picture loads (GetItemImage, then loadBorg8's private copy); romimages.py makes the same copies at
0x220000 + n, with the painted icon as their replacement.
"""
import json
import re
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
DATA = HERE / "item_icons.json"
HEADER = PROJECT / "src/game/item_icons.h"
ART = PROJECT / "art/items/unique"


def variant(fmt, w, h, pixels, palette, j):
    """The shared icon's copy for variant j: (pixels, palette), drawn the same."""
    used = set(pixels)
    unused = [k for k in range(len(palette) // 2) if k not in used]
    k = unused[j]
    mid = (h // 2) * w + w // 2
    p, pal = bytearray(pixels), bytearray(palette)
    pal[2 * k:2 * k + 2] = palette[2 * pixels[mid]:2 * pixels[mid] + 2]
    p[mid] = k
    return bytes(p), bytes(pal)


def load():
    return json.loads(DATA.read_text())["icons"]


def write_header(icons):
    rows = []
    for n, icon in enumerate(icons):
        for item in icon["items"]:
            rows.append(f"    {{0x{item:04X}, 0x{icon['base']:04X}, {icon['variant']}}},  // {icon['name']}")
    HEADER.write_text(
        "// item_icons.h: written by tools/hdui/item_icons.py from tools/hdui/item_icons.json (don't edit by hand).\n"
        "// Each item that shares its icon with others: its item ID, the shared icon (Borg8 index) and its variant (its\n"
        "// place among the items sharing it): src/game/patches.cpp, \"Item icons\".\n"
        "#pragma once\n#include <cstdint>\n\n"
        "struct ItemIcon {\n    uint16_t item, icon;\n    uint8_t variant;\n};\n\n"
        f"constexpr ItemIcon kItemIcons[{len(rows)}] = {{\n" + "\n".join(rows) + "\n};\n")
    print(f"item_icons.py: {HEADER.relative_to(PROJECT)} ({len(rows)} items)")


def import_brief(brief):
    sys.path.insert(0, str(PROJECT / "tools/manual"))
    sys.path.insert(0, str(brief))
    import romdata
    from items import BATCHES
    rom_path = PROJECT / "baserom.z64"
    rom = rom_path.read_bytes()
    table, pos = {}, 0xDF700
    while True:
        key, value = struct.unpack(">HH", rom[pos:pos + 4])
        if key == 0 and table:
            break
        table[key] = value
        pos += 4
    borg = {int(v, 16): n for n, v in re.findall(r"#define BORG8_(\w+) (0x[0-9a-fA-F]+)",
                                                  (PROJECT / "decomp/include/Borgindecies.h").read_text())}
    ids = {}
    for kind in (romdata.gear, romdata.armour, romdata.weapons):
        for e in kind(rom_path):
            ids.setdefault(e["name"], []).append(e["id"])
    kinds_for = {"1-accessories": {1, 9, 10, 11, 12, 13, 14, 15, 19}, "2-keys-and-loot": {1, 17, 18},
                 "3-shields": {6}, "4-blades": {7}, "5-axes-and-maces": {7}, "6-staves-bows-polearms": {7},
                 "7-body-armour": {5}}
    icons, per_base = [], {}
    ART.mkdir(parents=True, exist_ok=True)
    for batch, (_, entries) in BATCHES.items():
        rows = dict(re.findall(r"\| `([^`]+\.png)` \| ([^|]+) \|", (brief / f"batch-{batch}/items.md").read_text()))
        for names, _ in entries:
            items = [i for n in names for i in ids.get(n, []) if i in table and (i >> 8) in kinds_for[batch]]
            if len(items) < len(names):
                sys.exit(f"item_icons.py: {names} not all in the icon table")
            file = next(f for f, who in rows.items() if who.strip() == names[0] or who.strip().endswith(", ".join(names)))
            name = "Item-" + Path(file).stem
            if any(i["name"] == name for i in icons):  # the hides: a crafting part (batch 2) and an armour (batch 7)
                name += "-" + batch.split("-", 1)[1]
            src = brief / "incoming" / f"batch-{batch}" / file
            shutil.copyfile(src, ART / f"{name}.png")
            # One icon per shared picture the items have now (the letters: Letter to Txomin has the scroll's).
            for base in dict.fromkeys(table[i] for i in items):
                j = per_base.get(base, 0)
                per_base[base] = j + 1
                icons.append({"name": name, "base": base, "base_name": borg[base], "variant": j,
                              "items": [i for i in items if table[i] == base]})
    DATA.write_text(json.dumps({"icons": icons}, indent=1) + "\n")
    print(f"item_icons.py: {len(icons)} icons for {sum(len(i['items']) for i in icons)} items -> "
          f"{DATA.relative_to(PROJECT)}, {len(list(ART.glob('*.png')))} pictures in {ART.relative_to(PROJECT)}")
    write_header(icons)


if __name__ == "__main__":
    if sys.argv[1:2] == ["import"] and len(sys.argv) == 3:
        import_brief(Path(sys.argv[2]).resolve())
    elif sys.argv[1:] == ["header"]:
        write_header(load())
    else:
        sys.exit(__doc__)
