#!/usr/bin/env bash
# install.sh: install Aidyn ReChronicled from this folder: a GitHub release, or the handheld package
# (tools/ally/package.sh), unpacked.
#
#   bash install.sh             game -> ~/Games/AidynReChronicled, app-launcher entries, then (after asking) the
#                               Steam shortcut; with a data/ folder (the handheld package) also the data
#   bash install.sh --no-steam  without the Steam step
#
# The data folder (~/.local/share/AidynReChronicled) is only touched when this folder has a data/ folder, and never
# overwrites what is yours: an existing settings.json or save (*.mpk) that differs stays, and the package's copy is
# put next to it as <name>.from-pc. Safe to run again (an update: the same command with a newer download).
set -euo pipefail
src=$(dirname "$(readlink -f "$0")")
game=$HOME/Games/AidynReChronicled
data=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled
apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
steam=1
[[ ${1:-} == --no-steam ]] && steam=0

say() { printf '\n== %s\n' "$*"; }
die() { printf '\ninstall.sh: %s\n' "$*" >&2; exit 1; }

[[ $(uname -m) == x86_64 ]] || die "this build is for x86_64 Linux"
[[ -x $src/game/AidynReChronicled ]] || die "run this from the unpacked folder (the one with game/ in it)"
need=$(du -sm "$src" | cut -f1)
free=$(df -Pm "$HOME" | awk 'NR == 2 {print $4}')
(( free > need + 200 )) || die "not enough free space in $HOME: ${free} MB free, about ${need} MB needed"

say "Game -> $game"
if [[ $(readlink -f "$src/game") != $(readlink -f "$game" 2>/dev/null || true) ]]; then
    mkdir -p "$(dirname "$game")"
    rm -rf "$game.new"
    cp -a "$src/game" "$game.new"
    rm -rf "$game"
    mv "$game.new" "$game"
fi
chmod +x "$game/AidynReChronicled" "$game/aidyn-rechronicled.sh"

say "Checking that it runs here"
if ! out=$("$game/AidynReChronicled" --help 2>&1); then
    echo "$out"
    echo "Libraries it can't find:"
    ldd "$game/AidynReChronicled" | grep "not found" || true
    die "the game doesn't start on this system (see above)"
fi
echo "ok"

if [[ -d $src/data ]]; then
    say "Data -> $data"
    mkdir -p "$data/texture-packs"
    cp -f "$src"/data/*.z64 "$data/" 2>/dev/null || true
    for f in settings.json "$src"/data/*.mpk; do
        f=$(basename "$f")
        [[ -e $src/data/$f ]] || continue
        if [[ -e $data/$f ]] && ! cmp -s "$src/data/$f" "$data/$f"; then
            cp -f "$src/data/$f" "$data/$f.from-pc"
            echo "kept your $f; the PC's copy is $f.from-pc (rename it to use it)"
        else
            cp -f "$src/data/$f" "$data/$f"
        fi
    done
    for p in "$src"/data/texture-packs/*/; do
        [[ -d $p ]] || continue
        p=$(basename "$p")
        echo "texture pack $p"
        rm -rf "$data/texture-packs/$p"
        cp -a "$src/data/texture-packs/$p" "$data/texture-packs/$p"
    done
    if [[ -d $src/data/manual ]]; then
        rm -rf "$data/manual"
        cp -a "$src/data/manual" "$data/manual"
    fi
fi

say "App launcher"
mkdir -p "$apps"
entry() {  # file name flag comment
    cat >"$apps/$1" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$2
Comment=$4
Exec="$game/aidyn-rechronicled.sh" $3
Path=$game
Icon=$game/icon.png
Terminal=false
Categories=Game;RolePlaying;
StartupWMClass=AidynReChronicled
Actions=manual;

[Desktop Action manual]
Name=Manual
Exec=xdg-open "$data/manual/index.html"
DESKTOP
}
entry aidyn-rechronicled.desktop "Aidyn ReChronicled" --original "Aidyn Chronicles: The First Mage, enhanced"
entry aidyn-rechronicled-patch.desktop "Aidyn ReChronicled (Aidyn Patch)" --patch \
    "Aidyn Chronicles: The First Mage, enhanced, with the community Aidyn Patch"
entry aidyn-rechronicled-plus.desktop "Aidyn ReChronicled (Aidyn Plus)" --plus \
    "Aidyn Chronicles: The First Mage, enhanced, with the Aidyn Plus rebalance"
echo "Aidyn ReChronicled, Aidyn ReChronicled (Aidyn Patch), Aidyn ReChronicled (Aidyn Plus)"

if (( steam )); then
    if [[ ! -d $HOME/.local/share/Steam/userdata && ! -d $HOME/.steam/steam/userdata
          && ! -d $HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/userdata ]]; then
        echo; echo "(No Steam here: skipped the Steam shortcut.)"
    else
        say "Steam shortcut"
        read -r -p "Add Aidyn ReChronicled to Steam as a non-Steam game? [Y/n] " a || a=n
        if [[ ${a,,} != n* ]]; then
            if pgrep -x steam >/dev/null; then
                read -r -p "Steam is running and has to close for this. Close it now? [Y/n] " a || a=n
                [[ ${a,,} == n* ]] && die "Steam left open. Close it yourself, then run: python3 $game/steam_shortcut.py"
                steam -shutdown >/dev/null 2>&1 || true
                for _ in $(seq 1 60); do pgrep -x steam >/dev/null || break; sleep 1; done
                pgrep -x steam >/dev/null && die "Steam didn't close. Exit it from its menu, then run: python3 $game/steam_shortcut.py"
            fi
            python3 "$game/steam_shortcut.py"
        fi
    fi
fi

say "Installed"
cat <<EOF
Game:   $game  (start it from the app menu, Steam, or $game/aidyn-rechronicled.sh)
Data:   $data  (settings.json, saves, the stored ROM, texture packs)
Logs:   $data/logs/last-run.log
First start: the game asks for your ROM (NTSC-U 1.0 .z64) once, then keeps it.
This folder ($src) isn't needed any more and can be deleted.
EOF
