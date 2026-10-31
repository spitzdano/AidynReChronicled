#!/usr/bin/env python3
"""steam_art.py: Steam library art for the game as a non-Steam shortcut -> build/ally/steam-art.

    tools/ally/steam_art.py [--icon PATH]

Made from art/steam (the art repository: the box painting without its title, redrawn for each of Steam's shapes) and
spitzdano's title card (art/titles/source/aidyn-rechronicled-logo.png):
  capsule.png   600x900 (library grid): the portrait painting, the title card at the top left as on the box
  wide.png      920x430 (recent games, wide capsule): the landscape painting, the title card left of the heroes
  hero.png      3840x1240: the hero painting as it is (Steam lays logo.png over it)
  logo.png      the title card, trimmed and fitted into 1280x720; transparent
  icon.png      256x256: the swordsman (art/steam/icon.png)
  ../github-preview.jpg  1280x640 (beside the folder, not shipped): the landscape with the title card, for the GitHub
                repository's social preview
                (Settings > General > Social preview: uploaded by hand, the API can't)
The title card gets a soft shadow so it reads against the bright sky. tools/ally/steam_shortcut.py --art
build/ally/steam-art puts them into Steam.

--icon PATH writes only the icon, to that path: the release's game/icon.png (the app menu's and the Steam shortcut's).
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]


def magick(*args):
    subprocess.run(["magick", *map(str, args)], check=True)


LOGO = PROJECT / "art/titles/source/aidyn-rechronicled-logo.png"
SRC = PROJECT / "art/steam"


def title_card(out, width):
    """The title card at that width with a soft dark shadow around it, on transparency."""
    magick(LOGO, "-trim", "+repage", "-resize", f"{width}x", "(", "+clone", "-background", "black",
           "-shadow", "70x18+0+10", ")", "+swap", "-background", "none", "-layers", "merge", "+repage", out)


def icon(path):
    """256x256: the swordsman from the box painting."""
    magick(SRC / "icon.png", "-filter", "Lanczos", "-resize", "256x256", "-strip", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--icon", type=Path, help="only write the icon (the swordsman), to this path")
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
        icon(out / "icon.png")
        # GitHub's social preview is 2:1: the landscape (1840x860) cropped to 1720x860, the heroes kept at the right.
        magick(SRC / "landscape.png", "-crop", "1720x860+60+0", "+repage", tmp / "card.png", "-gravity", "northwest",
               "-geometry", "+190+40", "-composite", "-filter", "Lanczos", "-resize", "1280x640", "-strip", "-quality", "90",
               out.parent / "github-preview.jpg")
    for f in sorted(out.iterdir()):
        print(f"steam_art.py: {f}")


if __name__ == "__main__":
    main()
