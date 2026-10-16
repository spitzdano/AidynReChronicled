"""romdata.py: the spell list, read from a ROM's spell database (used by build_manual.py).

The database (decomp: data/spelldb.cpp SpellDB::Init, include/spells.h Spell_ROM) is an array header (u8 count + 3 junk
bytes) for all spells, then for each of the six schools a header and that many 36-byte packed records:
  char name[23]; u8 id, type; u8 school, damage, stamina, cast, target, flags, wizard, special, range, ingredient, exp
Read the way the game reads it, so the list matches the ROM it came from (the original or Aidyn Patch). It is found by
its first spell's name, so a patched ROM that moved it still works.
  school      0 Chaos (unused), 1 Elemental, 2 Naming, 3 Necromancy, 4 none ("Neutral"), 5 Star
  target      0 outside combat only, 1 the party, 2 enemies, 3 an area, 4 the whole battlefield (MagicTargetEnum)
  flags       bit 1 Solar, bit 0 Lunar (resistance x0.75 or x1.25 against an aspected caster, combatEntity.cpp);
              bit 4 aimed at a spot (area spells)
  wizard      the Wizard skill needed to learn it
  special     1 the target must have a true name, 2 human-sized or smaller, 3 Solar targets only, 4 Lunar targets only
  ingredient  0 none, 1 spice, 2 herb, 3 gemstone (used up when cast)
  exp         rank n costs exp * 50 * n^2 experience (TempSpell::GetExpPrice)
"""
from pathlib import Path

SCHOOLS = {1: "Elemental", 2: "Naming", 3: "Necromancy", 4: "Neutral", 5: "Star"}
TARGETS = {0: "Outside combat", 1: "Party", 2: "Enemy", 3: "Area", 4: "Battlefield"}
SPECIAL = {1: "Target needs a true name", 2: "Human-sized or smaller", 3: "Solar targets only",
           4: "Lunar targets only"}
INGREDIENTS = {0: "", 1: "Spice", 2: "Herb", 3: "Gemstone"}
RECORD = 36


def _aspect(flags):
    return "Solar" if flags & 2 else "Lunar" if flags & 1 else "Neutral"


def spells(rom_path):
    """[dict] in the database's order (by school)."""
    rom = Path(rom_path).read_bytes()
    first = rom.find(b"Immolation\0", 0x1000000)
    if first < 12:
        raise SystemExit(f"romdata.py: no spell database in {rom_path}")
    pos = first - 12
    total = rom[pos]
    pos += 4
    out = []
    for school in range(6):
        count = rom[pos]
        pos += 4
        for _ in range(count):
            r = rom[pos:pos + RECORD]
            pos += RECORD
            f = r[23:]
            if f[2] != school:
                raise SystemExit(f"romdata.py: spell record at ROM 0x{pos - RECORD:X} is not in school {school}")
            out.append({
                "name": r[:23].split(b"\0")[0].decode("ascii"),
                "id": f[0] | f[1] << 8,
                "school": SCHOOLS.get(school, "Chaos"),
                "damage": f[3],
                "stamina": f[4],
                "target": TARGETS.get(f[6], "?"),
                "aimed": bool(f[7] & 0x10),
                "aspect": _aspect(f[7]),
                "rank": f[8],
                "special": SPECIAL.get(f[9], ""),
                "range": f[10],
                "ingredient": INGREDIENTS.get(f[11], "?"),
                "xp": f[12] * 50,
            })
    if len(out) != total:
        raise SystemExit(f"romdata.py: {len(out)} spells read, the header says {total}")
    return out
