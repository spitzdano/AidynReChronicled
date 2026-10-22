# Aidyn ReChronicled

*Aidyn Chronicles: The First Mage* (Nintendo 64, 2001) as a native Linux game, enhanced: high resolution, widescreen,
up to 240 fps, a free camera, further draw distance, quicksaves and autosaves, a faster walk, and the community
rebalances Aidyn Patch and Aidyn Plus a button away.

**You need your own copy of the game:** a dump of the North American 1.0 cartridge (`.z64`, SHA-1
`47ae795a2b12783a13e2b8d03439ce9a39fc826c`). Nothing from the game is in this download.

## Play

Unpack the download, then either:

- **Install** (recommended): `bash install.sh`. It puts the game in `~/Games/AidynReChronicled`, adds "Aidyn
  ReChronicled", "Aidyn ReChronicled (Aidyn Patch)" and "Aidyn ReChronicled (Aidyn Plus)" to the app menu and, if you
  want, to Steam (as a non-Steam game; Steam closes for a moment).
- **Or run it in place**: `game/aidyn-rechronicled.sh`.

**Adding it to Steam by hand**, if the installer's Steam step says it can't (it only writes Steam's shortcut file when
it can read it back exactly): in Desktop Mode, Steam › Games › **Add a Non-Steam Game to My Library** › Browse ›
`~/Games/AidynReChronicled/aidyn-rechronicled.sh` › Add Selected Programs; then right-click it › **Properties**, name
it `Aidyn ReChronicled` and pick `~/Games/AidynReChronicled/icon.png` as its icon. Its library art is in
`~/Games/AidynReChronicled/steam-art/` (right-click it › Manage › Set custom artwork: `capsule.png`; on its page,
right-click the banner: `hero.png` and `logo.png`). It's a native Linux game: under
Properties › Compatibility, "Force the use of a specific Steam Play compatibility tool" must stay **unchecked**.

The first start asks for your ROM in a file dialog (or start it with `--rom /path/to/game.z64`). It is checked, kept
in `~/.local/share/AidynReChronicled`, and never asked for again. A zipped ROM has to be unzipped first.

Needs: Linux on x86_64 with glibc 2.38 or newer (any current distribution: Bazzite, Fedora, Ubuntu 24.04, Arch…), a
Vulkan graphics driver, and libdbus (every desktop has it). Everything else is in `game/lib`.

## Found a problem?

Please report it: a crash, a freeze, a glitch, a wrong picture, anything that looks off.

1. **Make a problem report.** If the game crashed, it already made one and said where. Otherwise pause as close as you
   can to the problem and choose **Report a problem** at the bottom of the **ReChronicled** page (on the pause
   menu's Menu page): it also keeps a picture of the screen and a save of that moment.
2. **Open an issue** with the [Problem report form](https://github.com/spitzdano/AidynReChronicled/issues/new?template=problem-report.yml) and drag in the newest `.zip` from
   `~/.local/share/AidynReChronicled/reports` (on a Steam Deck or an Ally, switch to Desktop Mode to reach it).
3. **Say what happened** and what you were doing just before.

A report holds the game's logs, your settings and your saves, nothing from the game's ROM, and your home folder's
name is left out of it.

## Aidyn Patch and Aidyn Plus

Two community rebalances by halibabica (u/halibabica) can be played instead of the original: Aidyn Patch and the bigger
Aidyn Plus (read its FAQ). Neither is in this download: get the patch yourself and apply it to your own 1.0 dump (Aidyn Plus is
a `.bps` patch: apply it with Flips or another BPS patcher), then start "Aidyn ReChronicled (Aidyn Patch)" or "(Aidyn
Plus)" (or `--patch` / `--plus`) and choose the patched file when asked, or start with `--rom /path/to/it.z64`. It is
checked by its hash (SHA-1 Aidyn Patch `1f95ad78824f491ea3d94e3ac5d36901cef09e4e`, Aidyn Plus
`d57f9459a77c66490ff09b3d9d97545c7a65fad2`) and kept. You can have none, one or both.

On the title screen the bottom left lists Original, Aidyn Patch and Aidyn Plus, the one you're playing lit; X and Y
(keyboard K and I) switch to the other two, as marked. One you haven't added is grey and marked *not found*. From the
menu (New Game, Load Game, Options), B goes back to the title. Each version keeps its own saves.

## Controls

An Xbox-style controller, or the keyboard:

| Controller | Keyboard | |
|---|---|---|
| Left stick, D-pad | Arrows, keypad 8 4 5 6 | Walk, move in combat, menus |
| Right stick (click: recentre) | | Camera |
| A / B | Z / X | Confirm, talk, attack / back, cancel |
| X / Y | K / I | Details, spells / items; switch versions (title screen) |
| View | J | Quicksave (walking about); previous party member (pause menu) |
| RT | L | Next party member (pause menu) |
| LB | Q | Show / hide the controls card (walking, combat); previous page (pause menu) |
| LT / RB | C / E | Sneak, end turn, previous page / next page |
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
- `aidyn-patch-quicksave.sav`, `-autosave-1.sav`, `-autosave-2.sav`, `-lastsave.sav`: the quicksave (View while
  walking about), the two newest autosaves and a copy of the last save made on the pak, for Quick Load and Continue.
- `logs/last-run.log`: what the last start printed. Look there if something goes wrong.
- `reports/`: problem reports, one `.zip` each with the logs, settings and saves: written by the game when it
  crashes, or by **Report a problem** on the pause menu's ReChronicled page. Send the newest with a few words on what
  happened.
- `texture-packs/`: HD texture packs. This release includes, next to the game, newly painted art
  (`rechronicled-art`: every portrait, the item and equipment-slot icons, the button prompts, redrawn HUD pieces and
  the first world textures: Alaron, the starting forest, part of Gwernia Castle), the HD letters and Xbox button
  icons for all the game's text and the redrawn menu titles and inventory words (`rechronicled-text`, drawn from fonts;
  they win over any HD menu pack's, so the text always matches this version's spacing and lettering) and the title-screen branding (`rechronicled`); the HD menu and world
  packs (upscales of the game's own pictures) are not part of it.

## About

Made by statically recompiling the game with N64Recomp, running on N64ModernRuntime and the RT64 renderer.
Unofficial: not affiliated with or endorsed by THQ, H2O Entertainment or Nintendo. GPL-3.0 (see `LICENSE`).
Built on blackgamma7's Aidyn Chronicles decompilation (https://github.com/blackgamma7/Aidyn).

Made with heavy AI involvement: nearly all of the code was written by an AI coding assistant (Claude Code), directed
and play-tested by Spitzdano, and the redrawn portraits, icons and world textures were made with an AI image
generator from the game's originals.
