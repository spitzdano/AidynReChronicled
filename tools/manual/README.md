# tools/manual

A PC edition of the game's instruction booklet: the same chapters as the 2001 booklet (controls, menus, saving,
combat, magic, the pause menu, camping, stats, items, characters, spells), written anew for this port, plus PC
chapters (getting started, Aidyn Patch, what ReChronicled changes, `settings.json`). It looks like the booklet:
green cloth, gold uncial headings with illuminated initials, the spell list in gold bars.

```sh
tools/manual/shots.sh          # optional: screenshots of the port (needs build/dev/test.mpk; takes about a minute)
tools/manual/build_manual.py   # -> ~/.local/share/AidynReChronicled/manual/index.html + "Aidyn ReChronicled Manual.pdf"
```

F1 in the game, and the Manual action of the launchers (`tools/install-launchers.sh`), open `index.html`.

| File | |
|---|---|
| `manual.html` | The source: all the text, the CSS (screen and A5 print), markers for what the build fills in |
| `build_manual.py` | Fills it in, writes the page and its files, prints the PDF with headless Chromium (profile in a temp folder) |
| `romdata.py` | The spell list from a ROM's spell database (the way `SpellDB::Init` reads it), so it is right for the original and for Aidyn Patch |
| `romart.py` | Portraits, icons and pictures from the ROM's Borg8 images, upscaled with Real-ESRGAN (cached in `build/manual/art`) |
| `shots.sh` | Plays the test save through `tools/dev/pad.sh` and screenshots the title, menus, field, pause pages and options at 1920x1080 |
| `initial.svg` | The illuminated initials' gold tile (rendered to PNG by the build: a CSS gradient prints as a page-sized image on every page) |
| `fonts/` | Uncial Antiqua and Alegreya Sans (SIL OFL 1.1, licences beside them; Marcellus SC comes from `tools/hdui/fonts`) |

**Nothing from the game is in the repository.** The text is new; the game facts follow the booklet and the spell
data comes from the player's ROM at build time. Pictures and screenshots are made from the ROM into `build/manual`
(gitignored) and the data folder. The original booklet's scans are not used.

Where the ROM and the booklet disagree, the ROM wins, since it is what the game does: Air Shield, Dragon Flames and
Fireball use other ingredients than the booklet lists, Sense Aura is neutral rather than Lunar, and Escape, Detecting
Traps, Opening, Detect Moon Phase, Detect Sun Phase, Stealth and Poison are not in the booklet at all (their
descriptions say so rather than guess).

Font checksums (sha256): `UncialAntiqua-Regular.ttf` 33a5128b…, `AlegreyaSans-Regular.ttf` 8fab6341…,
`-Italic` f49f6f2b…, `-Bold` a3055a18…, `-BoldItalic` 6a5550f0…, from github.com/google/fonts (ofl/).
