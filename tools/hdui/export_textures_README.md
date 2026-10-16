# Aidyn Chronicles: world textures

The textures of the game's 3D world (characters and monsters, land, buildings, props, weapons, spell effects),
taken from the Nintendo 64 cartridge (NTSC-U 1.0), so they can be redrawn another way and put back into Aidyn
ReChronicled. The portraits and item icons are a separate package (AidynArt).

> This is the game's own art (© 2001 THQ / H2O Entertainment), taken from your own cartridge dump. It's for your
> own use: don't publish or share it.

## What's here

| Folder | |
|---|---|
| `original/` | Each texture exactly as the game stores it, mostly 32x32 pixels (some 16 to 64 on a side) |
| `x8/` | The same textures 8 times larger with hard pixel edges. The best input for redrawing: nothing is guessed |
| `current-hd/` | The current HD look, to compare against: the HD world pack's version (Real-ESRGAN, 8x) |
| `redone/` | Empty folders, same layout: new versions go here, under the same file names |
| `contact/` | One sheet per folder, every texture at a glance, labelled with its number and size |
| `manifest.json` | Every texture: file, ROM asset number, size, N64 format, transparency, tiling, whether it scrolls, every model and area that uses it, and the size a new version should have |

Inside each of the four picture folders, the textures are sorted the same way:

| | Count | |
|---|---|---|
| `characters/<Name>` | 583 in 91 folders | One model's textures: the party, everyone you talk to, every monster. Folders use the game's model names (`Alaron`, `Goblin2`, `ChaosTrooper`, `NPCFemale1` for townsfolk) |
| `characters/_shared` | 149 | Used by several characters (shared skin, cloth, armour) |
| `world/<Area>` | 1,099 in 21 folders | Land, buildings and props, by area: `Overworld` (317), `EhudTower` (144), `GwerniaCastle` (90), `Ugarit`, `Terminor`, `Erromon`, the interiors, dungeons and cutscene sets (`CoronationScene`, `Dreamscape`, `Campsite`) |
| `items/<Name>` | 73 in 47 folders | Weapons and shields as they're held, and a few carried things (`HornKynon`, `HatKendall`) |
| `items/_shared` | 16 | Used by several items |
| `effects/<Name>` | 26 in 24 folders | Spells (named after the spell: `AirShield`, `Whitefire`…), potions, combat markers |
| `effects/_shared` | 23 | Used by several effects |
| `other` | 146 | Used by nothing the export could trace to a model (the code loads them itself): particles, numbers, sky, odds and ends |

Files are named after the texture's asset number in the ROM, for example `tex08989.png`. There are no names to
give them: the game doesn't name its textures.

**One texture, one folder.** The game shares textures between models (a stone wall in five areas, a belt on three
characters). Each texture is still only here once, because redrawing it changes it everywhere it's used. It's in the
most specific place: an item before a character before an effect before the world, and within the world, the area
that uses it most. `used_by` and `areas` in the manifest list everything that uses it.

## Rules for redone textures

The game wraps these pictures around 3D models, so a new version has to fit the same way:

1. **Same file name, same folder**, under `redone/` (e.g. `redone/characters/Goblin/tex08989.png`). Leave out any
   texture you don't redo: it keeps its current HD version.
2. **Same shape.** 32x64 stays twice as tall as wide. The best size is the manifest's `redo_size`, 8x (256x256 for a
   32x32 texture). Anything from 4x upwards works and is scaled to fit.
3. **Same layout.** A character's texture is a map of pieces (a face, a sleeve, a belt buckle) laid onto the model at
   fixed spots, and a building's is laid onto its walls the same way. Keep every feature where it is and the same
   size; redraw it, don't rearrange it. The contact sheets and `current-hd/` help to see what a piece is.
4. **Seamless where it repeats.** Most land and wall textures repeat across a surface: the right edge has to
   continue into the left and the bottom into the top. `tiling` in the manifest says how each was seen to repeat
   while playing (`wrap`: repeats; `mirror`: repeats flipped, so the edges always meet; `clamp`: doesn't repeat).
   It's `null` for textures not seen yet: assume `wrap` for land, walls, roofs and floors. Textures with
   `"scrolls": true` (water, fog, lava: 94) slide sideways in the game and must wrap left to right.
5. **Keep the greyscale ones grey.** Textures in the I and IA formats (127: `I4`, `I8`, `IA4`, `IA8`, `IA16` in the
   manifest) are coloured by the game as it draws them (spell glows, smoke, some foliage). Draw them in shades of
   grey, or the colours will multiply.
6. **Transparency like the original.** `transparency` in the manifest: `none` (solid), `cut-out` (leaves, fences,
   hair, grilles: every pixel fully see-through or fully solid; the game keeps hard edges, so no soft fringe) or
   `soft` (smoke, glows, shadows: partly see-through).
7. **PNG**, 8-bit RGBA or RGB.

The game also uses smaller copies of some textures for distant surfaces (`mip_levels`); those are made from your
picture automatically.

## Putting them back

Bring the `redone/` folder back to the PC (in this package's folder or anywhere) and ask for it to be built in. The
textures go into the project's `art/textures/` and the `rechronicled-art` pack, which the game puts in front of the
HD world pack: each redone texture is used in place of its Real-ESRGAN version, with everything the game needs made
from it (the smaller copies, every scroll position of the moving ones), so nothing else needs doing. The first one in
was Alaron's set.
