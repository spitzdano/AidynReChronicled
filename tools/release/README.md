# Aidyn ReChronicled

*Aidyn Chronicles: The First Mage* (Nintendo 64, 2001) as a native game for Linux and Windows, enhanced: high
resolution, widescreen, up to 240 fps, a free camera, further draw distance, quicksaves and autosaves, a faster walk,
and the community rebalances Aidyn Patch and Aidyn Plus a button away.

> **Work in progress.** This is a hobby project I'm making for myself, in my own time. Expect rough edges, unfinished
> parts and changes between releases, and no schedule. For lack of time I haven't played very far into the game yet,
> so playtests and problem reports from further along are very welcome (see "Found a problem?" below);
> I'll work on them when I can.

**What's new:** `RELEASE-NOTES.md`, next to this file, has what each release changed, newest first.

## Features

- **A native game**, not an emulator: for Linux, one download for any current distribution, with an installer that
  adds it to the app menu and to Steam (Steam Deck and ROG Ally friendly); for Windows, a folder to unpack and play.
- **Aidyn Patch and Aidyn Plus**: halibabica's community rebalances play as two more versions next to the original.
  Bring your own patched ROM (checked by its hash; nothing of either patch ships here), switch on the title screen, and
  each version keeps its own saves.
- **Sharp at any resolution**: drawn at 1080 lines or more, supersampled, with anti-aliasing.
- **Widescreen**: 16:9, 21:9 or any width, with the HUD at the screen edges; the menus stay 4:3, as they were made.
- **Smooth**: the game runs at 60 frames a second (the N64 managed 15 to 20), with frames drawn in between up to your
  display's rate, and walk cycles without their stutter.
- **Newly painted art**: 2,113 of the game's 2,115 world textures, every portrait, an icon of its own for every item
  (each potion its own colour, scrolls and wands by school), the HUD, the map page's land and a repainted pause scroll,
  plus HD lettering and Xbox button prompts. F2 shows what was there before.
- **Further draw distance**: more of the land drawn, trees and objects showing from much further away.
- **A free camera** on the right stick, kept out of walls.
- **Quicksave, autosaves, Quick Load and Continue**, in files of their own beside the Controller Pak.
- **Fast-forward** (3x) and a **faster walk** (1.5x by default, up to 2x).
- **Clearer combat**: a turn-order bar, a combat log, framed portraits, easier aiming, and what a spell or potion does
  shown while you choose it.
- **Help built in**: a controls card on screen, button hints in the menus, a ReChronicled topic in the help book and a
  PC edition of the manual (F1).
- **Changed while you play**: the Display and ReChronicled pages of the pause menu hold all of this edition's options.
- **Safe saves**: the Controller Pak's last 20 states are kept, an emulator's save (Project64, ares, Mupen64Plus,
  RetroArch) can be copied in, and a problem report gathers what's needed when something goes wrong.

**You need your own copy of the game:** a dump of the North American 1.0 cartridge (`.z64`, SHA-1
`47ae795a2b12783a13e2b8d03439ce9a39fc826c`). Nothing from the game is in this download.

## Play on Linux

Unpack the download (`…-linux-x86_64.tar.gz`), then either:

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

Needs: Linux on x86_64 with glibc 2.38 or newer (any current distribution: Bazzite, Fedora, Ubuntu 24.04, Arch…), a
Vulkan graphics driver, and libdbus (every desktop has it). Everything else is in `game/lib`.

## Play on Windows

Unpack the download (`…-windows-x86_64.zip`) wherever you like (Documents\Games, say) and run
`AidynReChronicled.exe`. The game isn't signed, so the first time Windows may say "Windows protected your PC": choose
**More info** › **Run anyway**.

Needs: Windows 10 or 11 (64-bit) and a graphics driver with Vulkan (any current NVIDIA, AMD or Intel driver).
Everything else is in the folder, Microsoft's Visual C++ runtime included (`msvcp140.dll`, `vcruntime140.dll`,
`vcruntime140_1.dll`, redistributed under Microsoft's terms): nothing to install.

**Adding it to Steam:** double-click **`Add to Steam.bat`**. It adds "Aidyn ReChronicled" as a non-Steam game with its
icon and library art (Steam closes for a moment and starts again; running it again updates the shortcut, so do it
after moving the folder). By hand instead: Steam › Games › **Add a Non-Steam Game to My Library** › Browse ›
`AidynReChronicled.exe` › Add Selected Programs, then right-click it › **Properties** and name it `Aidyn ReChronicled`.
Its library art is in the `steam-art` folder (right-click it › Manage › Set custom artwork: `capsule.png`; on its
page, right-click the banner: `hero.png` and `logo.png`).

## Your ROM and the data folder

**Your ROMs go in the `ROMs` folder** of the download: the 1.0 ROM, and if you have them, the same with Aidyn Patch
or Aidyn Plus applied. Any file names: the game recognises each by its contents when it starts, keeps a copy in the
data folder, and lists the versions it found on the title screen. (On Linux, `install.sh` copies them to the installed
game's `ROMs` folder, `~/Games/AidynReChronicled/ROMs`, and an update keeps that folder.) Without a ROM there, the
first start asks for it in a file dialog (or start the game with `--rom` and the path to it). A zipped ROM has to be
unzipped first.

The **data folder** holds the ROMs, saves, settings, logs and problem reports:

- Linux: `~/.local/share/AidynReChronicled`
- Windows: `%APPDATA%\AidynReChronicled` (type that into File Explorer's address bar)

## Found a problem?

Please report it: a crash, a freeze, a glitch, a wrong picture, anything that looks off.

1. **Make a problem report.** If the game crashed, it already made one and said where. Otherwise pause as close as you
   can to the problem and choose **Report a problem** at the bottom of the **ReChronicled** page (on the pause
   menu's Menu page): it also keeps a picture of the screen and a save of that moment.
2. **Open an issue** with the [Problem report form](https://github.com/spitzdano/AidynReChronicled/issues/new?template=problem-report.yml) and drag in the newest `.zip` from
   `reports` in the data folder (on a Steam Deck or an Ally, switch to Desktop Mode to reach it).
3. **Say what happened** and what you were doing just before.

A report holds the game's logs, your settings and your saves, nothing from the game's ROM, and your home folder's
name is left out of it.

## Aidyn Patch and Aidyn Plus

Two community rebalances by halibabica (u/halibabica) can be played instead of the original: Aidyn Patch and the bigger
Aidyn Plus (read its FAQ). Neither is in this download: get the patch yourself and apply it to your own 1.0 dump (Aidyn Plus is
a `.bps` patch: apply it with Flips or another BPS patcher), then put the patched file in the `ROMs` folder and
switch to it on the title screen (on Linux, the app menu's "Aidyn ReChronicled (Aidyn Patch)" and "(Aidyn Plus)"
entries, or `--patch` / `--plus`, also ask for it when it's missing). It is checked by its hash (SHA-1 Aidyn Patch `1f95ad78824f491ea3d94e3ac5d36901cef09e4e`, Aidyn Plus
`d57f9459a77c66490ff09b3d9d97545c7a65fad2`) and kept. You can have none, one or both.

On the title screen the bottom left lists Original, Aidyn Patch and Aidyn Plus, the one you're playing lit; X and Y
(keyboard K and I) switch to the other two, as marked. One you haven't added is grey and marked *not found*: put its
ROM in the `ROMs` folder and start the game again. If the version you played last has no ROM (you only have Aidyn
Plus, say), the game plays one that has. From the
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
| | F11, Alt+Enter / Shift+Esc | Windowed or borderless full screen (also the Display page's Window row; remembered) / quit |

## Settings, saves, logs

All in the data folder (see "Your ROM and the data folder"):

- `settings.json`: resolution, aspect (`"16:9"`, `"21:9"`, `"4:3"`…), frame rate, anti-aliasing, draw distance and
  more, written with defaults on the first start. Edit it while the game is closed. `camera_invert_y` /
  `camera_invert_x` invert the camera.
- `aidyn.mpk` (`aidyn-patch.mpk`): your saves, a Controller Pak image like an emulator's (an emulator's own can be
  copied in under this name: Project64's and ares' as they are, Mupen64Plus' and RetroArch's are converted when the game
  starts, the original kept in `backups/`). `backups/` keeps its last
  20 states (at each start and before each save).
- `aidyn-patch-quicksave.sav`, `-autosave-1.sav`, `-autosave-2.sav`, `-lastsave.sav`: the quicksave (View while
  walking about), the two newest autosaves and a copy of the last save made on the pak, for Quick Load and Continue.
- `logs/last-run.log`: what the last start printed. Look there if something goes wrong.
- `reports/`: problem reports, one `.zip` each with the logs, settings and saves: written by the game when it
  crashes, or by **Report a problem** on the pause menu's ReChronicled page. Send the newest with a few words on what
  happened.
- `texture-packs/`: HD texture packs of your own (folders, or `.rtz` files). This release includes, next to the game (in its own
  `texture-packs`), newly painted art (`rechronicled-art`: every portrait, the item and equipment-slot icons, the button prompts, redrawn HUD pieces, the map page's land and
  2,113 of the 2,115 world textures), the HD letters and Xbox button
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
