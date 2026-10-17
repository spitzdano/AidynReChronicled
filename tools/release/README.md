# Aidyn ReChronicled

*Aidyn Chronicles: The First Mage* (Nintendo 64, 2001) as a native Linux game, enhanced: high resolution, widescreen,
up to 120 fps, a free camera, further draw distance, and the community Aidyn Patch one button away.

**You need your own copy of the game:** a dump of the North American 1.0 cartridge (`.z64`, SHA-1
`47ae795a2b12783a13e2b8d03439ce9a39fc826c`). Nothing from the game is in this download.

## Play

Unpack the download, then either:

- **Install** (recommended): `bash install.sh`. It puts the game in `~/Games/AidynReChronicled`, adds "Aidyn
  ReChronicled" and "Aidyn ReChronicled (Aidyn Patch)" to the app menu and, if you want, to Steam (as a non-Steam
  game; Steam closes for a moment).
- **Or run it in place**: `game/aidyn-rechronicled.sh`.

The first start asks for your ROM in a file dialog (or start it with `--rom /path/to/game.z64`). It is checked, kept
in `~/.local/share/AidynReChronicled`, and never asked for again. A zipped ROM has to be unzipped first.

Needs: Linux on x86_64 with glibc 2.38 or newer (any current distribution: Bazzite, Fedora, Ubuntu 24.04, Arch…), a
Vulkan graphics driver, and libdbus (every desktop has it). Everything else is in `game/lib`.

## Aidyn Patch

The community Aidyn Patch (u/halibabica) rebalances the game. Apply it to your own 1.0 dump, then start "Aidyn
ReChronicled (Aidyn Patch)" (or `--patch`) and choose the patched file when asked. Once both are stored, the View
button (keyboard J) on the title screen switches between them.

## Controls

An Xbox-style controller, or the keyboard:

| Controller | Keyboard | |
|---|---|---|
| Left stick, D-pad | Arrows, keypad 8 4 5 6 | Walk, move in combat, menus |
| Right stick (click: recentre) | | Camera |
| A / B | Z / X | Confirm, talk, attack / back, cancel |
| X / Y | K / I | Details, spells / items |
| View | J | Show / hide the controls card (walking, combat); previous party member (pause menu); switch versions (title screen) |
| RT | L | Next party member (pause menu) |
| LT / LB / RB | C / Q / E | Sneak, end turn, previous / next page |
| Menu | Enter | Pause menu |
| Left stick click | hold Tab | Fast-forward on / off (3x; enemy turns in combat are 2x on their own) |
| | F2 | The newly painted art off / on, to compare with what was there before |
| | F11, Alt+Enter / Shift+Esc | Fullscreen / quit |

## Settings, saves, logs

All in `~/.local/share/AidynReChronicled`:

- `settings.json`: resolution, aspect (`"16:9"`, `"21:9"`, `"4:3"`…), frame rate, anti-aliasing, draw distance and
  more, written with defaults on the first start. Edit it while the game is closed. `camera_invert_y` /
  `camera_invert_x` invert the camera.
- `aidyn.mpk` (`aidyn-patch.mpk`): your saves, a Controller Pak image like an emulator's. `backups/` keeps its last
  20 states (at each start and before each save).
- `logs/last-run.log`: what the last start printed. Look there if something goes wrong.
- `texture-packs/`: HD texture packs. This release includes, next to the game, newly painted art
  (`rechronicled-art`: every portrait, the item and equipment-slot icons, the button prompts, redrawn HUD pieces and
  the first world textures: Alaron, the starting forest, part of Gwernia Castle) and the title-screen branding
  (`rechronicled`); the HD menu and world packs (upscales of the game's own pictures) are not part of it.

## About

Made by statically recompiling the game with N64Recomp, running on N64ModernRuntime and the RT64 renderer.
Unofficial: not affiliated with or endorsed by THQ, H2O Entertainment or Nintendo. GPL-3.0 (see `LICENSE`).
Built on blackgamma7's Aidyn Chronicles decompilation (https://github.com/blackgamma7/Aidyn).

Made with heavy AI involvement: nearly all of the code was written by an AI coding assistant (Claude Code), directed
and play-tested by Spitzdano, and the redrawn portraits, icons and world textures were made with an AI image
generator from the game's originals.
