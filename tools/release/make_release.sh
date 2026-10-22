#!/usr/bin/env bash
# make_release.sh: build a release and publish it on GitHub (spitzdano/AidynReChronicled).
#
#   tools/release/make_release.sh <version>             build build/release/AidynReChronicled-<version>-linux-x86_64.tar.gz
#   tools/release/make_release.sh <version> --windows   ... and AidynReChronicled-<version>-windows-x86_64.zip
#   tools/release/make_release.sh <version> [--windows] --publish   ... then tag v<version>, push the tag, create the
#                                                       GitHub release with them
#
# The download holds the portable build (tools/ally/build_portable.sh: glibc 2.38+, its own SDL2), the launcher, the
# installer, the Steam shortcut script and its library art, the icon, the title branding pack, the redrawn portraits and icons
# (rechronicled-art: tools/hdui/art_pack.py from art/) and the HD letters (rechronicled-text: tools/hdui/text_pack.py),
# all made here from fonts and our own art,
# README.md (tools/release/README.md) and LICENSE. Nothing from the ROM: the game asks for the player's ROM on its
# first start. --publish needs a clean, pushed main branch, so the tag is the commit the binaries were built from.
# The Windows zip (tools/windows/build_windows.sh, cross-compiled here) holds the exe and its DLLs, the same packs and
# Steam art, README.md and LICENSE, in one folder to unpack and play.
# tools/release/notes/<version>.md, if there, goes into the release notes (what's new).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "make_release.sh: $*" >&2; exit 1; }

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?$ ]] || die "usage: make_release.sh <version, e.g. 0.1.0> [--publish]"
publish=0 windows=0
for a in "${@:2}"; do
    case $a in
        --publish) publish=1 ;;
        --windows) windows=1 ;;
        *) die "unknown option $a" ;;
    esac
done
tag=v$version
name=AidynReChronicled-$version-linux-x86_64
out=build/release
packs=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/texture-packs
pack=$packs/rechronicled

if (( publish )); then
    [[ -z $(git status --porcelain) ]] || die "uncommitted changes: commit and push first"
    git fetch -q origin main
    [[ $(git rev-parse HEAD) == $(git rev-parse origin/main) ]] || die "HEAD isn't origin/main: push first"
    git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "tag $tag exists already"
fi

echo "== portable build"
AIDYN_VERSION=$version tools/ally/build_portable.sh  # the title screen shows it
# The binary before strip, kept here and never shipped, to read testers' problem reports: the offsets in a report's
# game-crash.txt (AidynReChronicled(+0x2be600)) are its functions: addr2line -f -C -e $out/symbols/AidynReChronicled-<version> 0x2be600
mkdir -p "$out/symbols"
cp build/portable/AidynReChronicled "$out/symbols/AidynReChronicled-$version"
if [[ ! -f $pack/rt64.json ]]; then
    echo "== title branding pack"
    python3 tools/branding/make_branding.py
fi
echo "== redrawn art pack"
python3 tools/hdui/art_pack.py
echo "== HD letters pack"
python3 tools/hdui/text_pack.py

echo "== $name"
rm -rf "$out/$name" "$out/$name.tar.gz" "$out/$name.tar.gz.sha256"
mkdir -p "$out/$name/game/texture-packs"
cp -a build/portable/dist/. "$out/$name/game/"
cp tools/ally/aidyn-rechronicled.sh tools/ally/steam_shortcut.py "$out/$name/game/"
python3 tools/ally/steam_art.py --icon "$out/$name/game/icon.png" >/dev/null
# Steam library art (tools/ally/steam_art.py: the box painting from art/steam with the title card): steam_shortcut.py,
# run by install.sh, puts it into Steam.
python3 tools/ally/steam_art.py >/dev/null
cp -a build/ally/steam-art "$out/$name/game/steam-art"
cp -a "$pack" "$out/$name/game/texture-packs/rechronicled"
cp -a "$packs/rechronicled-art" "$out/$name/game/texture-packs/rechronicled-art"
cp -a "$packs/rechronicled-text" "$out/$name/game/texture-packs/rechronicled-text"
cp tools/ally/install.sh tools/release/README.md LICENSE "$out/$name/"
mkdir -p "$out/$name/ROMs"  # the player's ROMs go here (src/main/main.cpp, store_roms_from_folders)
cp tools/release/ROMs-README.txt "$out/$name/ROMs/README.txt"
chmod +x "$out/$name/install.sh" "$out/$name/game/aidyn-rechronicled.sh" "$out/$name/game/steam_shortcut.py"
# Nothing ROM-derived may ship: the only pictures are the icon, the branding pack and the redrawn art.
stray=$(find "$out/$name" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.mpk' \) | head -1)
[[ -z $stray ]] || die "a ROM or save got into the release: $stray"
tar -C "$out" -czf "$out/$name.tar.gz" "$name"
(cd "$out" && sha256sum "$name.tar.gz" >"$name.tar.gz.sha256")
echo "make_release.sh: $out/$name.tar.gz ($(du -h "$out/$name.tar.gz" | cut -f1))"
assets=("$out/$name.tar.gz" "$out/$name.tar.gz.sha256")

if (( windows )); then
    wname=AidynReChronicled-$version-windows-x86_64
    echo "== Windows build"
    AIDYN_VERSION=$version tools/windows/build_windows.sh
    # The exe and its .pdb, kept here and never shipped, to read a Windows report's game-crash.txt
    # (AidynReChronicled.exe(+0x81e50)): llvm-symbolizer --obj=$out/symbols/windows-<version>/AidynReChronicled.exe 0x140081e50
    # (the offset plus the image base, 0x140000000).
    mkdir -p "$out/symbols/windows-$version"
    cp build/windows/AidynReChronicled.exe build/windows/AidynReChronicled.pdb "$out/symbols/windows-$version/"
    echo "== $wname"
    rm -rf "$out/$wname" "$out/$wname.zip" "$out/$wname.zip.sha256"
    mkdir -p "$out/$wname/texture-packs"
    cp -a build/windows/dist/. "$out/$wname/"
    cp -a build/ally/steam-art "$out/$wname/steam-art"
    for p in rechronicled rechronicled-art rechronicled-text; do
        cp -a "$out/$name/game/texture-packs/$p" "$out/$wname/texture-packs/$p"
    done
    cp tools/release/README.md LICENSE "$out/$wname/"
    mkdir -p "$out/$wname/ROMs"
    sed 's/$/\r/' tools/release/ROMs-README.txt >"$out/$wname/ROMs/README.txt"
    stray=$(find "$out/$wname" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.mpk' \) | head -1)
    [[ -z $stray ]] || die "a ROM or save got into the Windows release: $stray"
    (cd "$out" && python3 -c 'import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(sys.argv[2]):
        dirs.sort()
        for f in sorted(files):
            z.write(os.path.join(root, f))' "$wname.zip" "$wname" && sha256sum "$wname.zip" >"$wname.zip.sha256")
    echo "make_release.sh: $out/$wname.zip ($(du -h "$out/$wname.zip" | cut -f1))"
    assets+=("$out/$wname.zip" "$out/$wname.zip.sha256")
fi

(( publish )) || exit 0
echo "== GitHub release $tag"
git tag -a "$tag" -m "Aidyn ReChronicled $version"
git push -q origin "$tag"
notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
{
    if (( windows )); then
        echo "Aidyn ReChronicled $version for Linux and Windows (x86_64)."
    else
        echo "Aidyn ReChronicled $version for Linux (x86_64)."
    fi
    echo
    echo "**Play on Linux:** download \`$name.tar.gz\`, unpack it, run \`bash install.sh\` (or"
    echo "\`game/aidyn-rechronicled.sh\` in place). Needs glibc 2.38+ (any current distribution), a Vulkan driver and libdbus."
    echo
    if (( windows )); then
        echo "**Play on Windows:** download \`$wname.zip\`, unpack it anywhere, run \`AidynReChronicled.exe\`. Needs"
        echo "Windows 10 or 11 (64-bit), a Vulkan driver and the Visual C++ Redistributable 2015-2022 (x64)."
        echo
    fi
    echo "The first start asks for your own NTSC-U 1.0 ROM (\`.z64\`) and keeps it. The README in the download has the"
    echo "rest: controls, Aidyn Patch, settings, adding it to Steam."
    echo
    echo "Contains no game data. Includes the newly painted art (portraits, item and slot icons, button prompts, HUD"
    echo "pieces, world textures); the HD menu and world texture packs (upscales of the game's own pictures) aren't part of it."
    echo
    if [[ -f tools/release/notes/$version.md ]]; then
        cat "tools/release/notes/$version.md"
        echo
    fi
    echo "Built from $(git rev-parse --short HEAD). SHA-256: \`$(cut -d' ' -f1 "$out/$name.tar.gz.sha256")\` (Linux)"
    if (( windows )); then
        echo "\`$(cut -d' ' -f1 "$out/$wname.zip.sha256")\` (Windows)"
    fi
} >"$notes"
gh release create "$tag" "${assets[@]}" --verify-tag \
    --title "Aidyn ReChronicled $version" --notes-file "$notes"
echo "make_release.sh: published $tag"
