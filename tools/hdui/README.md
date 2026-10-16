# HD texture packs: hd-ui (menus, text, portraits) and hd-world (terrain, buildings, characters)

Builds an RT64 texture pack that replaces the game's 2D art (fonts, titles, logos, HUD, parchment and book
backgrounds, item icons) with 4x upscaled versions. The pack is made on your machine from your own ROM and lives
in `~/.local/share/AidynReChronicled/texture-packs/hd-ui`; it is never committed. `settings.json` loads it by default
(`"texture_packs": ["hd-ui"]`); an empty list turns it off.

```sh
~/Work/n64-recomp-toolkit/scripts/get-upscaler.sh     # once: Real-ESRGAN (ncnn/Vulkan), pinned in pins.env
tools/dev/newsave.sh                                  # once: a save in the first forest, on build/dev/test.mpk (~5 min)
tools/hdui/collect.sh --from-save build/dev/test.mpk  # play through the UI with dumping on (window must be visible)
python3 -I tools/hdui/build_pack.py                   # match, upscale, write the pack
```

`collect.sh` adds to what earlier runs collected, so screens it can't reach (combat, shops, later areas) can be
added by playing them yourself with the same two variables set:

```sh
AIDYN_DUMP_TEXTURES=$PWD/build/hdui/dumps AIDYN_UI_LOG=$PWD/build/hdui/ui build/cmake/AidynReChronicled
python3 -I tools/hdui/build_pack.py
```

How it works:

- `AIDYN_UI_LOG` (src/game/patches.cpp, hooked at `N64BorgImageDraw`) saves every 2D image the game draws, whole.
- `AIDYN_DUMP_TEXTURES` makes RT64 dump every texture the game loads, with the RDRAM address it came from.
- `build_pack.py` finds each dumped texture's rectangle in a UI image (from the load address and tile origin) and
  only accepts it when the pixels match exactly. Full-width strips (logos, backgrounds, icons) are cut from the image
  upscaled whole with Real-ESRGAN, so they join without seams. Font glyphs (cells of a greyscale atlas) are
  smooth-scaled instead (bicubic, then the edge tightened): the AI upscaler thinned them and pushed their edges past
  the cell, where the game clips them ("cut off" text).
- Portraits (40x40) are doubled pixel for pixel, then AI-upscaled 4x (8x in all). Fed the 40x40 image directly, the
  upscaler repainted faces from so few pixels that a one-pixel blue iris came out white; as 2x2 blocks the eyes survive.
- Text: both font sheets (MainFont, SlimFont: 3x46 cells of 12x9) are redrawn with `--font` (default
  `fonts/MarcellusSC-Regular.ttf`, SIL Open Font License, see `fonts/OFL.txt`): Roman small capitals like the game's
  own "HitPoints / Level / Gold" labels. `font.py` draws each letter, digit and punctuation mark with the font's own
  proportions: its width is the font's advance plus 0.6 texels, the glyph unstretched and centred in it, capitals 7
  texels tall, lowercase as small capitals. The game measures letter widths from the original (wide, loosely spaced)
  sheet, so it is given these widths instead (`font.py --header` writes `src/game/font_widths.h`, which the game
  applies when the hd-ui pack is in use; re-run it and rebuild the game after changing the font or `TRACK`), and the
  letter textures are generated for them. Controller-button icons keep the smoothed original. `--font none` keeps
  the original pixel font (smoothed); then also leave `hd-ui` out or the widths won't match.
- Words drawn as pictures (map place names, CAMP, JOURNAL, PRESS START) are listed in `labels.json` by image key and
  redrawn with the text font in their original colour and place (`labels.py`; town dots become rings). Numbers painted
  into a picture (the journal calendar, `"numbers"` region) are removed before the upscale and redrawn after it
  (`repaint.py`), one height, colour and baseline per row.
- Item and slot icons are AI-upscaled, then their colours are pulled back towards a plain bicubic upscale at coarse
  scale (`pngio.restore_colours`): the upscaler drifts small icons' colours (the short sword's blade went lavender).
- Textures load on demand by default in RT64; everything visible at rest (whole-width strips, glyphs, small images)
  is marked `preload` so a page never shows its original pixels for its first seconds.
- Left at original resolution: frames the game captures at runtime (pause background, save thumbnails) and
  full-colour text labels 9 px tall (the upscaler misspells them).

Known gaps: the Options volume sliders, and any screen not visited during collection.

## Portraits from the ROM

`portraits.py` (run by `build_pack.py`) reads all 121 portraits (characters, NPCs, monsters) straight from the ROM
(`romassets.py`: the game's asset archive, NRV2B-compressed), so every portrait is in the pack before you meet the
character. Each is drawn whole or as a left/right part while a page slides; for each of those 79 loads the TMEM
contents are rebuilt the way the RDP loads them and hashed with RT64's own function (`tmemhash.cpp`, built by CMake as
`tmemhash`). `portraits.verify()` checked this against every load the game made of Alaron's portrait (32 of 32).
Faces are doubled pixel for pixel before the 4x AI upscale (8x in all) so one-pixel eyes survive.

## Icons and other small images from the ROM

`romimages.py` (run by `build_pack.py`) does the same for every other small image the game draws in one load: all
102 item, skill and spell icons and the minimap pictures, each whole and cropped as pages slide or lists scroll.
Checked against the 226 icon and minimap textures the game loaded during collection (226 of 226).

## Big images from the ROM

`bigstrips.py` (run by `build_pack.py`) covers the 43 images too big for one TMEM load (all page and screen
backgrounds, title cards, logos, map pieces, clouds; not the two font sheets). The game side
(`src/game/patches.cpp`, "Big pictures") always draws them as the same full-width strips, so each has one fixed set:
rows `k*step .. k*step+step` (one extra row for filtering) and the rows left at the bottom. They're rebuilt from the
ROM like RT64's LoadTile (8-, 16- and 32-bit; 32-bit split into two TMEM halves) and hashed with `tmemhash`; checked
against a run's captures: all 207 strips of the 14 big images it drew matched. Images that
were captured keep their edits (journal numbers, button icons); clouds are smooth-scaled.

## World textures

```sh
python3 -I tools/hdui/world.py    # after collect.sh / playing with AIDYN_DUMP_TEXTURES set; needs python-numpy
```

Every world texture in the ROM (`romtextures.py`, 2,179 textures: terrain, walls, trees, water, characters, monsters,
items on the ground), each mip level and every scroll position of water and fog, keyed by the hash the game produces
(the game's own load code replayed: decomp n64Borg/anim.cpp). Checked against capture: 1,568 of 1,768 static world
textures and 136 of 140 scrolling frames seen in game; the rest (particles, effects) come from texture dumps. Each is padded with copies
of itself as it repeats in game (wrap, mirror or clamp), upscaled 4x with Real-ESRGAN's general model (the anime model
flattens stone and grass), then fine detail the AI smoothed away is put back from a bicubic upscale, pixel by pixel
(`keep_grain`). Mipmap levels are found as textures loaded together whose pixels are a shrunken copy of the largest,
and are rebuilt from the upscaled image. The world pack streams in as textures are first used.

## Art for redrawing

```sh
python3 tools/hdui/export_art.py        # portraits, item and slot icons -> build/art-export/AidynArt(.zip)
python3 tools/hdui/export_textures.py   # world textures -> build/art-export/AidynTextures(.zip); --no-hd: smaller
python3 tools/hdui/export_prompts.py    # the button icons of the port's prompts -> build/art-export/AidynPrompts(.zip)
```

Packages of the game's art for redrawing it some other way, each with original, 8x nearest-neighbour and current-HD
versions, empty `redone/` folders, contact sheets, a manifest and a README with the rules a new version has to follow
(`export_art_README.md`, `export_textures_README.md`). `export_textures.py` sorts the 2,115 distinct world textures by
what uses them, found the way the game loads them: characters (Borg7 actors → Borg6 → Borg5 models), areas (map
chunks, and the props each area's Borg9 maps place), items, spell effects (the decomp's spell visuals), other. Both
are made from the ROM, so they stay in build/.

Redrawn pictures come back into `art/` (see `art/README.md`): `redone.py` finds portraits and icons by name and
textures by number, `portraits.py` and `romimages.py` use the pictures instead of upscaling (in `hd-ui`), and
`art_pack.py` builds the `rechronicled-art` pack from them alone (textures too: every mip level and scroll position),
which a release can ship and which wins over `hd-ui` and `hd-world`. Button icons go in `art/buttons`, where
`buttons.py` picks them up for every prompt.
