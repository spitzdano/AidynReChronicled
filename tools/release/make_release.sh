#!/usr/bin/env bash
# make_release.sh: build a release and publish it on GitHub (spitzdano/AidynReChronicled).
#
#   tools/release/make_release.sh <version>             build build/release/AidynReChronicled-<version>-linux-x86_64.tar.gz
#   tools/release/make_release.sh <version> --publish   ... then tag v<version>, push the tag, create the GitHub release
#
# The download holds the portable build (tools/ally/build_portable.sh: glibc 2.38+, its own SDL2), the launcher, the
# installer, the Steam shortcut script and its library art, the icon, the title branding pack, the redrawn portraits and icons
# (rechronicled-art: tools/hdui/art_pack.py from art/) and the HD letters (rechronicled-text: tools/hdui/text_pack.py),
# all made here from fonts and our own art,
# README.md (tools/release/README.md) and LICENSE. Nothing from the ROM: the game asks for the player's ROM on its
# first start. --publish needs a clean, pushed main branch, so the tag is the commit the binaries were built from.
# tools/release/notes/<version>.md, if there, goes into the release notes (what's new).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "make_release.sh: $*" >&2; exit 1; }

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?$ ]] || die "usage: make_release.sh <version, e.g. 0.1.0> [--publish]"
publish=0
[[ ${2:-} == --publish ]] && publish=1
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
chmod +x "$out/$name/install.sh" "$out/$name/game/aidyn-rechronicled.sh" "$out/$name/game/steam_shortcut.py"
# Nothing ROM-derived may ship: the only pictures are the icon, the branding pack and the redrawn art.
stray=$(find "$out/$name" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.mpk' \) | head -1)
[[ -z $stray ]] || die "a ROM or save got into the release: $stray"
tar -C "$out" -czf "$out/$name.tar.gz" "$name"
(cd "$out" && sha256sum "$name.tar.gz" >"$name.tar.gz.sha256")
echo "make_release.sh: $out/$name.tar.gz ($(du -h "$out/$name.tar.gz" | cut -f1))"

(( publish )) || exit 0
echo "== GitHub release $tag"
git tag -a "$tag" -m "Aidyn ReChronicled $version"
git push -q origin "$tag"
notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
{
    echo "Aidyn ReChronicled $version for Linux (x86_64)."
    echo
    echo "**Play:** download \`$name.tar.gz\`, unpack it, run \`bash install.sh\` (or \`game/aidyn-rechronicled.sh\` in"
    echo "place). The first start asks for your own NTSC-U 1.0 ROM (\`.z64\`) and keeps it. The README in the download"
    echo "has the rest: controls, Aidyn Patch, settings."
    echo
    echo "Needs glibc 2.38+ (any current distribution), a Vulkan driver and libdbus. Contains no game data. Includes the"
    echo "newly painted art (portraits, item and slot icons, button prompts, HUD pieces, the first world textures); the"
    echo "HD menu and world texture packs (upscales of the game's own pictures) aren't part of it."
    echo
    if [[ -f tools/release/notes/$version.md ]]; then
        cat "tools/release/notes/$version.md"
        echo
    fi
    echo "Built from $(git rev-parse --short HEAD). SHA-256: \`$(cut -d' ' -f1 "$out/$name.tar.gz.sha256")\`"
} >"$notes"
gh release create "$tag" "$out/$name.tar.gz" "$out/$name.tar.gz.sha256" --verify-tag \
    --title "Aidyn ReChronicled $version" --notes-file "$notes"
echo "make_release.sh: published $tag"
