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
import re
import struct
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


# ---- Weapons and creatures (for the Aidyn Plus tables) ----
# Both databases are laid out like the spells': a count (u8 + 3 junk), then per category a count and that many packed
# records (decomp: include/weapondb.h Weapon_ROM, 48 bytes, 11 weapon classes; include/entity.h Entity_ROM, 136 bytes,
# 7 categories). Found by their structure (every record a name and an item ID of the database's type), so a patched
# ROM that moved them still works.
STATS = ["Intelligence", "Willpower", "Dexterity", "Endurance", "Strength", "Stamina"]
SKILLS = ["Alchemist", "Diplomat", "Healer", "Loremaster", "Mechanic", "Merchant", "Ranger", "Stealth", "Thief",
          "Troubadour", "Warrior", "Wizard"]
WEAPON_CLASSES = ["Bite", "Breath", "Claw", "Hafted", "Missile", "Pole", "Spit", "Sting", "Sword", "Thrown", "Tusk"]
ELEMENTS = ["", "Earth", "Solar", "Physical", "Necromancy", "Fire", "Lunar", "Naming", "Water", "Magic", "Air", "Star",
            "Elemental", "Chaos", "Cutting", "Smashing", "Holy"]


def _find_db(rom, cats, size, name_len, id_off, db_types):
    for pos in range(0x1000000, len(rom) - 8, 4):
        if rom[pos] == 0:
            continue
        total, q, got = rom[pos], pos + 4, 0
        for _ in range(cats):
            n = rom[q]
            q += 4
            for _ in range(n):
                r = rom[q:q + size]
                s = r[:name_len].split(b"\0")[0]
                if len(s) < 3 or not all(32 <= c < 127 for c in s) or r[id_off + 1] not in db_types:
                    break
                q += size
                got += 1
            else:
                continue
            break
        else:
            if got == total:
                return pos
    return None


def _records(rom, pos, cats, size):
    q, out = pos + 4, []
    for c in range(cats):
        n = rom[q]
        q += 4
        for _ in range(n):
            out.append((c, rom[q:q + size]))
            q += size
    return out


def weapons(rom_path):
    """[dict] of the weapons the party can use (classes Hafted, Missile, Pole, Sword, Thrown), in database order."""
    rom = Path(rom_path).read_bytes()
    pos = _find_db(rom, 11, 48, 21, 21, {7})
    if pos is None:
        raise SystemExit(f"romdata.py: no weapon database in {rom_path}")
    out = []
    for c, r in _records(rom, pos, 11, 48):
        if WEAPON_CLASSES[c] not in ("Hafted", "Missile", "Pole", "Sword", "Thrown"):
            continue
        out.append({"name": r[:21].split(b"\0")[0].decode("ascii"), "id": r[21] | r[22] << 8,
                    "class": WEAPON_CLASSES[c], "strength": r[24], "hit": r[25], "damage": r[26],
                    "price": r[27] | r[28] << 8, "range": r[30],
                    "element": ELEMENTS[r[34]] if r[34] < len(ELEMENTS) else ""})
    return out


def creatures(rom_path):
    """[dict] of the creatures and people, in database order (slot): level and the two
    element resistances, as damage taken in percent (the game multiplies damage by the amount x 0.25:
    0 immune, 2 half, 4 normal, 5 a weakness). placeholder: an internal record (STER1505, STMI03) or a shopkeeper
    (GW Supply Shop #80), not worth listing."""
    rom = Path(rom_path).read_bytes()
    pos = _find_db(rom, 7, 136, 20, 41, {2})
    if pos is None:
        raise SystemExit(f"romdata.py: no creature database in {rom_path}")
    out = []
    for slot, (c, r) in enumerate(_records(rom, pos, 7, 136)):
        name = r[:20].split(b"\0")[0].decode("ascii")
        resists = []
        for k in (0x76, 0x78):
            e, amount = r[k], r[k + 1]
            if 0 < e < len(ELEMENTS):
                resists.append((ELEMENTS[e], amount * 25))
        out.append({"name": name, "slot": slot, "id": r[41] | r[42] << 8, "level": r[0x4D], "resists": resists,
                    "placeholder": (name.isupper() and any(ch.isdigit() for ch in name)) or "#" in name})
    return out


def _s8(v):
    return v - 256 if v > 127 else v


def name_is_placeholder(raw):
    """The unused records' names: "exp 12", "exp24", "key17"."""
    return re.fullmatch(rb"(exp|key) ?\d+", raw.split(b"\0")[0]) is not None


def armour(rom_path):
    """[dict] of the body armour and shields (decomp: include/armordb.h ArmorRom, 48 bytes, 2 categories), in
    database order. Bonuses: the stat and skill it raises or lowers, the spell it holds, the element it resists
    (damage taken in percent, as creatures')."""
    rom = Path(rom_path).read_bytes()
    pos = _find_db(rom, 2, 48, 23, 23, {5, 6})
    if pos is None:
        raise SystemExit(f"romdata.py: no armour database in {rom_path}")
    spell_names = {s["id"]: s["name"] for s in spells(rom_path)}
    out = []
    for c, r in _records(rom, pos, 2, 48):
        bonus = _bonuses(r, 35, 37, ((39, 41), (43, 45)), 46, spell_names)
        out.append({"name": r[:23].split(b"\0")[0].decode("ascii"), "id": r[23] | r[24] << 8,
                    "kind": "Shield" if c else "Armour", "defence": r[26], "protection": r[27], "dex": _s8(r[28]),
                    "stealth": _s8(r[30]), "placeholder": name_is_placeholder(r[:23]), "price": r[31] | r[32] << 8, "bonus": bonus})
    return out


# The item database's 11 categories, with the item type (high byte of the ID) of each.
GEAR_KINDS = [(1, "Other"), (9, "Helmet"), (10, "Cloak"), (11, "Gloves"), (12, "Ring"), (13, "Wand"), (14, "Belt"),
              (15, "Boots"), (17, "Scroll"), (18, "Key item"), (19, "Amulet")]


def _bonuses(r, stat, skill, spells_at, element, spell_names):
    """The stat and skill an item raises, the spells it holds (offset of the ID, offset of its level) and the
    element it guards against, as text."""
    out = []
    if r[stat] < len(STATS) and r[stat + 1]:
        out.append(f"{STATS[r[stat]]} {_s8(r[stat + 1]):+d}")
    if r[skill] < len(SKILLS) and r[skill + 1]:
        out.append(f"{SKILLS[r[skill]]} {_s8(r[skill + 1]):+d}")
    for k, lv in spells_at:
        spell = r[k] | r[k + 1] << 8
        if spell:
            out.append(f"{spell_names.get(spell, 'a spell')} {r[lv]}" if r[lv] else spell_names.get(spell, "a spell"))
    if 0 < r[element] < len(ELEMENTS):
        out.append(f"{ELEMENTS[r[element]]} {r[element + 1] * 25}%")
    return out


def gear(rom_path):
    """[dict] of the other equipment and items: helmets, cloaks, gloves, rings, wands, belts, boots, scrolls,
    amulets, key and other items (decomp: include/itemDB.h Gear_Rom, 44 bytes, 11 categories), in database order.
    Aidyn Plus has two scrolls with an empty ID (type 0, "` Dispel Necro"): allowed, as broken: True. It also starts
    names with a marker its font draws as a symbol ("~ Gwernia" a map, "* Bear Claw" a crafting part, "| Moon Gem" a
    wand, "^ Fireball" and "< Haste" scrolls by school): left out of name."""
    rom = Path(rom_path).read_bytes()
    pos = _find_db(rom, 11, 44, 20, 20, {t for t, _ in GEAR_KINDS} | {0})
    if pos is None:
        raise SystemExit(f"romdata.py: no item database in {rom_path}")
    spell_names = {s["id"]: s["name"] for s in spells(rom_path)}
    out = []
    for c, r in _records(rom, pos, 11, 44):
        name = r[:20].split(b"\0")[0].decode("ascii")
        out.append({"name": name.lstrip("`~*|^<>_ "), "id": r[20] | r[21] << 8, "kind": GEAR_KINDS[c][1], "broken": r[21] == 0,
                    "price": r[28] | r[29] << 8,
                    "bonus": [f"{label} {r[k]}" for k, label in ((25, "Protection"), (24, "Damage")) if r[k]]
                    + _bonuses(r, 31, 33, ((35, 37), (39, 41)), 42, spell_names),
                    "placeholder": name_is_placeholder(r[:20])})
    return out


# ---- Training costs and potions (tables in the game's code segment, not its databases) ----
# The patched ROMs (Aidyn Patch, Aidyn Plus) keep the 1.0 code's layout, so these sit at the same places in all three:
# a RAM address is ROM offset + 0x80000400 - 0x1000. Decomp: CharSkills::GetSkillXpMod / GetWeaponXpMod (skills.cpp,
# the XP to raise one a level: (level + 1)^2 x the multiplier), potion_prices (generic.cpp, PotionEnum order) and
# potionRecipies (crafting/potion.h: potion, spices, herbs, gemstones, Alchemist rank, 2 more).
POTIONS = ["Fire Flask", "Inferno Flask", "Sleep Gas", "Acid Flask", "Healing Potion", "Stamina Potion",
           "Curing Potion", "Antidote", "Restore Potion", "Strength Potion", "Dexterity Potion", "Aspect Potion",
           "Jumping Potion", "Clarity Potion", "Charisma Potion", "Defence Potion", "Stealth Potion"]


def _ram(rom, address, size):
    return rom[address - 0x80000400 + 0x1000:][:size]


def training(rom_path):
    """{"skills": {skill: XP multiplier}, "weapons": {weapon class: XP multiplier}}. A weapon class no trainer
    teaches has 0 in the original (the creatures' Bite, Claw, Sting...)."""
    rom = Path(rom_path).read_bytes()
    skills = struct.unpack(">12i", _ram(rom, 0x800D38D0, 48))
    weapons = struct.unpack(">11I", _ram(rom, 0x800D3900, 44))
    if not all(0 < s < 100000 for s in skills):
        raise SystemExit(f"romdata.py: no skill XP table where the 1.0 has it in {rom_path}")
    return {"skills": dict(zip(SKILLS, skills)), "weapons": dict(zip(WEAPON_CLASSES, weapons))}


def potions(rom_path):
    """[dict] per potion that can be brewed or bought: name, price, and its recipe (spices, herbs, gemstones, the
    Alchemist rank it needs), in PotionEnum order."""
    rom = Path(rom_path).read_bytes()
    prices = struct.unpack(">17H", _ram(rom, 0x800E2CB0, 34))
    recipes = {r[0]: r for r in (_ram(rom, 0x800D7960, 0x69)[i:i + 7] for i in range(0, 0x69, 7))}
    out = []
    for k, name in enumerate(POTIONS):
        if k in (11, 12):
            continue  # Aspect and Jumping: never in the game
        r = recipes.get(k)
        out.append({"name": name, "price": prices[k],
                    "recipe": {"spices": r[1], "herbs": r[2], "gemstones": r[3], "alchemist": r[4]} if r else None})
    return out
