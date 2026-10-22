#!/usr/bin/env python3
"""steam_art.py: Steam library art for the game as a non-Steam shortcut -> build/ally/steam-art.

    tools/ally/steam_art.py [--icon PATH]

Made from art/steam (the art repository: the box painting without its title, redrawn for each of Steam's shapes) and
spitzdano's title card (art/titles/source/aidyn-chronicles-logo-redesign.png, RECHRONICLED included):
  capsule.png   600x900 (library grid): the portrait painting, the title card at the top left as on the box
  wide.png      920x430 (recent games, wide capsule): the landscape painting, the title card left of the heroes
  hero.png      3840x1240: the hero painting as it is (Steam lays logo.png over it)
  logo.png      the title card, trimmed and fitted into 1280x720; transparent
  icon.png      256x256: the swordsman (art/steam/icon.png)
The title card gets a soft shadow so it reads against the bright sky. tools/ally/steam_shortcut.py --art
build/ally/steam-art puts them into Steam.

--icon PATH writes only the release's app icon: an illuminated A like the manual's initials (tools/manual/initial.svg,
Marcellus SC), made from fonts, so it can ship anywhere.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
FONT = PROJECT / "tools/hdui/fonts/MarcellusSC-Regular.ttf"


def magick(*args):
    subprocess.run(["magick", *map(str, args)], check=True)


LOGO = PROJECT / "art/titles/source/aidyn-chronicles-logo-redesign.png"
SRC = PROJECT / "art/steam"


def title_card(out, width):
    """The title card at that width with a soft dark shadow around it, on transparency."""
    magick(LOGO, "-trim", "+repage", "-resize", f"{width}x", "(", "+clone", "-background", "black",
           "-shadow", "70x18+0+10", ")", "+swap", "-background", "none", "-layers", "merge", "+repage", out)


def icon(path):
    """256x256: the manual's gold tile with an A in Marcellus SC."""
    magick("-background", "none", "-density", "288", HERE.parent / "manual/initial.svg", "-resize", "256x256!",
           "(", "-background", "none", "-fill", "#fff6d6", "-stroke", "#7a4e0c", "-strokewidth", "3",
           "-font", FONT, "-pointsize", "200", "label:A", "-trim", "+repage", "-resize", "x168", ")",
           "-gravity", "center", "-geometry", "+0+4", "-composite",
           "-bordercolor", "#9a6a12", "-border", "6", "-resize", "256x256!", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--icon", type=Path, help="only write the release's app icon (the illuminated A), to this path")
    args = ap.parse_args()
    if args.icon:
        icon(args.icon)
        print(f"steam_art.py: {args.icon}")
        return
    for name in ("portrait.png", "landscape.png", "hero.png", "icon.png"):
        if not (SRC / name).exists():
            sys.exit(f"steam_art.py: no {SRC / name} (the art repository: git submodule update --init art)")
    out = PROJECT / "build/ally/steam-art"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        # Made at the paintings' own size (1200x1800, 1840x860), then scaled down, for a sharper card.
        title_card(tmp / "card.png", 760)
        magick(SRC / "portrait.png", tmp / "card.png", "-gravity", "northwest", "-geometry", "+36+40", "-composite",
               "-filter", "Lanczos", "-resize", "600x900", "-strip", out / "capsule.png")
        magick(SRC / "landscape.png", tmp / "card.png", "-gravity", "northwest", "-geometry", "+250+40", "-composite",
               "-filter", "Lanczos", "-resize", "920x430", "-strip", out / "wide.png")
        magick(SRC / "hero.png", "-strip", out / "hero.png")
        magick(LOGO, "-trim", "+repage", "-resize", "1280x720", out / "logo.png")
        magick(SRC / "icon.png", "-filter", "Lanczos", "-resize", "256x256", "-strip", out / "icon.png")
    for f in sorted(out.iterdir()):
        print(f"steam_art.py: {f}")


if __name__ == "__main__":
    main()
