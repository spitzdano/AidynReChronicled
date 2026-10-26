"""title_panel.py: the title menu's panel and the achievement toast, in the combat turn order bar's style: eight pieces
painted by Codex from spitzdano's turn bar (art/hud/source/title-panel; its HANDOFF.txt has the joins), for
make_branding.py, which builds each into the game (RGBA16, at most one 4 KB load of TMEM) with its full-resolution
version in the rechronicled pack.

At 8 pixels a texel (make_branding.py's SS) the pieces are whole texels as painted, but for two:
  tp_cap_l_plate  300 x 240 painted: 2 transparent pixels each side (38 x 30 texels)
  tp_body         512 x 512 painted: halved to 256 (32 x 32 texels: 64 x 64 would be 8 KB of TMEM)
The bars are 240 pixels (30 texels) tall, the painted bar on rows 14..226; their joins match pixel for pixel, so the
game lays caps and middles edge to edge (src/game/patches.cpp, "Title panel").
"""
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
from pngio import read_png  # noqa: E402

SOURCE = Path(__file__).resolve().parents[2] / "art/hud/source/title-panel"
S = 8


def _load(name):
    w, h, rgba = read_png(SOURCE / f"{name}.png")
    a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32) / 255
    a[..., :3] *= a[..., 3:]  # premultiplied
    return a


def _piece(name, pad=0, half=False):
    def draw(w, h):
        a = _load(name)
        if half:
            a = a.reshape(a.shape[0] // 2, 2, a.shape[1] // 2, 2, 4).mean(axis=(1, 3))
        if pad:
            a = np.pad(a, ((0, 0), (pad, pad), (0, 0)))
        assert a.shape[:2] == (h * S, w * S), (name, a.shape, (h * S, w * S))
        return a
    return draw


IMAGES = [("tp_cap_l", 25, 30, _piece("cap_left")),
          ("tp_cap_l_plate", 38, 30, _piece("cap_left_plate", pad=2)),
          ("tp_cap_r", 25, 30, _piece("cap_right")),
          ("tp_bar_mid", 64, 30, _piece("bar_mid")),
          ("tp_body", 32, 32, _piece("body_fill", half=True)),
          ("tp_rail_l", 5, 64, _piece("rail_left")),
          ("tp_rail_r", 5, 64, _piece("rail_right")),
          ("tp_medal", 20, 20, _piece("medal"))]
