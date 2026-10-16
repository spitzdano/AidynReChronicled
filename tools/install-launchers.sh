#!/usr/bin/env bash
# install-launchers.sh: app-launcher entries for the two versions, in ~/.local/share/applications.
#   Aidyn ReChronicled               --original (the NTSC-U 1.0 release)
#   Aidyn ReChronicled (Aidyn Patch) --patch
# Each starts the version it names; View on the title screen still switches (and restarts in place). Both have a
# "Manual" action that opens the manual tools/manual/build_manual.py built (F1 in the game opens it too).
#   tools/install-launchers.sh            install (or refresh after moving the repo)
#   tools/install-launchers.sh --remove   remove them
set -euo pipefail
P=$(dirname "$(dirname "$(readlink -f "$0")")")
D=${XDG_DATA_HOME:-$HOME/.local/share}/applications
exe=$P/build/cmake/AidynReChronicled
manual=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/manual/index.html
if [[ ${1:-} == --remove ]]; then
    rm -f "$D/aidyn-rechronicled.desktop" "$D/aidyn-rechronicled-patch.desktop"
    echo "install-launchers.sh: removed"
    exit 0
fi
[[ -x $exe ]] || { echo "install-launchers.sh: $exe not built yet" >&2; exit 1; }
mkdir -p "$D"
entry() {  # file name flag comment
    cat >"$D/$1" <<DESKTOP
[Desktop Entry]
Type=Application
Name=$2
Comment=$4
Exec="$exe" $3
Path=$P
Icon=applications-games
Terminal=false
Categories=Game;RolePlaying;
StartupWMClass=AidynReChronicled
Actions=manual;

[Desktop Action manual]
Name=Manual
Exec=xdg-open "$manual"
DESKTOP
}
entry aidyn-rechronicled.desktop "Aidyn ReChronicled" --original "Aidyn Chronicles: The First Mage, enhanced (original 1.0 release)"
entry aidyn-rechronicled-patch.desktop "Aidyn ReChronicled (Aidyn Patch)" --patch "Aidyn Chronicles: The First Mage, enhanced, with the community Aidyn Patch"
echo "install-launchers.sh: $D/aidyn-rechronicled.desktop, $D/aidyn-rechronicled-patch.desktop"
