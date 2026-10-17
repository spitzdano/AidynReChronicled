# Aidyn ReChronicled on the ROG Xbox Ally X (Bazzite)

How to move the game from the PC to the handheld over a USB stick, add it to Steam with its art, and play it in Game
Mode. Takes about ten minutes, most of it copying.

Once it's installed, newer versions can come straight from GitHub instead of a USB stick (in Desktop Mode, Konsole;
the repository is private, so the GitHub CLI signs in first; Homebrew comes with Bazzite):

```sh
brew install gh
gh auth login                       # GitHub.com › HTTPS › Yes › Login with a web browser
gh repo clone spitzdano/AidynReChronicled ~/AidynReChronicled -- --depth 1
~/AidynReChronicled/tools/ally/update-from-github.sh          # the newest release (or: … 0.2.1)
```

Later updates: `git -C ~/AidynReChronicled pull`, then the same `update-from-github.sh`. It installs the game only
(`install.sh --no-steam`): saves, settings and the Steam shortcut stay as they are.

> The package contains your own ROM dumps and saves. It's for your own devices: don't share it.

## What's in the package

`AidynReChronicled-Ally.tar` (about 1.6 GB) unpacks to `AidynReChronicled-Ally/`:

| | |
|---|---|
| `install.sh` | Installs everything (below) |
| `game/` | The game built to run on any recent Linux (glibc 2.38 or newer: Bazzite has newer), its own SDL2 and libatomic in `lib/`, the launcher `aidyn-rechronicled.sh`, `steam_shortcut.py`, and the Steam art in `steam-art/` |
| `data/` | `settings.json` tuned for the handheld, your two stored ROM dumps (original and Aidyn Patch), your save `aidyn.mpk`, the three texture packs (HD menus, HD world, title art) and the manual |

On the handheld the game goes to `~/Games/AidynReChronicled` and its data to `~/.local/share/AidynReChronicled`,
the same data folder as on the PC.

## 1. On the PC: put it on a USB stick

The package is already built in `~/Work/aidyn-recomp/build/ally/usb/`. Copy both files to the stick (any format:
the tar stays under FAT32's 4 GB limit):

```sh
cp ~/Work/aidyn-recomp/build/ally/usb/AidynReChronicled-Ally.tar ~/Work/aidyn-recomp/build/ally/usb/HANDOFF.md /run/media/$USER/<STICK>/
sync
```

To rebuild it later (after changes to the game, or to carry newer saves over):

```sh
cd ~/Work/aidyn-recomp
tools/ally/build_portable.sh        # the game, built in a container (Ubuntu 24.04) so it runs anywhere
tools/ally/steam_art.py             # Steam art (needs build/ally/scene.png, see the script)
tools/manual/build_manual.py        # the manual
tools/ally/package.sh               # -> build/ally/usb/
```

## 2. On the handheld: install

1. **Switch to Desktop Mode**: Xbox button › Power › Switch to Desktop.
2. **Plug in the stick.** Open it in Dolphin (the file manager) once so it mounts, under `/run/media/<you>/<STICK>`.
3. **Open Konsole** (application menu › System › Konsole) and unpack the package into your home folder:

   ```sh
   tar -xf /run/media/$USER/<STICK>/AidynReChronicled-Ally.tar -C ~
   ```

   (Type the start of the stick's name and press Tab to complete it.)
4. **Install**:

   ```sh
   bash ~/AidynReChronicled-Ally/install.sh
   ```

   It copies the game and the data into place, starts the game once with `--help` to make sure it runs on this
   system, adds it to the app menu, then asks whether to add it to Steam (answer Y). Steam has to be closed for that
   step; the script asks and closes it. At the end it prints where everything went.
5. **Return to Gaming Mode** (the icon on the desktop).
6. The game is in the **Library › Non-Steam** tab as **Aidyn ReChronicled**, with its cover, banner, logo and icon.
7. `~/AidynReChronicled-Ally` can be deleted now.

If the handheld already has the game, the same steps update it. Your settings and saves on the handheld are kept: when
the PC's differ, they arrive as `settings.json.from-pc` and `aidyn.mpk.from-pc` next to yours (rename one to use it).

### By hand, if the Steam step refuses

`steam_shortcut.py` only writes Steam's shortcut file when it can read it back exactly. If it ever says it can't, add
the game in Desktop Mode instead:

1. Steam › Games › **Add a Non-Steam Game to My Library** › Browse › `~/Games/AidynReChronicled/aidyn-rechronicled.sh` ›
   Add Selected Programs.
2. Right-click it › **Properties**: name it `Aidyn ReChronicled`; click the empty icon square and pick
   `~/Games/AidynReChronicled/icon.png`.
3. Right-click it › Manage › **Set custom artwork**: `steam-art/capsule.png`.
4. On its library page, right-click the empty banner › **Set custom background**: `steam-art/hero.png`; right-click
   again › **Set custom logo**: `steam-art/logo.png`.
5. The wide capsule (`steam-art/wide.png`, the Recent Games row) has no menu for non-Steam games. Copy it into
   `~/.local/share/Steam/userdata/<your id>/config/grid/` as `<app id>.png`; `python3
   ~/Games/AidynReChronicled/steam_shortcut.py --dry-run` prints the app id.

## 3. First start, in Game Mode

- **Proton must be off.** This is a native Linux game. Game page › gear › Properties › Compatibility: "Force the use
  of a specific Steam Play compatibility tool" must be **unchecked**. (If it is on, the game won't start: Proton
  can't run the launcher script.)
- **Controller**: leave Steam Input on with the default **Gamepad** layout. The game sees an Xbox controller.
- It opens fullscreen at the title screen. The first start takes a few seconds longer while the texture packs load.

## Controls on the Ally

The same as with an Xbox controller on the PC (the manual has every screen):

| Ally | In the game |
|---|---|
| Left stick, D-pad | Walk, move in combat, menus |
| Right stick | Turn the camera; up and down tilts it. Click (R3): camera back behind the party |
| Left stick click (L3) | Fast-forward on / off (3x, chevrons at the top right); enemy turns in combat run 2x on their own |
| A | Talk, open, confirm; in combat: attack |
| B | Back, cancel; walking: show the party |
| X | Details; in combat: spells (tap repeats the last, hold + stick picks one) |
| Y | In combat: items and potions (tap repeats the last, hold + stick picks one) |
| View (⧉) | Pause menu: previous party member. **Title screen: switch Original / Aidyn Patch** |
| RT | Pause menu: next party member |
| LT | Sneak (with the stick); combat: end turn; pause menu: previous page |
| LB / RB | Combat: end turn (LB); pause menu: previous / next page; RB + stick: the game's own camera |
| Menu (≡) | Pause menu; back to the game |
| Xbox button | Steam menu: **Exit Game** quits (saving is the game's own: save before you quit) |

The keyboard keys from the PC (F1 manual, F11 fullscreen, Shift+Esc quit) aren't needed in Game Mode.

## Settings for the handheld

`~/.local/share/AidynReChronicled/settings.json` (edit it in Desktop Mode, e.g. with Kate, while the game is
closed). The package's version differs from the PC's in three places:

| Setting | Ally | PC | Why |
|---|---|---|---|
| `fullscreen` | `true` | `false` | Game Mode |
| `supersampling` | 1 | 2 | 2 would draw 4x the pixels |
| `msaa` | 2 | 4 | Lighter on the GPU |
| everything else | same | same | 1080 lines, 16:9 (the Ally's screen), 120 fps, draw distance 2, HD packs |

Check the frame rate with the Steam performance overlay (⋯ button › Performance › Performance Overlay Level 1). The
game itself always runs at 60; above that, frames are drawn in between up to `fps_limit`. If it doesn't hold, lower in
this order: `fps_limit` 90 or 60, `msaa` 0, `draw_distance` 1.5, `render_height` 720. Leave Steam's own frame limit
(⋯ › Performance) off or at 120, or it fights `fps_limit`.

## Saves

Saves live in `~/.local/share/AidynReChronicled/aidyn.mpk` (Aidyn Patch: `aidyn-patch.mpk`), one per device: Steam
Cloud doesn't sync non-Steam games. The package carries the PC's save to the handheld; to carry progress back,
copy `aidyn.mpk` from the handheld to the PC's `~/.local/share/AidynReChronicled/` (keep a copy of the old one). The
files are ordinary N64 Controller Pak images.

## The manual

`~/.local/share/AidynReChronicled/manual/index.html`, and the PDF next to it, open in Desktop Mode. (F1 opens it
on the PC.)

## When something goes wrong

Every run writes `~/.local/share/AidynReChronicled/logs/last-run.log` (the run before: `previous-run.log`). Bring that
file back to the PC if you need help.

| Symptom | Likely cause |
|---|---|
| Nothing happens when you press Play | Proton is on (see First start), or the launcher lost its executable bit: run `chmod +x ~/Games/AidynReChronicled/aidyn-rechronicled.sh ~/Games/AidynReChronicled/AidynReChronicled` |
| Log says "No ROM yet" | The ROMs didn't reach `~/.local/share/AidynReChronicled` (`aidyn.n64.us.1.0.z64`): run `install.sh` again |
| Log says a library is missing | Run `ldd ~/Games/AidynReChronicled/AidynReChronicled | grep "not found"`; everything but SDL2 and libatomic comes from the system |
| Controller does nothing | Steam Input off for the game: game page › controller icon › enable Steam Input |
| Menus or text look like the N64 | A texture pack is missing: check `~/.local/share/AidynReChronicled/texture-packs` has `hd-ui`, `hd-world`, `rechronicled` |
| Stutters | See Settings for the handheld |

## What was tested before handing over

- The handheld build runs on this PC from start to field, all three texture packs, 60 fps (the game) / 100 shown.
- It starts in a bare Fedora 44 container (Bazzite is Fedora-based), needing only `libdbus` from the system, which
  every desktop has.
- The whole install in a Fedora 44 container: unpack, `install.sh` (game, data, the `--help` check, Steam shortcut
  and art for a fresh Steam account), the launcher and its log, and a second install keeping a changed save.
- The installed copy, started through `aidyn-rechronicled.sh` with only its own data folder: finds the stored ROM,
  loads the three packs, title screen at 60 fps.
- `steam_shortcut.py` on a copy of Steam's shortcut file with another shortcut in it: that one is left as it was,
  the game is added once (a second run updates it) and the art is named for its app id.

Not tested: the handheld itself (no remote access to it from the PC). Its GPU (AMD, RADV driver) is a different
driver from the PC's (NVIDIA); RT64 runs on RADV on the Steam Deck.
