#!/usr/bin/env python3
"""build_manual.py: build the Aidyn ReChronicled manual (HTML + PDF) from manual.html and the player's ROM.

    tools/manual/build_manual.py [--rom ROM] [--patch-rom ROM] [--out DIR] [--no-pdf]

1. Pictures: every <img data-art="Name"> is a Borg8 image: the redrawn one in art/ if there is one (portraits, item
   and slot icons), else the ROM's, upscaled by romart.py (cached in build/manual/art); every <figure data-shot="name"> is build/manual/shots/<name>.png from shots.sh, saved as JPEG.
   A figure without its screenshot is left out (or shows the Borg8 image named in data-fallback).
2. The spell list is read from the ROM (romdata.py); with the Aidyn Patch ROM too, a table of what the patch changes.
3. Writes DIR/index.html with fonts/, art/, shots/ next to it, then DIR/Aidyn ReChronicled Manual.pdf (Chromium,
   headless, its profile in a temp folder).
Defaults: the ROMs the game stored in ~/.local/share/AidynReChronicled (else baserom.z64), output in its manual/ folder.
Everything written is derived from the ROM, so none of it goes in the repository. The F1 key in the game opens
DIR/index.html.
"""
import argparse
import datetime
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import romart  # noqa: E402
import romdata  # noqa: E402
import redone  # noqa: E402  (tools/hdui, on the path through romart)

DATA = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "AidynReChronicled"
PDF_NAME = "Aidyn ReChronicled Manual.pdf"
FONTS = [HERE / "fonts" / f for f in ("UncialAntiqua-Regular.ttf", "AlegreyaSans-Regular.ttf", "AlegreyaSans-Italic.ttf",
                                      "AlegreyaSans-Bold.ttf", "AlegreyaSans-BoldItalic.ttf")]
FONTS.append(PROJECT / "tools/hdui/fonts/MarcellusSC-Regular.ttf")
SCHOOL_ICON = {"Elemental": "IconSchoolElem", "Naming": "IconSchoolName", "Necromancy": "IconSchoolNecro",
               "Star": "IconSchoolStar"}

# What each spell does, in the manual's words, after the original booklet's spell list. Keyed by the spell's item
# number (low byte of its ID), which Aidyn Patch keeps when it renames one. None: the booklet doesn't describe it.
DESCRIPTIONS = {
    0x00: "Every friendly figure within range gains 1 Armour per rank.",
    0x01: "One elemental within range changes sides and fights for the party.",
    0x02: "The target loses 2 Strength per rank.",
    0x03: "Dragon fire pours from the caster over the targets.",
    0x04: "A great chunk of earth falls on the target and crushes it.",
    0x05: "A ball of fire flies from the caster's fingertips.",
    0x06: "Lightning strikes the target.",
    0x08: "The target gains 2 Strength per rank.",
    0x09: "A magic wind clears fog from the battlefield and strikes one target.",
    0x0A: "An elemental within range is banished to the spirit plane: destroyed.",
    0x0C: "The target changes sides for the battle, seeing friends as enemies and enemies as friends.",
    0x0D: "One Marquis within range changes sides and fights for the party.",
    0x0F: None,
    0x10: "The target gains 2 Endurance per rank.",
    0x11: None,
    0x12: "Reveals the target's stats; higher ranks reveal more.",
    0x13: "The target loses 2 Endurance per rank.",
    0x14: "One zombie within range changes sides and fights for the party.",
    0x15: "A ghostly hand closes on the target's heart and crushes it.",
    0x16: "Turns the battlefield to night, which changes its aspect.",
    0x17: "Everyone in range moves 2 further per rank.",
    0x18: "The target loses 2 Stamina per rank.",
    0x19: "The target gains 2 Stamina per rank.",
    0x1A: "Takes 2 Stamina per rank from the target and gives it to the caster for the rest of the battle.",
    0x1B: "Walls the target in with bleached bones: while it lasts the target can't attack, cast or move.",
    0x1C: "Every friendly figure within range gains 1 Armour per rank.",
    0x1D: "Resistance to Elemental magic rises by 6% per rank.",
    0x1E: "Resistance to Naming magic rises by 6% per rank.",
    0x1F: "Resistance to Necromancy rises by 6% per rank.",
    0x20: "Resistance to Star magic rises by 6% per rank.",
    0x21: "May end any Elemental spell active in the area.",
    0x22: "May end any Naming spell active in the area.",
    0x23: "May end any Necromancy spell active in the area.",
    0x24: "May end any Star spell active in the area.",
    0x26: "Every friendly figure within range gains 1 Armour per rank.",
    0x27: None,
    0x28: None,
    0x29: "The target gains 2 Dexterity per rank.",
    0x2A: "The target loses 2 Dexterity per rank.",
    0x2B: "Freezes the target solid for as long as the spell lasts.",
    0x2D: "Turns the battlefield to day, which changes its aspect.",
    0x2E: None,
    0x2F: "The target's movement drops by 2 per rank; at zero it can't move.",
    0x30: "Traps the target in a glowing web that hurts it every round. It can't move, attack or cast until it breaks "
          "free, which depends on its Strength.",
    0x31: "White fire flash-fries one target.",
    0x32: None,
    0x33: "Drains 2d6 points from one of the target's stats, chosen at random.",
    0x34: "Shakes the morale of Solar enemies, who may flee.",
    0x35: "Shakes the morale of Lunar enemies, who may flee.",
    0x36: "The target loses 2 Intelligence per rank.",
    0x37: "The target gains 2 Intelligence per rank.",
    0x38: "Cures poison; higher ranks succeed more often.",
    0x39: "Everyone affected regains some Stamina every round. Daytime only.",
    0x3A: "The caster jumps to a chosen spot on the battlefield within range.",
    0x3B: "Acid burns the target for 1d6 every round until the spell ends.",
    0x3C: None,
    0x3D: "Sets the target alight: it burns for 1d6 every round until the spell ends.",
    0x3E: "Turns the next few spells cast at the target back on their casters.",
    0x3F: "When a wound would knock the target out, the spell restores them with a few Endurance points (more at "
          "higher ranks), then ends.",
}


def toolkit():
    if os.environ.get("N64RECOMP_TOOLKIT"):
        return Path(os.environ["N64RECOMP_TOOLKIT"])
    for line in (PROJECT / "tools/toolkit.env").read_text().splitlines():
        if line.startswith("TOOLKIT="):  # TOOLKIT=${N64RECOMP_TOOLKIT:-/path}
            value = line.split("=", 1)[1].strip().strip('"')
            if value.startswith("${") and ":-" in value:
                value = value.split(":-", 1)[1].rstrip("}")
            return Path(os.path.expandvars(value)).expanduser()
    sys.exit("build_manual.py: tools/toolkit.env has no TOOLKIT=")


def esc(s):
    return html.escape(str(s), quote=True)


def art_img(name, cls="icon"):
    return f'<img class="{cls}" data-art="{name}" alt="">'


def spell_tables(spells):
    """One table per school; each spell is a row of figures with its description on a full-width row under it."""
    out = []
    for school in ("Elemental", "Naming", "Necromancy", "Star", "Neutral"):
        rows = [s for s in spells if s["school"] == school]
        if not rows:
            continue
        icon = art_img(SCHOOL_ICON[school], "icon icon-big") if school in SCHOOL_ICON else ""
        out.append(f'<h3 class="school">{icon}{school}</h3>')
        out.append('<div class="scroll"><table class="spells"><thead><tr><th>Spell</th><th class="n">Rank</th>'
                   '<th>Ingredient</th><th class="n">ST</th><th class="n">Dmg</th><th>Aspect</th><th>On</th>'
                   '<th class="n">XP</th></tr></thead>')
        for s in sorted(rows, key=lambda s: s["name"].lower()):
            desc = DESCRIPTIONS.get(s["id"] & 0xFF)
            text = esc(desc) if desc else '<span class="dim">Not in the original booklet.</span>'
            if s["special"]:
                text += f' <em>{esc(s["special"])}.</em>'
            aspect = {"Solar": art_img("IconAsSolar"), "Lunar": art_img("IconAsLunar")}.get(s["aspect"], "")
            out.append(
                f'<tbody class="spell"><tr><td>{esc(s["name"])}</td><td class="n">{s["rank"]}</td>'
                f'<td>{esc(s["ingredient"]) or "—"}</td><td class="n">{s["stamina"]}</td>'
                f'<td class="n">{s["damage"] or "—"}</td><td>{aspect} {esc(s["aspect"])}</td><td>{esc(s["target"])}</td>'
                f'<td class="n">{s["xp"] or "—"}</td></tr>'
                f'<tr><td class="desc" colspan="8">{text}</td></tr></tbody>')
        out.append("</table></div>")
    return "\n".join(out)


# Aidyn Patch table: one column per figure, a cell only where the patch changes it.
COLUMNS = [("rank", "Rank"), ("ingredient", "Ingredient"), ("stamina", "ST"), ("damage", "Dmg"), ("target", "On"),
           ("xp", "XP")]


def other_changes(b, p):
    out = []
    if b["name"] != p["name"]:
        out.append(f"now called {esc(p['name'])}")
    if b["aspect"] != p["aspect"]:
        out.append(f"now {esc(p['aspect'])}")
    if b["special"] != p["special"]:
        out.append(esc(p["special"]) if p["special"] else "no target limit")
    if b["range"] != p["range"]:
        out.append(f"reach {b['range']} → {p['range']}")
    return out


def patch_table(base, patched):
    by_id = {s["id"]: s for s in base}
    rows = []
    for p in patched:
        b = by_id.get(p["id"])
        if b is None:
            rows.append((p["name"], {}, ["new spell"]))
            continue
        cells = {k: f'<s>{esc(b[k] or "—")}</s> {esc(p[k] or "—")}' for k, _ in COLUMNS if b[k] != p[k]}
        other = other_changes(b, p)
        if cells or other:
            rows.append((b["name"], cells, other))
    if not rows:
        return "<p>Aidyn Patch leaves every spell as it was.</p>"
    out = [f"<h3>Spells in Aidyn Patch</h3><p>What the patch changes in the spell list: {len(rows)} of {len(patched)} "
           "spells, read from both of your dumps. Each cell shows the original value struck out, then the patch's. "
           "Columns as in the <a href=\"#spells\">Spell List</a>; reach is how far the spell goes.</p>",
           '<div class="scroll"><table class="changes"><thead><tr><th>Spell</th>'
           + "".join(f"<th>{label}</th>" for _, label in COLUMNS) + "<th>Other</th></tr></thead><tbody>"]
    for name, cells, other in rows:
        out.append(f"<tr><td>{esc(name)}</td>" + "".join(f"<td>{cells.get(k, '')}</td>" for k, _ in COLUMNS)
                   + f"<td>{'; '.join(other)}</td></tr>")
    out.append("</tbody></table></div>")
    return "\n".join(out)


def toc(page):
    items = []
    for sid, title in re.findall(r'<section class="chapter" id="([\w-]+)">\s*<h2>(.*?)</h2>', page):
        items.append(f'<li><a href="#{sid}">{title}</a></li>')
    return "<ol>\n" + "\n".join(items) + "\n</ol>"


def initials(page):
    """<h2>Controls</h2> -> <h2><span class="initial">C</span>ontrols</h2> (Contents too)."""
    return re.sub(r"<h2>(\w)", r'<h2><span class="initial">\1</span>', page)


def save_art(name, src, folder):
    """Sized for the page (the PDF embeds each picture again on every page that shows it): icons 64 px, opaque
    pictures as JPEG. Returns the file name."""
    if not shutil.which("magick"):
        shutil.copyfile(src, folder / f"{name}.png")
        return f"{name}.png"
    size = "64x64>" if name.startswith("Icon") else "256x256>" if name.startswith("Portrait") else "1024x1024>"
    opaque = subprocess.run(["magick", "identify", "-format", "%[opaque]", str(src)], capture_output=True,
                            text=True, check=True).stdout.strip().lower() == "true"
    out = f"{name}.jpg" if opaque else f"{name}.png"
    subprocess.run(["magick", str(src), "-resize", size] + (["-quality", "90"] if opaque else []) + [str(folder / out)],
                   check=True)
    return out


def save_shot(src, dst):
    if shutil.which("magick"):
        subprocess.run(["magick", str(src), "-resize", "1600x>", "-quality", "86", str(dst)], check=True)
    else:
        shutil.copyfile(src, dst.with_suffix(".png"))
        return dst.with_suffix(".png")
    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    stored = DATA / "aidyn.n64.us.1.0.z64"
    ap.add_argument("--rom", type=Path, default=stored if stored.exists() else PROJECT / "baserom.z64")
    ap.add_argument("--patch-rom", type=Path, default=DATA / "aidyn.n64.us.1.0.aidyn-patch.z64")
    ap.add_argument("--out", type=Path, default=DATA / "manual")
    ap.add_argument("--no-pdf", action="store_true")
    args = ap.parse_args()
    if not args.rom.exists():
        sys.exit(f"build_manual.py: no ROM at {args.rom} (run the game once with --rom, or pass --rom)")
    esrgan = toolkit() / "opt/realesrgan/realesrgan-ncnn-vulkan"
    if not esrgan.exists():
        sys.exit(f"build_manual.py: {esrgan} missing (run the toolkit's scripts/get-upscaler.sh)")

    page = re.sub(r"<!--.*?-->\n?", "", (HERE / "manual.html").read_text(), flags=re.S)  # the source's own notes
    spells = romdata.spells(args.rom)
    page = page.replace("{{SPELLS}}", spell_tables(spells))
    if args.patch_rom.exists():
        page = page.replace("{{PATCH}}", patch_table(spells, romdata.spells(args.patch_rom)))
    else:
        page = page.replace("{{PATCH}}", "<p class=\"note\">Store the Aidyn Patch dump (start the game once with "
                                         "<code>--rom</code> and the patched file) and build this manual again to see "
                                         "what the patch changes in the spell list.</p>")
    page = page.replace("{{BUILT}}", f"Built {datetime.date.today():%-d %B %Y}.")
    page = page.replace("{{TOC}}", toc(page))
    page = initials(page)

    out = args.out
    tmp_out = out.with_name(out.name + ".new")
    shutil.rmtree(tmp_out, ignore_errors=True)
    (tmp_out / "fonts").mkdir(parents=True)
    (tmp_out / "art").mkdir()
    (tmp_out / "shots").mkdir()
    for f in FONTS:
        shutil.copyfile(f, tmp_out / "fonts" / f.name)

    shots_dir = PROJECT / "build/manual/shots"

    def shot(m):
        name, rest = m.group(1), m.group(2)
        src = shots_dir / f"{name}.png"
        if src.exists():
            dst = save_shot(src, tmp_out / "shots" / f"{name}.jpg")
            return m.group(0).replace("<img ", f'<img src="shots/{dst.name}" ', 1)
        fb = re.search(r'data-fallback="(\w+)"', rest)
        if fb:
            return m.group(0).replace("<img ", f'<img data-art="{fb.group(1)}" ', 1)
        return ""
    page = re.sub(r'<figure data-shot="([\w-]+)"([^>]*)>.*?</figure>', shot, page, flags=re.S)

    wanted = sorted(set(re.findall(r'data-art="(\w+)"', page)))
    art = romart.build(args.rom, PROJECT / "decomp", esrgan, wanted, PROJECT / "build/manual/art")
    art.update({n: p for n, p in redone.collect().items() if n in art})  # the redrawn portraits and icons (art/)
    files = {name: save_art(name, path, tmp_out / "art") for name, path in art.items()}
    page = re.sub(r'data-art="(\w+)"', lambda m: f'src="art/{files[m.group(1)]}"', page)

    # The initials' gold tile: a gradient prints as a page-sized image on every page, so it is a picture here.
    if shutil.which("magick"):
        subprocess.run(["magick", "-background", "none", "-density", "288", str(HERE / "initial.svg"), "-resize",
                        "276x276", str(tmp_out / "art/initial.png")], check=True)
    else:
        shutil.copyfile(HERE / "initial.svg", tmp_out / "art/initial.svg")
        page = page.replace("art/initial.png", "art/initial.svg")
    (tmp_out / "index.html").write_text(page)
    if not args.no_pdf:
        pdf(tmp_out / "index.html", tmp_out / PDF_NAME)
    shutil.rmtree(out, ignore_errors=True)
    tmp_out.rename(out)
    print(f"build_manual.py: {out / 'index.html'}" + ("" if args.no_pdf else f"\n                 {out / PDF_NAME}"))


def pdf(page, dst):
    browser = shutil.which("chromium") or shutil.which("google-chrome-stable")
    if not browser:
        print("build_manual.py: no chromium: skipped the PDF", file=sys.stderr)
        return
    with tempfile.TemporaryDirectory() as profile:  # never a profile in ~/.config
        r = subprocess.run([browser, "--headless", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files",
                            f"--user-data-dir={profile}", f"--print-to-pdf={dst}", page.as_uri()],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=300)
    if r.returncode != 0 or not dst.exists():
        sys.exit(f"build_manual.py: chromium failed to print the PDF:\n{r.stderr[-2000:]}")


if __name__ == "__main__":
    main()
