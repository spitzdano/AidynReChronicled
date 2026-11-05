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
v2 (2026-11-05, BRIEF-v2.md; the card is the v2 card, the rest are new):
  mc_iconplate       MenuIconPlate, 28 x 28: under the card's big icon; mc_stat, mc_stat_on  MenuStatPlate / On, 22 x 21: an attribute column
  mc_bar_track, mc_bar_ticks  the skill level bar, 21 x 3; bf_l / bf_m its fill: a 1 texel cap (the same at both ends) and the plain middle
  mc_hint_l / _m / _r  MenuHintPlate cut into 8 texel caps and the middle (144 x 12): the plate behind a row of button prompts
  mc_tabw_off, mc_tabw_on  MenuTabWideOff / On, 28 x 14: the tabs with a word; mk_coin, mk_xp, mk_use  8 x 8 marks; mc_empty 48 x 14
  mp_nw .. mp_se     MenuPopup (96 x 64) as a nine-patch: 16 texel corners that never stretch, edges and middle that do
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
def _chip(name):
    """MenuChip / MenuChipOn without the round slot Codex painted at the left end: the game's own button icon (a round picture) is drawn
    there, and the two circles never lined up (spitzdano, 2026-11-04). The slot (centre 32, 32 of 320 x 64 px, radius about 25) is
    filled from the plain paper 58 pixels to its right, which keeps the grain and the vertical shading."""
    def draw(w, h):
        a = _load(name)
        ys, xs = np.mgrid[0:a.shape[0], 0:a.shape[1]]
        inside = (xs - 32.0) ** 2 + (ys - 32.0) ** 2 <= 27.0 ** 2
        src = np.roll(a, -58, axis=1)
        # soften the join: blend over the last 2 pixels of the disc
        d = np.sqrt((xs - 32.0) ** 2 + (ys - 32.0) ** 2)
        t = np.clip((27.0 - d) / 2.0, 0.0, 1.0)[..., None]
        out = a * (1.0 - t) + src * t
        assert out.shape[:2] == (h * S, w * S)
        return out.astype(np.float32)
    return draw


IMAGES += [("mt_off", 16, 14, _whole("MenuTabOff")), ("mt_on", 16, 14, _whole("MenuTabOn")), ("mt_rule", 120, 4, _whole("MenuTabRule")),
           ("mc_chip", 40, 8, _chip("MenuChip")), ("mc_chip_on", 40, 8, _chip("MenuChipOn")),
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


def _part(name, x0, y0, x1, y1):
    """The part of a painted picture from pixel (x0, y0) to (x1, y1)."""
    def draw(w, h):
        a = _load(name)[y0:y1, x0:x1]
        assert a.shape[:2] == (h * S, w * S), (name, a.shape, (h * S, w * S))
        return a
    return draw


IMAGES += [("mc_iconplate", 28, 28, _whole("MenuIconPlate")), ("mc_stat", 22, 21, _whole("MenuStatPlate")), ("mc_stat_on", 22, 21, _whole("MenuStatPlateOn")),
           ("mc_bar_track", 21, 3, _whole("MenuBarTrack")), ("mc_bar_ticks", 21, 3, _whole("MenuBarTicks")),
           ("bf_l", 1, 3, _part("MenuBarFill", 0, 0, 8, 24)), ("bf_m", 19, 3, _part("MenuBarFill", 8, 0, 160, 24)),
           ("mc_hint_l", 8, 12, _part("MenuHintPlate", 0, 0, 64, 96)), ("mc_hint_m", 144, 12, _part("MenuHintPlate", 64, 0, 1216, 96)),
           ("mc_hint_r", 8, 12, _part("MenuHintPlate", 1216, 0, 1280, 96)),
           ("mc_tabw_off", 28, 14, _whole("MenuTabWideOff")), ("mc_tabw_on", 28, 14, _whole("MenuTabWideOn")),
           ("mk_coin", 8, 8, _whole("MarkCoin")), ("mk_xp", 8, 8, _whole("MarkXP")), ("mk_use", 8, 8, _whole("MarkUse")),
           ("mc_empty", 48, 14, _whole("MenuEmpty"))]
_X, _Y = (0, 128, 640, 768), (0, 128, 384, 512)
for _j, _row in enumerate(("n", "", "s")):
    for _i, _col in enumerate(("w", "", "e")):
        _name = "mp_" + (_row + _col if _row + _col else "c")
        IMAGES.append((_name, (_X[_i + 1] - _X[_i]) // S, (_Y[_j + 1] - _Y[_j]) // S, _part("MenuPopup", _X[_i], _Y[_j], _X[_i + 1], _Y[_j + 1])))
