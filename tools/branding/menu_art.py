"""menu_art.py: the pause menu's inventory, spells and skills pieces painted by Codex (art/menu, 2026-11-03; its BRIEF.md), cut into the
game's pictures for make_branding.py: each is at most one 4 KB load of TMEM, with its full-resolution version in the rechronicled pack.

  mc_<column>_<row>  MenuCard (132 x 138 texels, 1056 x 1104 painted) in 3 x 3 tiles of 44 x 46 texels: the description card that
                     takes the paper doll's place (page texels x 20..152, y 72..210; src/game/patches.cpp, "Details card")
  mc_wear            MenuWearSlot, 12 x 12 texels: a worn item's small plate along the card's foot
  mt_off, mt_on      MenuTabOff / MenuTabOn, 16 x 14: the category and school tabs; mt_rule MenuTabRule, 120 x 4: the line under them
  mc_chip, mc_chip_on  MenuChip / MenuChipOn, 40 x 8: the Sort and Details plates (a round slot at the left for the button)
  mc_heading         MenuHeading, 120 x 6: the rule under a group's name; mc_hl MenuRowHighlight, 120 x 12: the highlighted row's wash
  mc_track, mc_thumb_t / _m / _b  the scroll bar: MenuScrollTrack 3 x 100, MenuScrollThumb cut into its 1 texel caps and a middle
  mc_pagebar         MenuPageBar, 56 x 6: the page turn bar, beside the right stick's two icons
At 8 pixels a texel (make_branding.py's SS) every piece is whole texels as painted.
"""
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
from pngio import read_png  # noqa: E402

SOURCE = Path(__file__).resolve().parents[2] / "art/menu"
S = 8
CARD_W, CARD_H, TILE_W, TILE_H = 132, 138, 44, 46


def _load(name):
    w, h, rgba = read_png(SOURCE / f"{name}.png")
    a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32) / 255
    a[..., :3] *= a[..., 3:]  # premultiplied
    return a


def _tile(i, j):
    def draw(w, h):
        a = _load("MenuCard")
        assert a.shape[:2] == (CARD_H * S, CARD_W * S), a.shape
        t = a[j * TILE_H * S:(j + 1) * TILE_H * S, i * TILE_W * S:(i + 1) * TILE_W * S]
        assert t.shape[:2] == (h * S, w * S), (t.shape, (h * S, w * S))
        return t
    return draw


def _whole(name):
    def draw(w, h):
        a = _load(name)
        assert a.shape[:2] == (h * S, w * S), (name, a.shape, (h * S, w * S))
        return a
    return draw


IMAGES = [(f"mc_{i}_{j}", TILE_W, TILE_H, _tile(i, j)) for j in range(3) for i in range(3)]
IMAGES += [("mc_wear", 12, 12, _whole("MenuWearSlot"))]
IMAGES += [("mt_off", 16, 14, _whole("MenuTabOff")), ("mt_on", 16, 14, _whole("MenuTabOn")), ("mt_rule", 120, 4, _whole("MenuTabRule")),
           ("mc_chip", 40, 8, _whole("MenuChip")), ("mc_chip_on", 40, 8, _whole("MenuChipOn")),
           ("mc_heading", 120, 6, _whole("MenuHeading")), ("mc_hl", 120, 12, _whole("MenuRowHighlight")),
           ("mc_track", 3, 100, _whole("MenuScrollTrack")), ("mc_pagebar", 56, 6, _whole("MenuPageBar"))]


def _thumb(y0, y1):
    def draw(w, h):
        a = _load("MenuScrollThumb")
        t = a[y0 * S:y1 * S]
        assert t.shape[:2] == (h * S, w * S), (t.shape, (h * S, w * S))
        return t
    return draw


IMAGES += [("mc_thumb_t", 3, 1, _thumb(0, 1)), ("mc_thumb_m", 3, 28, _thumb(1, 29)), ("mc_thumb_b", 3, 1, _thumb(29, 30))]
