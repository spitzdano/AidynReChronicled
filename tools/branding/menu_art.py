"""menu_art.py: the pause menu's inventory, spells and skills pieces painted by Codex (art/menu, 2026-11-03; its BRIEF.md), cut into the
game's pictures for make_branding.py: each is at most one 4 KB load of TMEM, with its full-resolution version in the rechronicled pack.

  mc_<column>_<row>  MenuCard (132 x 138 texels, 1056 x 1104 painted) in 3 x 3 tiles of 44 x 46 texels: the description card that
                     takes the paper doll's place (page texels x 20..152, y 72..210; src/game/patches.cpp, "Details card")
  mc_wear            MenuWearSlot, 12 x 12 texels: a worn item's small plate along the card's foot
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
