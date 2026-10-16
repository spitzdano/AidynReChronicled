#!/usr/bin/env python3
"""steam_art.py: Steam library art for the game as a non-Steam shortcut -> build/ally/steam-art.

    tools/ally/steam_art.py [--scene PNG]

  logo.png      the title card from the ROM (BORG8_TitleCard), upscaled 4x with Real-ESRGAN, its TM redrawn in the
                text font and "Rechronicled" added where the title screen draws it (as the HD UI pack and the title
                screen show it); transparent
  hero.png      3840x1240: the scene, the band around its middle (Steam lays the logo over it)
  capsule.png   600x900 (library grid): the scene with the logo
  wide.png      920x430 (recent games, wide capsule): the scene with the logo
  icon.png      256x256: an illuminated A like the manual's initials (tools/manual/initial.svg, Marcellus SC)

--icon PATH writes only the icon (it is drawn from the manual's tile and font, nothing from the ROM: a release
ships it). --scene is a 1920x1080 screenshot without HUD; the default is build/ally/scene.png, the castle gate from the
opening cinematic (capture it with tools/dev/pad.sh: New Game, keep the name, then shots during the intro). Everything
here is made from the ROM, so it stays in build/.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools/hdui"))
sys.path.insert(0, str(PROJECT / "tools/manual"))
import repaint  # noqa: E402
import romassets  # noqa: E402
from build_manual import toolkit  # noqa: E402
from pngio import bleed, decode_borg8, read_png, write_png  # noqa: E402

FONT = PROJECT / "tools/hdui/fonts/MarcellusSC-Regular.ttf"
TM_BOX = (174, 131, 13, 8)          # tools/hdui/labels.json, the title card's "marks"
RECHRONICLED = (97 - 48, 139)       # src/game/patches.cpp: "Rechronicled" at card texel (49, 139)
SCALE = 4


def magick(*args):
    subprocess.run(["magick", *map(str, args)], check=True)


def logo(rom_path, out, tmp):
    rom = romassets.Rom(rom_path)
    index = {n: i for i, n in romassets.borg8_names(PROJECT / "decomp").items()}["TitleCard"]
    fmt, w, h, pixels, palette = rom.image(index)
    rgba = decode_borg8(fmt, w, h, pixels, palette)
    img = {"w": w, "h": h, "rgba": rgba}
    colour = repaint.mark_colour(img, TM_BOX)
    rgba = bleed(w, h, repaint.clear(img, [TM_BOX]))
    write_png(tmp / "card.png", w, h, rgba)
    esrgan = toolkit() / "opt/realesrgan/realesrgan-ncnn-vulkan"
    subprocess.run([str(esrgan), "-i", str(tmp / "card.png"), "-o", str(tmp / "card4.png"), "-n",
                    "realesrgan-x4plus-anime", "-s", str(SCALE), "-f", "png"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    W, H, big = read_png(tmp / "card4.png")
    big = repaint.draw_mark(W, H, big, TM_BOX, "TM", colour, FONT, SCALE)
    write_png(tmp / "card4tm.png", W, H, big)
    branding = PROJECT / "build/branding/rechronicled@8x.png"  # 8x of 96x16 texels (tools/branding/make_branding.py)
    if not branding.exists():
        sys.exit("steam_art.py: build/branding/rechronicled@8x.png missing: run tools/branding/make_branding.py")
    x, y = RECHRONICLED
    magick(tmp / "card4tm.png", "(", branding, "-resize", "50%", ")", "-geometry", f"+{x * SCALE}+{y * SCALE}",
           "-composite", "-trim", "+repage", out)


def icon(path):
    """256x256: the manual's gold tile with an A in Marcellus SC."""
    magick("-background", "none", "-density", "288", HERE.parent / "manual/initial.svg", "-resize", "256x256!",
           "(", "-background", "none", "-fill", "#fff6d6", "-stroke", "#7a4e0c", "-strokewidth", "3",
           "-font", FONT, "-pointsize", "200", "label:A", "-trim", "+repage", "-resize", "x168", ")",
           "-gravity", "center", "-geometry", "+0+4", "-composite",
           "-bordercolor", "#9a6a12", "-border", "6", "-resize", "256x256!", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--scene", type=Path, default=PROJECT / "build/ally/scene.png")
    ap.add_argument("--rom", type=Path, default=PROJECT / "baserom.z64")
    ap.add_argument("--icon", type=Path, help="only write the icon, to this path")
    args = ap.parse_args()
    if args.icon:
        icon(args.icon)
        print(f"steam_art.py: {args.icon}")
        return
    if not args.scene.exists():
        sys.exit(f"steam_art.py: no scene at {args.scene} (see --help)")
    out = PROJECT / "build/ally/steam-art"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        logo(args.rom, out / "logo.png", tmp)
        scene = args.scene
        # Hero: a 31:10 band of the scene, a little above the middle, at Steam's 3840x1240.
        magick(scene, "-gravity", "center", "-crop", "1920x620+0-60", "+repage", "-filter", "Lanczos", "-resize",
               "3840x1240!", out / "hero.png")
        # Capsules: the scene, darkened towards the logo, with the logo on top.
        magick(scene, "-gravity", "center", "-crop", "720x1080+0+0", "+repage", "-resize", "600x900!",
               "(", "-size", "600x900", "gradient:#000000c0-#00000030", ")", "-composite",
               "(", out / "logo.png", "-resize", "540x", ")", "-gravity", "north", "-geometry", "+0+70", "-composite",
               out / "capsule.png")
        magick(scene, "-gravity", "center", "-crop", "1920x897+0+0", "+repage", "-resize", "920x430!",
               "(", "-size", "920x430", "radial-gradient:#00000090-#00000020", ")", "-composite",
               "(", out / "logo.png", "-resize", "x380", ")", "-gravity", "center", "-composite", out / "wide.png")
        icon(out / "icon.png")
    for f in sorted(out.iterdir()):
        print(f"steam_art.py: {f}")


if __name__ == "__main__":
    main()
