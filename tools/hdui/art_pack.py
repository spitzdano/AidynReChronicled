#!/usr/bin/env python3
"""art_pack.py: the "rechronicled-art" texture pack: the redrawn portraits, item and slot icons and world textures
in art/, and the UI pieces hudart.py draws (the time-of-day dial, the pause pages' arrows, the HitPoints bar).

    tools/hdui/art_pack.py [--out DIR]      -> ~/.local/share/AidynReChronicled/texture-packs/rechronicled-art

Only the pictures in art/ (redone.py), cut the way the game loads them (portraits.py, romimages.py): the whole picture
at 8x, and every left/right/top/bottom part a page slides or a list scrolls through at 4x (2x for portraits outside
the party), each keyed by the RT64 hash of the ROM picture it replaces. A world texture (art/textures, named as
export_textures.py names them) replaces every load of it the game makes (romtextures.py, as world.py): each mip level
(box-filtered from the picture) and each scroll position of a moving one (the picture rotated), at 8x. The hashes need the ROM to work out, but nothing of the ROM is in the pack: it can ship
with a release, next to the game (texture-packs/), unlike hd-ui. It comes after hd-ui in settings.json's texture_packs,
so it wins where both have a picture.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_pack import PROJECT, image_key  # noqa: E402
import portraits  # noqa: E402
import portrait_states  # noqa: E402
import potions  # noqa: E402
import redone  # noqa: E402
import romimages  # noqa: E402


# The world textures (walls, floors, characters, items: drawn in 3D, filtered) are given RT64's "half" shift, as the
# HD world pack's are: RT64 then samples a replacement half a source texel further on, where the game's own filtering
# puts the texels. The pack's default stays "none", which its flat 2D pictures need (portraits, icons, words). Without
# it every painted texture sat half a texel off: at the foot of the starting forest's pine walls (tex03860), whose
# bottom row is a fringe of cut-out spikes, the cut-out rose above the ground and the sky showed as a pale wedge.


def _blur(a, sigma):
    import numpy as np
    r = max(1, int(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = np.apply_along_axis(lambda m: np.convolve(np.pad(m, r, mode="edge"), k, mode="valid"), 1, a)
    return np.apply_along_axis(lambda m: np.convolve(np.pad(m, r, mode="edge"), k, mode="valid"), 0, out)


ROUND_SIGMA = 7.0     # (pixels at 8x: about a texel) how far a corner of the game's 1-bit mask is rounded
ROUND_MAX_CHANGE = 0.02  # a cut-out whose smoothed outline would move more than this share of its area is left alone (leaves, fur, lace)


def round_cutout(img):
    """The painted picture of a cut-out (RGBA16 or palette: a shape with a sharp edge) with its outline rounded. The game's mask is 1 bit at the
    N64's own size (32 x 32, say), so scaled up its edge is a staircase: stairs a texel wide, many pixels each, and the painted alpha follows
    them. The shape is blurred and cut again at one half, which is a smooth outline of the same shape, and given an edge about two pixels soft.
    Left alone: a picture whose transparency is not just an edge (glass, smoke), and one where the rounding would move more than
    ROUND_MAX_CHANGE of the area (fine edges: foliage, hair, chains). Where the outline grows, the colour of the nearest painted pixel fills in."""
    import numpy as np
    alpha = img[..., 3]
    hard = alpha >= 128
    if hard.all() or not hard.any():
        return img
    if ((alpha > 20) & (alpha < 235)).sum() > 0.25 * hard.sum():  # (an edge's soft pixels are a thin ring: not a soft picture)
        return img
    blurred = _blur(hard.astype(np.float32), ROUND_SIGMA)
    smooth = blurred >= 0.5
    if (smooth != hard).sum() > ROUND_MAX_CHANGE * hard.sum():
        return img
    out = img.copy()
    have = hard.copy()
    colour = out[..., :3].astype(np.float32)
    for _ in range(12):  # colour bleeds outward a pixel at a time into what the smoothed edge newly covers
        need = (blurred > 0.2) & ~have
        if not need.any():
            break
        acc = np.zeros_like(colour)
        cnt = np.zeros(have.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            sh = np.roll(np.roll(have, dy, 0), dx, 1)
            acc += np.roll(np.roll(colour, dy, 0), dx, 1) * sh[..., None]
            cnt += sh
        fill = need & (cnt > 0)
        colour[fill] = acc[fill] / cnt[fill][:, None]
        have = have | fill
    out[..., :3] = np.clip(colour + 0.5, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip((blurred - 0.5) * 8.0 + 0.5, 0.0, 1.0).__mul__(255.0).round().astype(np.uint8)
    return out


def world_textures(rom_path, textures, out, tmemhash):
    """Replacements for the redrawn world textures ({ROM index: path}); returns rt64.json entries."""
    import tempfile
    import numpy as np
    import romassets
    import romtextures
    from pngio import write_png
    from world import box_down, hashes
    infos = {i: info for i, info in romtextures.textures(romassets.Rom(rom_path)) if i in textures}
    for index in sorted(set(textures) - set(infos)):
        print(f"art_pack.py: tex{index:05d}: not a world texture the game loads like the others (left out)")
    jobs, owners = [], []
    for index, info in infos.items():
        for level, (fmt, siz, line, words, w, h, tlut) in enumerate(info["tiles"]):
            for rotation, tm in (info["frames"] if level == 0 else [(0, info["tmem"])]):
                jobs.append((tm, fmt, siz, line, words, w, h, tlut))
                owners.append((index, w, h, rotation))
    with tempfile.TemporaryDirectory() as tmp:
        hs = hashes(tmemhash, jobs, tmp)
    entries, pictures = [], {}
    for h_, (index, lw, lh, rotation) in zip(hs, owners):
        info = infos[index]
        w0, h0 = info["w"], info["h"]
        if index not in pictures:
            W, H, rgba = redone.load(textures[index], w0, h0)
            img = np.frombuffer(bytes(rgba), np.uint8).reshape(H, W, 4).copy()
            if info["type"] == 0:  # RGBA16: 1-bit alpha, a cut-out
                img[..., 3] = np.where(img[..., 3] >= 128, 255, 0)
            if info["type"] in (0, 2):  # RGBA16 or palette: the cut-outs' outlines are rounded
                img = round_cutout(img)
            pictures[index] = img
        img = pictures[index]
        if (lw, lh) == (w0, h0):
            pic = np.roll(img, rotation * redone.SCALE, axis=1) if rotation else img
        elif w0 % lw == 0 and h0 % lh == 0:
            pic = box_down(img, lw * redone.SCALE, lh * redone.SCALE)
        else:
            continue
        write_png(out / f"{h_}.png", pic.shape[1], pic.shape[0], np.ascontiguousarray(pic).tobytes())
        entries.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "stream", "shift": "half"})
    print(f"art_pack.py: {len(infos)} world textures, {len(entries)} replacements")
    return entries


def big_pictures(rom_path, decomp, art, out, tmemhash):
    """Replacements for redrawn pictures too big for one load of TMEM (the pause pages' titles, the title card, and
    the ones hudart.py draws: the Options page's Sound and Music bars): the fixed strips the game draws them in
    (bigstrips.py), cut from the picture at 8x."""
    import bigstrips
    import hudart
    items = []
    for name, img in bigstrips.big_images(rom_path, decomp).values():
        if name in art:
            items.append((img, redone.load(art[name], img[1], img[2]), redone.SCALE))
        elif hudart.handles(name):
            items.append((img, hudart.redraw(name, img[1], img[2]), hudart.SCALE))
    return bigstrips.cut(out, tmemhash, items) if items else []


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path,
                    default=Path.home() / ".local/share/AidynReChronicled/texture-packs/rechronicled-art")
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    ap.add_argument("--no-world", action="store_true", help="leave out the world textures (a quick test pack)")
    args = ap.parse_args()
    tmemhash = PROJECT / "build/cmake/tmemhash"
    if not tmemhash.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    art = redone.collect()
    art = potions.write_art(art, PROJECT / "build/hdui/potions")  # each potion's colour (#25)
    if not art and not redone.collect_textures():
        sys.exit(f"art_pack.py: no pictures in {redone.ART}")
    final, out = args.out, args.out.with_name(args.out.name + ".new")
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    decomp = PROJECT / "decomp"
    # Cropped variants are drawn only while a page slides in or out: the party's (pause pages) at 4x, the rest at 2x,
    # which keeps the pack about a third of the size.
    party = {p.stem for p in (redone.ART / "portraits/party").glob("*.png")}
    entries = portraits.build(args.rom, decomp, tmemhash, None, None, out, redone.SCALE, 4, art=art, only_art=True,
                              crop_scale_of=lambda name: 4 if name in party else 2)
    entries += romimages.build(args.rom, decomp, tmemhash, None, None, out, 4, set(), image_key, art=art, only_art=True)
    entries += big_pictures(args.rom, decomp, art, out, tmemhash)
    entries += portrait_states.entries(args.rom, out, portrait_states.STATES, tmemhash)  # the party's damage portraits (art/portraits/party/states)
    textures = {} if args.no_world else redone.collect_textures()
    if textures:
        entries += world_textures(args.rom, textures, out, tmemhash)
    db = {
        "configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream",
                          "defaultShift": "none", "hashVersion": 5},
        "textures": entries, "operationFilters": [], "shiftFilters": [], "extraFiles": [],
    }
    (out / "rt64.json").write_text(json.dumps(db, indent=1) + "\n")
    # The pause pages painted whole, words and all (scroll_pages.py): src/game/page_words.cpp leaves their words to this
    # pack while it's on, instead of redrawing them over the game's own picture.
    pages = [name for name in ("PauseBGOptions", "PauseBGStats", "PauseBGMap") if name in art]
    # The map page's land pieces painted in colour (map_pieces.py): the game draws them untinted while the pack is on.
    if "MapPiece01" in art:
        pages.append("MapPieces")
    (out / "aidyn-pages.txt").write_text("".join(f"{name}\n" for name in pages))
    old = final.with_name(final.name + ".old")
    shutil.rmtree(old, ignore_errors=True)
    if final.exists():
        final.rename(old)
    out.rename(final)
    shutil.rmtree(old, ignore_errors=True)
    print(f"art_pack.py: {len(art)} pictures and {len(textures)} textures, {len(entries)} replacements in {final}")


if __name__ == "__main__":
    main()
