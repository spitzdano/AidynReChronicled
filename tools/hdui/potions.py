"""potions.py: a colour of its own for every potion's icon, and for scrolls and wands by their spell's school (#25).

The game draws every potion with one icon (IconItemPotion, 16x16 CI8: a glass bottle of red liquid). Here each potion
gets a copy with its palette recoloured: src/game/patches.cpp ("Potion colours") makes the copy in the game, and
romimages.py makes the same copies so the redrawn art pack replaces each with the painted bottle recoloured alike.
Healing keeps the original red.

The recolour (the same in C++, on the palette's 5-bit RGBA16, and here, on the painted picture's 8-bit RGBA): a colour
is its grey (min of green and blue) plus an amount of red (red minus that grey); where that amount is above zero it
becomes the same amount of the potion's colour instead. Glass and highlights, with no more red than green or blue,
stay as they are.

Scrolls and wands (IconItemScroll, IconItemWand: every one the same, a purple ribbon, a blue star) get their spell's
school colour the same way on their blue instead of their red ("blue" mode: the grey is min of red and green, the
amount blue minus that): Chaos red, Elemental amber, Naming green, Necromancy violet, Star blue.
"""
import numpy as np

# PotionEnum order (decomp potion.h); None: the original red.
COLOURS = [
    (255, 140, 30),   # Fire Flask: orange
    (255, 60, 20),    # Inferno Flask: fiery red-orange
    (190, 150, 255),  # Sleep Gas: lavender
    (170, 255, 40),   # Acid Flask: acid green
    None,             # Healing potion: red
    (255, 230, 60),   # Stamina: yellow
    (255, 110, 190),  # Curing: pink
    (60, 220, 90),    # Antidote: green
    (225, 235, 255),  # Restore: silvery white
    (205, 110, 45),   # Strength: amber brown
    (40, 230, 230),   # Dexterity: cyan
    (255, 200, 80),   # Aspect: gold
    (150, 255, 150),  # Jumping: pale green
    (110, 180, 255),  # Clarity: sky blue
    (240, 80, 255),   # Charisma: magenta
    (120, 140, 210),  # Defence: steel blue
    (95, 75, 140),    # Stealth: dusky violet
]
BASE = "IconItemPotion"

# MagicSchoolEnum order (decomp spells.h): Chaos, Elemental, Naming, Necromancy, NONE, Star.
SCHOOL_COLOURS = [(235, 45, 45), (255, 150, 30), (60, 210, 90), (150, 60, 210), None, (70, 120, 255)]
SCHOOL_BASES = ["IconItemScroll", "IconItemWand"]


def school_name(base, k):
    return f"{base}_s{k}"


def name(k):
    return f"{BASE}_{k}"


def recolour_rgba16(palette, colour, blue=False):
    """The CI8 palette (big-endian RGBA16 bytes) with its red (blue: its blue) recoloured to colour (8-bit RGB)."""
    t = [c >> 3 for c in colour]
    out = bytearray(palette)
    for i in range(len(palette) // 2):
        v = (palette[2 * i] << 8) | palette[2 * i + 1]
        r, g, b, a = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1
        grey = min(r, g) if blue else min(g, b)
        amt = (b if blue else r) - grey
        if amt <= 0:
            continue
        if blue:
            nr = min(31, r + (amt * t[0] + 15) // 31)
            ng = min(31, g + (amt * t[1] + 15) // 31)
            nb = min(31, grey + (amt * t[2] + 15) // 31)
        else:
            nr = min(31, grey + (amt * t[0] + 15) // 31)
            ng = min(31, g + (amt * t[1] + 15) // 31)
            nb = min(31, b + (amt * t[2] + 15) // 31)
        v = (nr << 11) | (ng << 6) | (nb << 1) | a
        out[2 * i], out[2 * i + 1] = v >> 8, v & 0xFF
    return bytes(out)


def recolour_rgba8(rgba, colour, blue=False):
    """An RGBA picture (bytes) with its red (blue: its blue) recoloured to colour, as recolour_rgba16 does."""
    px = np.frombuffer(bytes(rgba), np.uint8).reshape(-1, 4).astype(np.int32)
    r, g, b = px[:, 0], px[:, 1], px[:, 2]
    t = np.array(colour, np.int32)
    out = px.copy()
    if blue:
        grey = np.minimum(r, g)
        amt = np.maximum(b - grey, 0)
        out[:, 0] = np.where(amt > 0, np.minimum(255, r + (amt * t[0] + 127) // 255), r)
        out[:, 1] = np.where(amt > 0, np.minimum(255, g + (amt * t[1] + 127) // 255), g)
        out[:, 2] = np.where(amt > 0, np.minimum(255, grey + (amt * t[2] + 127) // 255), b)
    else:
        grey = np.minimum(g, b)
        amt = np.maximum(r - grey, 0)
        out[:, 0] = np.where(amt > 0, np.minimum(255, grey + (amt * t[0] + 127) // 255), r)
        out[:, 1] = np.where(amt > 0, np.minimum(255, g + (amt * t[1] + 127) // 255), g)
        out[:, 2] = np.where(amt > 0, np.minimum(255, b + (amt * t[2] + 127) // 255), b)
    return out.astype(np.uint8).tobytes()


def write_art(art, folder):
    """Adds the recoloured painted bottles to art ({name: path}), written into folder."""
    from pngio import read_png, write_png
    folder.mkdir(parents=True, exist_ok=True)
    if BASE in art:
        w, h, rgba = read_png(art[BASE])
        for k, colour in enumerate(COLOURS):
            if colour is None:
                continue
            path = folder / f"{name(k)}.png"
            write_png(path, w, h, recolour_rgba8(rgba, colour))
            art[name(k)] = path
    for base in SCHOOL_BASES:
        if base not in art:
            continue
        w, h, rgba = read_png(art[base])
        for k, colour in enumerate(SCHOOL_COLOURS):
            if colour is None:
                continue
            path = folder / f"{school_name(base, k)}.png"
            write_png(path, w, h, recolour_rgba8(rgba, colour, blue=True))
            art[school_name(base, k)] = path
    return art
