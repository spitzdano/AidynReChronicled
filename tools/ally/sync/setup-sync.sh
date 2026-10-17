#!/usr/bin/env bash
# setup-sync.sh [PC device ID]: on the handheld (Bazzite), sync the game's saves with the PC through Syncthing,
# running only while the game runs. See tools/ally/sync/README.md.
#
# 1. Installs Syncthing (Homebrew) if it isn't there.
# 2. Its user service, not started at login: aidyn-sync-watch (a user service that is) starts it when the game
#    starts and stops it once the PC has the last save. Installs aidyn-sync (aidyn_sync.py) and aidyn-sync-watch in
#    ~/.local/bin.
# 3. If this machine has saves already, asks which saves both machines keep: the PC's or the handheld's. The other
#    side's are kept, never deleted (backups/ here, a .sync-conflict copy on the PC).
# 4. Shares the save folder with the PC (only aidyn.mpk and aidyn-patch.mpk), prints this machine's device ID for
#    the PC to accept, and waits for the first sync.
# Without an ID, the PC's from pc-device-id next to this script (written there on the PC: aidyn_sync.py id).
set -euo pipefail
here=$(dirname "$(readlink -f "$0")")
die() { echo "setup-sync.sh: $*" >&2; exit 1; }

pc=${1:-$(cat "$here/pc-device-id" 2>/dev/null || true)}
pc=${pc^^}
[[ $pc =~ ^[A-Z0-9]{7}(-[A-Z0-9]{7}){7}$ ]] || die "usage: setup-sync.sh [the PC's Syncthing device ID: 8 groups of 7, dashes between]"
pgrep -x AidynReChronicl >/dev/null && die "the game is running: quit it first"
data=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled
units=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user
bin=$HOME/.local/bin

echo "== Syncthing"
if ! command -v syncthing >/dev/null; then
    brew=$(command -v brew || echo /home/linuxbrew/.linuxbrew/bin/brew)
    [[ -x $brew ]] || die "Syncthing isn't installed and Homebrew isn't here to install it"
    "$brew" install syncthing
    PATH=$(dirname "$brew"):$PATH
fi
st=$(command -v syncthing)
"$st" --version
serve="serve --no-browser"
"$st" serve --help 2>&1 | grep -q -- --no-restart && serve+=" --no-restart"

echo "== Services and tools"
mkdir -p "$units" "$bin"
install -m 755 "$here/aidyn_sync.py" "$bin/aidyn-sync"
install -m 755 "$here/aidyn-sync-watch" "$bin/aidyn-sync-watch"
cat >"$units/syncthing.service" <<EOF
[Unit]
Description=Syncthing (Aidyn ReChronicled saves: started by aidyn-sync-watch while the game runs)
After=network-online.target

[Service]
ExecStart=$st $serve
Restart=on-failure
RestartSec=5
SuccessExitStatus=3 4
RestartForceExitStatus=3 4
EOF
cat >"$units/aidyn-sync-watch.service" <<EOF
[Unit]
Description=Aidyn ReChronicled saves: Syncthing while the game runs

[Service]
ExecStart=$bin/aidyn-sync-watch
Restart=always
RestartSec=10

[Install]
WantedBy=default.target
EOF
systemctl --user daemon-reload

if [[ -f $data/aidyn.mpk || -f $data/aidyn-patch.mpk ]]; then
    echo
    echo "This handheld has saves already. Which saves should both machines use from now on?"
    echo "  P  the PC's        (this handheld's are kept in $data/backups)"
    echo "  H  this handheld's (sent to the PC; the PC keeps its own as a .sync-conflict copy)"
    read -rp "P or H? " choice
    mkdir -p "$data/backups"
    stamp=$(date +%Y%m%d-%H%M%S)
    for f in aidyn.mpk aidyn-patch.mpk; do
        [[ -f $data/$f ]] || continue
        cp -p "$data/$f" "$data/backups/${f%.mpk}-handheld-before-sync-$stamp.mpk"
        case ${choice^^} in
            # Syncthing keeps the newer file when both machines have a different one; the older one is kept
            # beside it as <name>.sync-conflict-<date>-<time>-<device>.mpk.
            P) touch -d "2000-01-01" "$data/$f" ;;
            H) touch "$data/$f" ;;
            *) die "answer P or H (nothing was changed but a backup copy)" ;;
        esac
    done
    echo "A copy of this handheld's saves: $data/backups/*-handheld-before-sync-$stamp.mpk"
fi

echo "== Sharing the saves with the PC"
systemctl --user start syncthing.service
trap 'systemctl --user stop syncthing.service' EXIT
"$bin/aidyn-sync" configure --name "Ally"
"$bin/aidyn-sync" add-device "$pc" --name "PC"
me=$("$bin/aidyn-sync" id)
systemctl --user enable --now aidyn-sync-watch.service
cat <<EOF

On the PC, accept this handheld: it shows as waiting there (aidyn_sync.py pending; its ID starts ${me:0:7}):

    python3 ~/Work/aidyn-recomp/tools/ally/sync/aidyn_sync.py add-device $me --name Ally

Waiting for the first sync (both machines on and online; up to 15 minutes, Ctrl+C to stop waiting: it then
happens the next time the game runs)...
EOF
"$bin/aidyn-sync" wait --timeout 900
echo
echo "setup-sync.sh: done. From now on Syncthing runs while the game runs; check with: aidyn-sync status"
