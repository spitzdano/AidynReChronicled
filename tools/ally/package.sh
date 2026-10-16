#!/usr/bin/env bash
# package.sh: everything the handheld needs, as one file for a USB stick -> build/ally/usb/
#
#   tools/ally/package.sh
#
# build/ally/usb/AidynReChronicled-Ally.tar holds AidynReChronicled-Ally/ with:
#   install.sh, HANDOFF.md                 (tools/ally)
#   game/  AidynReChronicled + lib/       (tools/ally/build_portable.sh)
#          aidyn-rechronicled.sh, steam_shortcut.py, steam-art/ (tools/ally/steam_art.py)
#   data/  settings.json                  (tools/ally/settings-ally.json: tuned for a 1080p handheld)
#          the stored ROMs and saves, texture-packs/, manual/   (from ~/.local/share/AidynReChronicled)
# and HANDOFF.md sits next to the tar. One tar instead of 72,000 files: copies to any stick (FAT32 too: it stays under
# 4 GB) and keeps the executable bits. It contains your ROM dumps: it's for your own devices, never to be shared.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
data=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled
stage=build/ally/stage/AidynReChronicled-Ally
usb=build/ally/usb

die() { echo "package.sh: $*" >&2; exit 1; }
[[ -x build/portable/dist/AidynReChronicled ]] || die "no portable build: tools/ally/build_portable.sh"
[[ -f build/ally/steam-art/logo.png ]] || die "no Steam art: tools/ally/steam_art.py"
[[ -f $data/manual/index.html ]] || die "no manual: tools/manual/build_manual.py"
[[ -f $data/aidyn.n64.us.1.0.z64 ]] || die "no stored ROM in $data (run the game once with --rom)"
for p in hd-ui hd-world rechronicled-art rechronicled; do [[ -d $data/texture-packs/$p ]] || die "texture pack $p missing in $data"; done

rm -rf build/ally/stage
mkdir -p "$stage/game" "$stage/data/texture-packs" "$usb"
cp tools/ally/install.sh tools/ally/HANDOFF.md "$stage/"
cp -a build/portable/dist/. "$stage/game/"
cp tools/ally/aidyn-rechronicled.sh tools/ally/steam_shortcut.py "$stage/game/"
cp -a build/ally/steam-art "$stage/game/steam-art"
cp build/ally/steam-art/icon.png "$stage/game/icon.png"
cp tools/ally/settings-ally.json "$stage/data/settings.json"
cp "$data"/*.z64 "$stage/data/"
cp "$data"/*.mpk "$stage/data/" 2>/dev/null || true
# The big folders go in through symlinks (tar -h follows them), so they aren't copied twice.
for p in hd-ui hd-world rechronicled-art rechronicled; do ln -s "$data/texture-packs/$p" "$stage/data/texture-packs/$p"; done
ln -s "$data/manual" "$stage/data/manual"

rm -f "$usb/AidynReChronicled-Ally.tar"
tar -chf "$usb/AidynReChronicled-Ally.tar" -C build/ally/stage AidynReChronicled-Ally
cp tools/ally/HANDOFF.md "$usb/"
rm -rf build/ally/stage
size=$(du -h "$usb/AidynReChronicled-Ally.tar" | cut -f1)
echo "package.sh: $usb/AidynReChronicled-Ally.tar ($size) and $usb/HANDOFF.md"
echo "  saves included: $(cd "$data" && ls *.mpk 2>/dev/null | tr '\n' ' ')"
