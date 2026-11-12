#!/usr/bin/env python3
"""refpoint_audit.py: the map points the game's own data asks for and the maps don't have (issue #81).

    tools/data/refpoint_audit.py [--rom baserom.z64] [--out report.md]

A map (Borg9) holds reference points, each with a number; the game's data refers to them by number: a dialogue's
commands (walk or run to a point, face one, create an actor at one, send the camera to one), a camera object (where it
looks) and a teleporter (the point the party arrives at, and where the camera aims, in the destination map). A number
the map doesn't have is a null pointer in the game's code. The N64 build and the port both read through it; the port
faults (the Keelin conversation in the Terminor inn asks for point 34, the inn's map has 1 to 33).

This reads the ROM (never copied anywhere: the report names assets and numbers only) and says, for every dialogue
that a map's dialogue object starts, which of its points that map lacks, for every camera object whose point its map lacks, and for
every teleporter whose destination we can find, the points its destination map lacks. Dialogues no map's dialogue
object starts (an NPC starts them) are listed apart, with the maps of the NPCs in their actor table where the ids
are missing, as the less certain half.

Layouts (decomp include/borg/borg9/b9voxel.h, borg13.h, as the ROM keeps them before the game relocates the pointers):
  Borg9Data: voxel object count u16 at +0x28, the objects' offset u32 at +0x44; each voxelObject 0x6C bytes (type u16 +0x16;
             then the union at +0x28: reference point id u16 +0x28, name +0x2C; camera object's point s16 +0x28; dialogue
             object's dialogue u16 +0x28; monster party's NPC u16 +0x48; teleporter's map group, grid x, grid y and point u16 +0x28 to +0x2E, its camera point +0x36).
  Borg13Data: commands' offset u32 +0, actors' offset u32 +4, command count u8 +0x12, actor count u8 +0x13; each command
             0x38 bytes: four ops {u16 value, u8 command, u8, u32} at +0; each actor four u16, the third the NPC.
"""
import argparse
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "hdui"))
import romassets  # noqa: E402

PROJECT = HERE.parents[1]

VOXEL_REFPOINT, VOXEL_CAMERA, VOXEL_DIALOGUE, VOXEL_TELEPORTER, VOXEL_MONSTER = 6, 8, 9, 7, 5
# Dialogue commands (decomp B13_Commands) whose value is a reference point of the map: the camera sent to one (CameraCutTo 4, CameraOn 6,
# CameraAngleTo 8, CameraPanTo 10, CameraSineTo 12), SetActorFacing 17, CreateActor 18, ActorWalk 40, ActorRun 41.
POINT_COMMANDS = {4: "camera cut to", 6: "camera on", 8: "camera angle to", 10: "camera pan to", 12: "camera sine to",
                  17: "actor faces", 18: "actor created at", 40: "actor walks to", 41: "actor runs to"}


def names(kind):
    """{asset index: name} of the decomp's BORG<kind>_ constants (Borgindecies.h)."""
    out = {}
    path = PROJECT / "decomp/include/Borgindecies.h"
    if path.exists():
        for m in re.finditer(r"#define\s+(BORG%d_\w+)\s+(0x[0-9A-Fa-f]+)" % kind, path.read_text(errors="replace")):
            out.setdefault(int(m.group(2), 16), m.group(1)[len("BORG%d_" % kind):])
    return out


# The groups, in EnumMapDatA order (a teleporter's first number), and the table that turns a group and a grid position into a map
# (decomp src_pseudo/BorgMapLists.cpp: { grid 1, grid 2, borg5, alt, borg9, ... }).
GROUPS = ["Overworld", "GoblinCamp", "PortSaiid", "Lighthouse", "GwerniaCastle", "GwerniaInteriors", "MiscInteriors", "Erromon", "Terminor",
          "Barrow", "RoogsCave", "JundarInteriors", "EhudTower", "Ugarit", "Ugarit2", "ChaosIsle", "Battlefeild"]


def map_lists():
    """{(group number, grid 1, grid 2): borg9 asset index}, from the decomp's tables."""
    consts = {}
    for m in re.finditer(r"#define\s+(BORG9_\w+)\s+(0x[0-9A-Fa-f]+)", (PROJECT / "decomp/include/Borgindecies.h").read_text(errors="replace")):
        consts[m.group(1)] = int(m.group(2), 16)
    text = (PROJECT / "decomp/src_pseudo/BorgMapLists.cpp").read_text(errors="replace")
    out = {}
    for g, name in enumerate(GROUPS):
        m = re.search(r"mapDataList mapDataList_%s\[\]\s*=\s*\{(.*?)\n\};" % name, text, re.S)
        if not m:
            continue
        for e in re.finditer(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*\w+\s*,\s*\w+\s*,\s*(BORG9_\w+)\s*,", m.group(1)):
            if e.group(3) in consts:
                out[(g, int(e.group(1)), int(e.group(2)))] = consts[e.group(3)]
    return out


class Map:
    def __init__(self, index, data):
        self.index = index
        count = struct.unpack_from(">H", data, 0x28)[0]
        objs = struct.unpack_from(">I", data, 0x44)[0]
        self.points = {}      # id -> name
        self.cameras = []     # (object index, point id)
        self.dialogues = []   # (object index, dialogue asset)
        self.npcs = set()
        self.teleporters = []  # (object index, map group, grid 1, grid 2, destination point, destination camera point)
        for k in range(count):
            o = objs + 0x6C * k
            if o + 0x6C > len(data):
                break
            kind = struct.unpack_from(">H", data, o + 0x16)[0]
            if kind == VOXEL_REFPOINT:
                pid = struct.unpack_from(">H", data, o + 0x28)[0]
                self.points[pid] = data[o + 0x2C:o + 0x3C].split(b"\0")[0].decode("latin1")
            elif kind == VOXEL_CAMERA:
                self.cameras.append((k, struct.unpack_from(">h", data, o + 0x28)[0]))
            elif kind == VOXEL_DIALOGUE:
                self.dialogues.append((k, struct.unpack_from(">H", data, o + 0x28)[0]))
            elif kind == VOXEL_MONSTER:
                self.npcs.add(struct.unpack_from(">H", data, o + 0x48)[0])
            elif kind == VOXEL_TELEPORTER:
                group, s1, s2, point = struct.unpack_from(">4H", data, o + 0x28)
                cam = struct.unpack_from(">H", data, o + 0x36)[0]
                self.teleporters.append((k, group, s1, s2, point, cam))


class Dialogue:
    def __init__(self, index, data):
        self.index = index
        cmds, actors = struct.unpack_from(">II", data, 0)
        count, nactors = data[0x12], data[0x13]
        self.uses = []  # (command number, command meaning, point id)
        for k in range(count):
            for j in range(4):
                value, com = struct.unpack_from(">HB", data, cmds + 0x38 * k + 8 * j)
                if com in POINT_COMMANDS and value != 0:
                    self.uses.append((k, POINT_COMMANDS[com], value))
        self.npcs = {struct.unpack_from(">4H", data, actors + 8 * a)[2] for a in range(nactors)}


def load(rom_path):
    rom = romassets.Rom(rom_path)
    maps, dialogues = {}, {}
    for i in range(rom.count):
        kind = rom.entry(i)[0]
        if kind not in (9, 13):
            continue
        try:
            data = rom.data(i)
            if kind == 9:
                maps[i] = Map(i, data)
            else:
                dialogues[i] = Dialogue(i, data)
        except Exception as e:  # an asset the layout doesn't fit (never a map or dialogue in practice)
            print(f"refpoint_audit.py: asset {i} (type {kind}) skipped: {e}", file=sys.stderr)
    return maps, dialogues


def report(maps, dialogues):
    mname = names(9)
    label = lambda m: f"map {m} ({mname[m]})" if m in mname else f"map {m}"
    out = ["# Reference points the game's data asks for and the maps don't have", "",
           f"{len(maps)} maps, {len(dialogues)} dialogues, read from the ROM. A point id is missing when the map the data runs on has no",
           "reference point of that number; the game's code then reads through a null pointer (the port faults on it).", ""]
    linked = defaultdict(set)   # dialogue asset -> maps whose dialogue objects start it
    for m in maps.values():
        for _, d in m.dialogues:
            linked[d].add(m.index)
    def rows_for(pairs, header):
        """One row per dialogue, map and missing point: what the dialogue does with it and how many times."""
        rows = []
        for d, m in pairs:
            dlg, have = dialogues[d], maps[m].points
            uses = defaultdict(list)
            for c, what, p in dlg.uses:
                if p not in have:
                    uses[p].append((c, what))
            for p, lst in sorted(uses.items()):
                kinds = defaultdict(int)
                for _, what in lst:
                    kinds[what] += 1
                rows.append(f"| {d} | {label(m)} | {p} | " + ", ".join(f"{w} x{n}" for w, n in sorted(kinds.items())) + " |")
        return ["| dialogue | " + header + " | point missing | used for |", "|---|---|---|---|"] + (rows or ["| none | | | |"]), len(rows)

    # 1. a dialogue a map's dialogue object starts
    pairs = [(d, m) for d, ms in sorted(linked.items()) if d in dialogues for m in sorted(ms)]
    table, n1 = rows_for(pairs, "on")
    bad_pairs = {(d, m) for d, m in pairs for _, _, p in dialogues[d].uses if p not in maps[m].points}
    out += ["## Dialogues a map's dialogue object starts (certain)", "",
            f"{len(pairs)} dialogue and map pairs; {len(bad_pairs)} use a point their map doesn't have ({len({d for d, _ in bad_pairs})} dialogues, "
            f"{len({m for _, m in bad_pairs})} maps).", ""] + table + [""]
    # 2. a dialogue no dialogue object starts (an NPC)
    out += ["## Dialogues no map's dialogue object starts (an NPC does): checked against the maps holding the NPCs in the actor table", ""]
    pairs2 = []
    for d, dlg in sorted(dialogues.items()):
        if d in linked or not dlg.uses:
            continue
        pairs2 += [(d, m.index) for m in maps.values() if m.npcs & dlg.npcs]
    table2, n2 = rows_for(pairs2, "maybe on")
    out += table2 + [""]
    # 2b. teleporters
    lists = map_lists()
    out_t, unresolved, total = [], 0, 0
    for m in maps.values():
        for k, group, s1, s2, point, cam in m.teleporters:
            total += 1
            dest = maps.get(lists.get((group, s1, s2), -1))
            if dest is None:
                unresolved += 1
                continue
            for what, pid in (("arrival point", point), ("camera point", cam)):
                if pid in (0, 0x7FF8) or pid in dest.points:
                    continue
                out_t.append(f"| {label(m.index)} | {k} | {label(dest.index)} | {what} | {pid} |")
    out += ["## Teleporters whose destination map lacks their arrival or camera point", "",
            f"{total} teleporters; {total - unresolved} destinations found in the map tables, {unresolved} not (a destination the table doesn't list).", "",
            "| from | object | to | what | point missing |", "|---|---|---|---|---|"] + (out_t or ["| none | | | | |"]) + [""]
    # 3. camera objects
    out += ["## Camera objects whose point their map lacks", ""]
    rows = [f"| {label(m.index)} | {k} | {p} |" for m in maps.values() for k, p in m.cameras if p > 0 and p not in m.points]
    out += ["| map | object | point missing |", "|---|---|---|"] + (rows or ["| none | | |"]) + [""]
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    maps, dialogues = load(args.rom)
    text = report(maps, dialogues)
    if args.out:
        args.out.write_text(text)
        print(f"refpoint_audit.py: wrote {args.out}")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
