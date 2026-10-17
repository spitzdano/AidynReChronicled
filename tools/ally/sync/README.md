# Saves on the PC and the handheld

Aidyn ReChronicled keeps its saves in one file per version of the game, `aidyn.mpk` (and `aidyn-patch.mpk` for the
Aidyn Patch), in `~/.local/share/AidynReChronicled`. [Syncthing](https://syncthing.net) keeps those two files the
same on both machines, directly between them (no cloud account; encrypted; at home over the network, away from home
over the internet through Syncthing's relays).

- **On the PC** Syncthing runs all the time (it's light), so it can always take a save from the handheld.
- **On the handheld** it runs only while the game runs: `aidyn-sync-watch` starts it when the game starts and
  stops it once the PC has the last save (at most 10 minutes after quitting, if the PC can't be reached; the save
  then goes the next time both are on).
- Only the saves sync. Settings, stored ROMs, logs and `backups/` stay on each machine.
- Every save that gets replaced by the other machine's is kept for a while in `.stversions/` in the save folder (the
  last 10), next to the game's own `backups/`.

The game (0.2.1 or newer) notices when its save file changes while it runs, so you can put the handheld to sleep
mid-game, play and save on the PC, and when you wake the handheld, its save and load screens show the PC's saves
(load one to continue from there; give it a minute after waking to reconnect). What you hadn't saved on the
handheld is still its own.

## On the PC (done)

```sh
sudo pacman -S syncthing
systemctl --user enable --now syncthing.service
python3 tools/ally/sync/aidyn_sync.py configure --name PC
python3 tools/ally/sync/aidyn_sync.py id > tools/ally/sync/pc-device-id    # committed, for the handheld
```

## On the handheld (Bazzite), once

In **Desktop Mode** (Xbox button › Power › Switch to Desktop), open **Konsole**:

1. **The GitHub CLI**, and sign in (the repository is private). Homebrew comes with Bazzite.

   ```sh
   brew install gh
   gh auth login
   ```

   Answer: GitHub.com › HTTPS › Yes (authenticate Git with your GitHub credentials) › Login with a web browser. It
   shows a code and opens the browser; sign in and enter the code.

2. **The repository** (only the newest version of each file: about 35 MB):

   ```sh
   gh repo clone spitzdano/AidynReChronicled ~/AidynReChronicled -- --depth 1
   ```

3. **The newest game** (0.2.1 or newer is needed for syncing):

   ```sh
   ~/AidynReChronicled/tools/ally/update-from-github.sh
   ```

4. **Save syncing**. The game must not be running.

   ```sh
   bash ~/AidynReChronicled/tools/ally/sync/setup-sync.sh
   ```

   It installs Syncthing, asks **which saves both machines should use from now on** (P: the PC's, or H: the
   handheld's; the other machine's are kept as a backup either way), then prints this handheld's device ID and
   waits. **On the PC**, accept the handheld: its ID shows with `aidyn_sync.py pending`, then

   ```sh
   python3 ~/Work/aidyn-recomp/tools/ally/sync/aidyn_sync.py add-device <the handheld's ID> --name Ally
   ```

   (or tell Claude on the PC that the handheld is waiting). The script finishes when the first sync is done.

5. Back to Game Mode and play.

## Later

| | |
|---|---|
| Is it working? | `aidyn-sync status` on the handheld, `python3 ~/Work/aidyn-recomp/tools/ally/sync/aidyn_sync.py status` on the PC: connected, and how much of the saves the other machine has |
| What did the watcher do? | `journalctl --user -u aidyn-sync-watch -n 20` |
| Update the game | `git -C ~/AidynReChronicled pull`, then `~/AidynReChronicled/tools/ally/update-from-github.sh` (or `… 0.3.0` for a given version) |
| A save went missing | the other machine's version is in `~/.local/share/AidynReChronicled/.stversions/` or a `aidyn.sync-conflict-….mpk` next to `aidyn.mpk`; the game's own copies are in `backups/`. Quit the game and copy one over `aidyn.mpk`. |
| Settings page | Syncthing's own: http://127.0.0.1:8384 (on the handheld only while it runs) |
| Turn it off | handheld: `systemctl --user disable --now aidyn-sync-watch.service`; PC: `systemctl --user disable --now syncthing.service` |

Both machines playing at the same time and saving both: Syncthing keeps the newer file and puts the other next to it
as `aidyn.sync-conflict-<date>-<time>-<device>.mpk`; nothing is lost, but you pick by copying.
