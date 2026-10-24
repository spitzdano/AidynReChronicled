#!/usr/bin/env python3
"""map_pieces.py: the world map page's 18 land pieces (#26), cut from one painting of all of them.

    tools/hdui/map_pieces.py [--rom ROM]

The map page shows the land in pieces, each revealed when the party finds that region's map: 18 small Borg8 drawings
(MapPiece01..18 and MapPieceGob) placed at the positions in the decomp's WidgetMenuWorldMap (WorldMap.cpp), on the
painted parchment (art/pages, PauseBGMap). art/pages/map/source/map-land.png paints them all at once, 8x the page
(2560x1920), in place, transparent elsewhere (Codex, from the brief in the project's notes). This cuts it into one
picture per piece, art/pages/map/<name>.png at 8x the piece's size, which art_pack.py then puts in the pack.

Pieces overlap, so each painted pixel is shared out rather than given whole to every piece that covers it (the ink is
mostly soft, semi-transparent wash: two copies drawn over each other would darken it): mostly to the piece whose own
original drawing (from the ROM) is nearest, up to 6 N64 pixels away, fading to the next one over about one N64
pixel where they meet, so a piece found on its own ends in a soft edge, not a step. Ink farther from every piece is
left out. The game tints its grey pieces brown; with these painted ones on, src/game/patches.cpp draws
them untinted (art_pack.py lists MapPieces in the pack's aidyn-pages.txt).
"""
import argparse
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(PROJECT / "tools/manual"))
import romart  # noqa: E402
import romassets  # noqa: E402
from pngio import decode_borg8, read_png, write_png  # noqa: E402

SCALE = 8
REACH = 6 * SCALE  # how far from a piece's original strokes its painted ink may go
FEATHER = 8.0      # where pieces meet, the ink fades from one to the other over about this many pixels (1 N64 pixel)
MAP = PROJECT / "art/pages/map"
# WorldMapPieces in the decomp's WorldMap.cpp: the piece and its place on the page (N64 pixels).
PIECES = [("MapPiece01", 58, 29), ("MapPiece02", 35, 29), ("MapPiece04", 35, 29), ("MapPiece05", 43, 58),
          ("MapPiece06", 60, 74), ("MapPiece07", 35, 70), ("MapPiece08", 56, 157), ("MapPiece09", 32, 143),
          ("MapPiece10", 59, 69), ("MapPiece11", 94, 62), ("MapPiece12", 111, 70), ("MapPiece13", 111, 74),
          ("MapPiece14", 110, 133), ("MapPiece15", 214, 117), ("MapPiece16", 120, 161), ("MapPieceGob", 128, 135),
          ("MapPiece17", 102, 166), ("MapPiece18", 106, 164)]


def dilate(mask):
    """One step of 8-neighbour growth."""
    out = mask.copy()
    out[1:, :] |= mask[:-1, :]
    out[:-1, :] |= mask[1:, :]
    out[:, 1:] |= mask[:, :-1]
    out[:, :-1] |= mask[:, 1:]
    out[1:, 1:] |= mask[:-1, :-1]
    out[:-1, :-1] |= mask[1:, 1:]
    out[1:, :-1] |= mask[:-1, 1:]
    out[:-1, 1:] |= mask[1:, :-1]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    args = ap.parse_args()
    w, h, rgba = read_png(MAP / "source/map-land.png")
    land = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4)
    rom = romassets.Rom(args.rom)
    names = romart.names(PROJECT / "decomp")

    # Each piece's distance (in 8-neighbour steps) from its own strokes, out to REACH, inside its rectangle.
    pieces = []
    for name, x, y in PIECES:
        fmt, pw, ph, pixels, palette = rom.image(names[name])
        alpha = np.frombuffer(bytes(decode_borg8(fmt, pw, ph, pixels, palette)), np.uint8).reshape(ph, pw, 4)[..., 3]
        X, Y, W, H = x * SCALE, y * SCALE, pw * SCALE, ph * SCALE
        hh, ww = min(H, h - Y), min(W, w - X)
        grown = np.kron(alpha > 0, np.ones((SCALE, SCALE), bool))[:hh, :ww]
        dist = np.full((hh, ww), np.inf, np.float32)
        for d in range(REACH + 1):
            dist[grown & np.isinf(dist)] = d
            grown = dilate(grown)
        pieces.append((name, X, Y, W, H, dist))
    # Soft ownership: weight exp(-distance / FEATHER) shared out over the pieces that reach a pixel (summing to 1),
    # so where two pieces meet the ink fades from one to the other over about one N64 pixel instead of a hard step.
    total = np.zeros((h, w), np.float32)
    for name, X, Y, W, H, dist in pieces:
        total[Y:Y + dist.shape[0], X:X + dist.shape[1]] += np.exp(-dist / FEATHER)
    inked = land[..., 3] > 0
    lost = int((inked & (total == 0)).sum())
    print(f"map_pieces.py: {inked.sum()} painted pixels, {lost} farther than {REACH // SCALE} N64 pixels from every "
          f"piece left out")
    for name, X, Y, W, H, dist in pieces:
        hh, ww = dist.shape
        weight = np.exp(-dist / FEATHER) / np.maximum(total[Y:Y + hh, X:X + ww], 1e-9)
        out = np.zeros((H, W, 4), np.uint8)
        piece = land[Y:Y + hh, X:X + ww].copy()
        piece[..., 3] = np.round(piece[..., 3] * weight).astype(np.uint8)
        out[:hh, :ww] = piece
        write_png(MAP / f"{name}.png", W, H, out.tobytes())
        print(f"  {name}: {W}x{H} at ({X}, {Y}), {(out[..., 3] > 0).sum()} painted pixels")


if __name__ == "__main__":
    main()
