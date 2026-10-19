#!/usr/bin/env python3
"""steam_art.py: Steam library art for the game as a non-Steam shortcut -> build/ally/steam-art.

    tools/ally/steam_art.py [--scene PNG]

  logo.png      Dan's logo (art/titles/source/aidyn-chronicles-logo-redesign.png, RECHRONICLED included), trimmed and
                fitted into 1280x720; transparent
  hero.png      3840x1240: a band of the scene, around the castle (Steam lays the logo over it)
  capsule.png   600x900 (library grid): the scene around the castle, darker towards the logo, with the logo
  wide.png      920x430 (recent games, wide capsule): the scene with the logo
  icon.png      256x256: an illuminated A like the manual's initials (tools/manual/initial.svg, Marcellus SC)

--icon PATH writes only the icon. --scene is a screenshot without HUD or title (a wide one, 64:27 or so); the default
is build/ally/scene.png, Gwernia Castle on the sea from the title flyover (about 90 s in). To capture one: a throwaway
RT64 pack, listed last in a scratch settings.json, that replaces the title card's strips, its shadow's and every
picture of the rechronicled pack with a clear 4x4 PNG (hashes: bigstrips.jobs + portraits.hashes, and the rechronicled
pack's rt64.json), then tools/dev/pad.sh shots once a second, keeping a frame between "Press Start"'s blinks. The scene
comes from the game, so it and the art stay in build/; the logo is Dan's own and is in the repository.
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


LOGO = PROJECT / "art/titles/source/aidyn-chronicles-logo-redesign.png"
FOCUS_X = 0.43   # the castle gate's place across the scene (the capsules are centred on it)


def logo(out):
    magick(LOGO, "-trim", "+repage", "-resize", "1280x720", out)


def grade(scene, out, w, h, x_off):
    """The scene cropped to w:h around FOCUS_X, a little more contrast and saturation, and a soft vignette."""
    magick(scene, "-gravity", "center", "-crop", f"{w}x{h}{x_off:+d}+0", "+repage",
           "-modulate", "100,112,100", "-sigmoidal-contrast", "3,50%",
           "(", "+clone", "-fill", "black", "-colorize", "100", "-fill", "white", "-draw",
           f"ellipse {w // 2},{h // 2} {int(w * 0.62)},{int(h * 0.7)} 0,360", "-blur", f"0x{max(w, h) // 8}", ")",
           "-compose", "multiply", "-composite", out)


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
    W, H = (int(v) for v in subprocess.run(["magick", "identify", "-format", "%w %h", str(args.scene)],
                                           capture_output=True, text=True, check=True).stdout.split())
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        logo(out / "logo.png")
        # Hero: a 31:10 band, the castle's walls and gate (a little above the middle), at Steam's 3840x1240.
        bh = round(W * 1240 / 3840)
        magick(args.scene, "-gravity", "north", "-crop", f"{W}x{bh}+0+{max(0, (H - bh) // 3)}", "+repage",
               "-modulate", "100,112,100", "-sigmoidal-contrast", "3,50%", "-filter", "Lanczos", "-resize",
               "3840x1240!", out / "hero.png")
        # Capsule: portrait around the gate, darker at the top for the logo.
        cw = round(H * 600 / 900)
        x_off = round(W * FOCUS_X - W / 2)
        grade(args.scene, tmp / "c.png", cw, H, x_off)
        magick(tmp / "c.png", "-resize", "600x900!",
               "(", "-size", "600x900", "gradient:#000000b0-#00000000", ")", "-composite",
               "(", out / "logo.png", "-resize", "520x", ")", "-gravity", "north", "-geometry", "+0+36", "-composite",
               out / "capsule.png")
        # Wide capsule: the scene with the logo on the left third.
        ww = min(W, round(H * 920 / 430))
        grade(args.scene, tmp / "w.png", ww, H, x_off + round(ww * 0.12))
        magick(tmp / "w.png", "-resize", "920x430!",
               "(", "-size", "430x920", "gradient:#000000c0-#00000000", "-rotate", "-90", ")", "-composite",
               "(", out / "logo.png", "-resize", "x390", ")", "-gravity", "west", "-geometry", "+24+0", "-composite",
               out / "wide.png")
        icon(out / "icon.png")
    for f in sorted(out.iterdir()):
        print(f"steam_art.py: {f}")


if __name__ == "__main__":
    main()
