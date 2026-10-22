#!/usr/bin/env bash
# windows-steam.sh: the Windows build (tools/windows/build_windows.sh) on this PC as its own Steam shortcut, run with
# GE-Proton, to test it the way a Windows player starts it, next to the Linux one (tools/dev/aidyn-steam.sh).
#
#   tools/dev/windows-steam.sh            copy the latest build/windows/dist into ~/Games/AidynReChronicled-Windows
#                                         (after each rebuild; the game mustn't be running)
#   tools/dev/windows-steam.sh --setup    that, plus (once; Steam must be closed) the shortcut "Aidyn ReChronicled
#                                         (Windows)" forced to the newest GE-Proton, its library art with a WINDOWS ·
#                                         PROTON band (the grid shows no names), and its Proton prefix made with the
#                                         ROMs from ~/.local/share/AidynReChronicled already stored, so no Wine file
#                                         dialog comes up
#
# The folder holds the exe and its DLLs, and links to the texture packs in ~/.local/share/AidynReChronicled/texture-packs
# (always the current ones). Its data folder (settings, saves, logs, reports) is the prefix's:
# ~/.local/share/Steam/steamapps/compatdata/<app id>/pfx/drive_c/users/steamuser/AppData/Roaming/AidynReChronicled,
# apart from the Linux build's.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "windows-steam.sh: $*" >&2; exit 1; }
D=$HOME/Games/AidynReChronicled-Windows
DATA=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled
STEAM=$HOME/.local/share/Steam
NAME="Aidyn ReChronicled (Windows)"

[[ -f build/windows/dist/AidynReChronicled.exe ]] || die "no Windows build yet: tools/windows/build_windows.sh"
pgrep -f '^[A-Za-z]:.*AidynReChronicled\.exe' >/dev/null && die "the Windows build is running: close it first"
mkdir -p "$D/texture-packs"
cp --remove-destination build/windows/dist/* "$D/"
for p in rechronicled rechronicled-art rechronicled-text; do
    [[ -d $DATA/texture-packs/$p ]] && ln -sfn "$DATA/texture-packs/$p" "$D/texture-packs/$p"
done
echo "windows-steam.sh: $D has the build from $(date -r build/windows/dist/AidynReChronicled.exe '+%F %T')"
[[ ${1:-} == --setup ]] || exit 0

pgrep -x steam >/dev/null && die "Steam is running: close it first (steam -shutdown), then run --setup again"
[[ -f build/ally/steam-art/capsule.png ]] || python3 tools/ally/steam_art.py >/dev/null
rm -rf "$D/steam-art" && cp -a build/ally/steam-art "$D/steam-art"
font=tools/hdui/fonts/MarcellusSC-Regular.ttf
for f in capsule wide; do
    read -r w ht < <(magick identify -format '%w %h\n' "$D/steam-art/$f.png")
    h=$(( (w < ht ? w : ht) / 9 ))
    magick "$D/steam-art/$f.png" \( -size "${w}x$h" xc:'rgba(10,20,60,0.85)' -font "$font" -pointsize $(( h * 11 / 20 )) \
        -fill '#f2e3b3' -gravity center -annotate +0+0 'WINDOWS · PROTON' \) -gravity north -composite "$D/steam-art/$f.png"
done
tool=$(basename "$(ls -d "$STEAM"/compatibilitytools.d/GE-Proton*/ | sort -V | tail -1)")
out=$(python3 tools/ally/steam_shortcut.py --launcher "$D/AidynReChronicled.exe" --art "$D/steam-art" \
      --icon "$D/steam-art/icon.png" --name "$NAME" --compat-tool "$tool")
echo "$out"
appid=$(grep -o 'app id [0-9]*' <<<"$out" | head -1 | cut -d' ' -f3)

# The prefix Steam will use for it, made now with the same tool, and the ROMs stored in its data folder.
prefix=$STEAM/steamapps/compatdata/$appid
if [[ ! -d $prefix/pfx ]]; then
    echo "== Proton prefix ($tool)"
    mkdir -p "$prefix"
    STEAM_COMPAT_DATA_PATH=$prefix STEAM_COMPAT_CLIENT_INSTALL_PATH=$STEAM \
        "$STEAM/compatibilitytools.d/$tool/proton" run wineboot -u >/dev/null 2>&1 || true
fi
appdata=$prefix/pfx/drive_c/users/steamuser/AppData/Roaming/AidynReChronicled
mkdir -p "$appdata"
for rom in "$DATA"/aidyn.n64.us.1.0*.z64; do
    [[ -f $appdata/$(basename "$rom") ]] || cp "$rom" "$appdata/"
done
echo "windows-steam.sh: ROMs stored in $appdata"
