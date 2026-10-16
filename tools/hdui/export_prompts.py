#!/usr/bin/env python3
"""export_prompts.py: the Xbox button icons the port's button prompts are made of, as a package for redrawing them.

    tools/hdui/export_prompts.py      -> build/art-export/AidynPrompts/ and build/art-export/AidynPrompts.zip

The 15 icons (tools/hdui/buttons.py) that every prompt is built from: the buttons inside the game's own text
("DETAILS [X]", in the hd-ui pack's font), the controls card, the LB / RB beside the pause pages' arrows and the title
screen's version switch (tools/branding/make_branding.py). Written as:
  current/    each icon as drawn now, 256x256 (a redrawn one in art/buttons, if there is one)
  in-game/    where they appear: every line of the controls cards, the page prompts, the version switch (8x, from
              build/branding; run make_branding.py first) and the text icons (font/, a 12x9 letter cell at 8x)
  redone/     empty: new versions go here, same file names
  contact-cards.png (both cards as the game shows them), contact-buttons.png, manifest.json, README.md
(export_prompts_README.md). Nothing here comes from the ROM: the icons are drawn from shapes and a system font.
"""
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import buttons  # noqa: E402
from pngio import write_png  # noqa: E402

OUT = PROJECT / "build/art-export"
BRANDING = PROJECT / "build/branding"
SIZE = 16  # texels of the exported square, at 16 pixels each: 256x256

# name: (N64 button it stands for, what it does)
BUTTONS = {
    "A": ("A", "confirm, talk, open, attack"),
    "B": ("B", "back, cancel; walking: the party's portraits"),
    "X": ("C-down", "details; in combat: spells"),
    "Y": ("C-up", "in combat: items and potions"),
    "LB": ("L", "previous page; in combat: end the turn"),
    "RB": ("R", "next page; hold + stick: the game's camera"),
    "LT": ("Z", "walking: sneak; in combat: end the turn"),
    "RT": ("C-right", "next party member"),
    "View": ("C-left", "show / hide the controls card; previous party member; the title screen's version switch"),
    "Menu": ("Start", "pause menu"),
    "DPad": ("D-pad", "menus (shown inside the game's text)"),
    "LS": ("(left stick)", "walk; move in combat"),
    "RS": ("(right stick)", "turn the camera"),
    "L3": ("(left stick click)", "fast-forward"),
    "R3": ("(right stick click)", "camera back behind the party"),
}
USES = [  # where an icon is drawn, and the box it gets there (texels; 1 texel = about 4.5 pixels at 1080p)
    ("text", "inside the game's text, e.g. DETAILS [X] (hd-ui pack)", (8, 9)),
    ("card", "the controls card's lines", (7, 7)),
    ("page", "LB / RB beside the pause pages' arrows", (8, 8)),
    ("title", "the title screen's version switch (View)", (10, 10)),
]


def kind_of(name):
    """(kind, text, colour) for buttons.draw."""
    if name in ("LS", "RS", "L3", "R3"):
        return "stick", buttons.STICKS[name], None
    for kind, text, colour in buttons.CELLS.values():
        if buttons.name_of(kind, text) == name:
            return kind, text, colour
    raise KeyError(name)


def main():
    root = OUT / "AidynPrompts"
    shutil.rmtree(root, ignore_errors=True)
    for d in ("current", "redone", "in-game/font"):
        (root / d).mkdir(parents=True)
    font = buttons.bold_font()
    entries = []
    for name, (n64, does) in BUTTONS.items():
        kind, text, colour = kind_of(name)
        rgba = buttons.draw(kind, text, colour, SIZE, SIZE, 16, font)
        write_png(root / "current" / f"{name}.png", SIZE * 16, SIZE * 16, rgba)
        mine = (buttons.ART / f"{name}.png").exists()
        entries.append({"name": name, "file": f"{name}.png", "n64_button": n64, "does": does,
                        "redrawn": mine, "redo_size": [256, 256]})
    # In the game: the font's text icons, and the port's prompts as make_branding.py drew them.
    names = {cell: buttons.name_of(kind, text) for cell, (kind, text, _) in buttons.CELLS.items()}
    for cell, (cw, ch, rgba) in buttons.icons(8).items():
        write_png(root / "in-game/font" / f"{names[cell]}.png", cw, ch, rgba)
    pictures = sorted(p for p in BRANDING.glob("ctl_*@8x.png") if "panel" not in p.name) + sorted(BRANDING.glob("prompt_*@8x.png")) + \
        sorted(BRANDING.glob("version_*@8x.png"))
    if not pictures:
        print("export_prompts.py: no build/branding pictures (run tools/branding/make_branding.py): in-game/ has only "
              "the text icons")
    for p in pictures:
        shutil.copyfile(p, root / "in-game" / p.name.replace("@8x", ""))

    # Contact sheets: the two cards as the game shows them, and the buttons.
    cards = {"field": ["walk", "camera", "talk", "party", "sneak", "ff", "menu"],
             "combat": ["move", "attack", "magic", "endturn", "combatcam", "ff", "hide"]}
    sheets = []
    for card, rows in cards.items():
        files = [str(BRANDING / f"ctl_{r}@8x.png") for r in rows if (BRANDING / f"ctl_{r}@8x.png").exists()]
        if files:
            out = root / f"card-{card}.png"
            subprocess.run(["magick", *files, "-background", "none", "-append", "-trim", "+repage", "-bordercolor",
                            "none", "-border", "16", "(", "+clone", "-alpha", "off", "-fill", "#141414", "-colorize",
                            "100", "-alpha", "set", "-channel", "A", "-evaluate", "set", "62%", "+channel", ")",
                            "+swap", "-compose", "over", "-composite", str(out)], check=True)
            sheets.append(str(out))
    if sheets:
        subprocess.run(["magick", *sheets, "-background", "#56603f", "-bordercolor", "#56603f", "-border", "24",
                        "-gravity", "north", "+append", str(root / "contact-cards.png")], check=True)
        for s in sheets:
            Path(s).unlink()
    subprocess.run(["magick", "montage", "-label", "%t", *[str(root / "current" / f"{n}.png") for n in BUTTONS],
                    "-tile", "8x", "-geometry", "128x128+10+10", "-background", "#c8b48e", "-pointsize", "16",
                    str(root / "contact-buttons.png")], check=True)

    (root / "manifest.json").write_text(json.dumps({
        "what": "Aidyn ReChronicled's button icons (Xbox layout), drawn for the port; no game data",
        "variants": {"current": "as drawn now, 256x256", "redone": "new versions go here",
                     "in-game": "where they appear (8x)"},
        "uses": [{"where": w, "box_texels": list(box), "about": a} for w, a, box in USES],
        "buttons": entries}, indent=1) + "\n")
    shutil.copyfile(HERE / "export_prompts_README.md", root / "README.md")
    zip_path = OUT / "AidynPrompts.zip"
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(root.rglob("*")):
            z.write(f, f.relative_to(OUT))
    print(f"export_prompts.py: {len(BUTTONS)} buttons, {len(pictures)} prompt pictures")
    print(f"export_prompts.py: {root}\n                   {zip_path} ({zip_path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
