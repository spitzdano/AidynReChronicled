# Aidyn Chronicles: portraits and item icons

The game's character, creature and sign portraits and its item and equipment-slot icons, taken from the Nintendo 64
cartridge (NTSC-U 1.0), so they can be redrawn another way and put back into Aidyn ReChronicled.

> This is the game's own art (© 2001 THQ / H2O Entertainment), taken from your own cartridge dump. It's for your
> own use: don't publish or share it.

## What's here

| Folder | |
|---|---|
| `original/` | Each picture exactly as the game stores it: portraits 40x40 pixels (one is 40x43), icons 16x16 |
| `x8/` | The same pictures 8 times larger with hard pixel edges (320x320 and 128x128). The best input for redrawing: nothing is guessed |
| `current-hd/` | The current HD look, to compare against: the same Real-ESRGAN upscale the HD pack uses, 8x (the pack also corrects the icons' colours slightly) |
| `redone/` | Empty folders, same layout: new versions go here, under the same file names |
| `manifest.json` | Every picture: name, folder, ROM asset number, size, N64 format, whether it has transparency, and the size a new version should have |
| `contact-portraits.png`, `contact-items.png` | Everything at a glance |

Inside each of the four, the pictures are sorted the same way:

| | Count | |
|---|---|---|
| `portraits/party` | 13 | Alaron, Brenna, Abrecan, Rheda, Godric (`PortraitGodrik`), Becan, Arturo, Keelin, Niesen, Donovan, Baird, Dougal, Sholeh |
| `portraits/people` | 67 | Everyone else with a face: townsfolk, guards, Chaos soldiers, bandits, Jundar, named characters |
| `portraits/creatures` | 41 | Monsters and animals: elementals, goblins, lizardmen, ogres, the undead, wolves and so on |
| `portraits/signs` | 3 | The kingdom sign, a banner and a signpost (the game "talks" through them) |
| `items` | 59 | Weapons, armour, rings, amulets, potions, herbs, keys, scrolls… as shown in the inventory |
| `slots` | 15 | The empty equipment slots on the inventory page (head, chest, rings, scrolls…) |

The names are the game's own (from the decompilation), for example `PortraitAlaron.png` and `IconItemSword.png`.
Some are spelled as the developers spelled them (`PortraitMinotuar`, `IconItemSythe`).

## Rules for redone pictures

The game swaps pictures in by name, so a new version only has to match in a few ways:

1. **Same file name, same folder**, under `redone/` (e.g. `redone/portraits/party/PortraitAlaron.png`). Leave out any
   picture you don't redo: it keeps its current HD version.
2. **Same shape.** Square for square (the Salamander portrait is 40x43). The best size is the one in the manifest's
   `redo_size`: 8x (320x320 for portraits, 128x128 for icons). Anything from 4x upwards works and is scaled to fit.
3. **Same framing.** The game draws pictures at fixed spots and, on some screens, only the left or right part of a
   portrait, so the face has to stay where it is. Redraw the picture, don't recompose it.
4. **Transparency where the original has it.** Every item icon has a transparent background (the game draws it on
   parchment): keep it transparent, with clean edges and no dark fringe. Portraits are solid squares, background
   included, except `PortraitCrossbone` (marked `"transparent": true` in the manifest).
5. **PNG**, 8-bit RGBA or RGB.

On a 1080p screen the HD menus show a portrait at roughly 140 to 180 pixels and an icon at about 70, so 8x is plenty
even at 4K.

## Putting them back

Bring the `redone/` folder back to the PC (in this package's folder or anywhere) and ask for it to be built into the
HD texture pack. The pack builder then uses each redone picture in place of its Real-ESRGAN version and cuts it the
way the game draws it (whole, or the parts some screens show), so nothing else needs doing.
