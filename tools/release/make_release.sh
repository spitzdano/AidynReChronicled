#!/usr/bin/env bash
# make_release.sh: build a release and publish it on GitHub (spitzdano/AidynReChronicled).
#
#   tools/release/make_release.sh <version>             build build/release/AidynReChronicled-<version>-linux-x86_64.tar.gz
#   tools/release/make_release.sh <version> --windows   ... and AidynReChronicled-<version>-windows-x86_64.zip
#   tools/release/make_release.sh <version> [--windows] --publish   ... then tag v<version>, push the tag, create the
#                                                       GitHub release with them
#   tools/release/make_release.sh <version> --patch [--windows] [--publish]
#                                                       a test build for people who have the last release: only the new
#                                                       executable and the title branding pack, as AidynReChronicled-
#                                                       <version>-patch-linux-x86_64.tar.gz (and -patch-windows-x86_64.zip),
#                                                       about 15 MB. --publish makes a GitHub pre-release of it.
#   --no-sound   leave out the restored instrument samples (bundled by default: below)
#   --no-hd      leave out the HD upscale packs (bundled by default: below)
#
# The download holds the portable build (tools/ally/build_portable.sh: glibc 2.38+, its own SDL2), the launcher, the
# installer, the Steam shortcut script and its library art, the icon, the title branding pack, the redrawn portraits and icons
# (rechronicled-art: tools/hdui/art_pack.py from art/) and the HD letters (rechronicled-text: tools/hdui/text_pack.py),
# all made here from fonts and our own art,
# README.md (tools/release/README.md) and LICENSE, and the restored instrument samples (sample-packs/
# rechronicled-samples: AudioSR redrawings of the game's own music samples, kept in the private
# AidynReChronicled-Sound repository, the sound/ submodule; spitzdano's call to ship them), and the HD upscale packs
# (hd-ui and hd-world: Real-ESRGAN upscales of the game's own pictures, kept in the private AidynReChronicled-hu-UI-world
# repository, the hd/ submodule; spitzdano's call too), only the pictures the redrawn art doesn't replace (rtz.py
# --without: about 45 MB of their 1.3 GB). Nothing else from the ROM: the game asks for the player's ROM on its first
# start. --publish needs a clean, pushed main branch, so the tag is the commit the binaries were built from.
# The Windows zip (tools/windows/build_windows.sh, cross-compiled here) holds the exe and its DLLs, the same packs and
# Steam art, README.md and LICENSE, in one folder to unpack and play.
# tools/release/notes/<version>.md goes into the GitHub release's notes (what's new), and every download carries
# RELEASE-NOTES.md: all the notes files, newest first. --publish needs this version's notes file.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "make_release.sh: $*" >&2; exit 1; }

version=${1:-}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?$ ]] || die "usage: make_release.sh <version, e.g. 0.1.0> [--publish]"
publish=0 windows=0 sound=1 hd=1 patch=0
for a in "${@:2}"; do
    case $a in
        --publish) publish=1 ;;
        --windows) windows=1 ;;
        --patch) patch=1 ;;
        --no-sound) sound=0 ;;
        --no-hd) hd=0 ;;
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
    [[ -f tools/release/notes/$version.md ]] || die "no release notes: write tools/release/notes/$version.md first"
fi

echo "== portable build"
AIDYN_VERSION=$version tools/ally/build_portable.sh  # the title screen shows it
# The binary before strip, kept here and never shipped, to read testers' problem reports: the offsets in a report's
# game-crash.txt (AidynReChronicled(+0x2be600)) are its functions: addr2line -f -C -e $out/symbols/AidynReChronicled-<version> 0x2be600
mkdir -p "$out/symbols"
cp build/portable/AidynReChronicled "$out/symbols/AidynReChronicled-$version"
if (( patch )); then
    # A test build for people who have the last release: the new executables, and the title branding pack (the pictures
    # the executable draws are in it too, at full resolution). Everything else of the release is unchanged.
    echo "== title branding pack"
    python3 tools/branding/make_branding.py
    pname=AidynReChronicled-$version-patch-linux-x86_64
    rm -rf "$out/$pname" "$out/$pname.tar.gz" "$out/$pname.tar.gz.sha256"
    mkdir -p "$out/$pname/texture-packs"
    cp build/portable/dist/AidynReChronicled "$out/$pname/"
    python3 -I tools/release/rtz.py "$packs/rechronicled" "$out/$pname/texture-packs/rechronicled.rtz"
    cat >"$out/$pname/apply.sh" <<'SH'
#!/usr/bin/env bash
# apply.sh: puts this test build over an installed Aidyn ReChronicled (~/Games/AidynReChronicled by default).
#   bash apply.sh            keeps the old files as AidynReChronicled.previous and rechronicled.rtz.previous
#   bash apply.sh --undo     puts them back
set -euo pipefail
here=$(dirname "$(readlink -f "$0")")
game=${AIDYN_GAME:-$HOME/Games/AidynReChronicled}
[[ -x $game/AidynReChronicled && -d $game/texture-packs ]] || { echo "apply.sh: no installed game in $game (install the last release first, or set AIDYN_GAME=<its folder>)" >&2; exit 1; }
if [[ ${1:-} == --undo ]]; then
    [[ -f $game/AidynReChronicled.previous ]] || { echo "apply.sh: nothing to undo" >&2; exit 1; }
    mv -f "$game/AidynReChronicled.previous" "$game/AidynReChronicled"
    [[ -f $game/texture-packs/rechronicled.rtz.previous ]] && mv -f "$game/texture-packs/rechronicled.rtz.previous" "$game/texture-packs/rechronicled.rtz"
    echo "Back to the release you had."
    exit 0
fi
cp -f "$game/AidynReChronicled" "$game/AidynReChronicled.previous"
[[ -f $game/texture-packs/rechronicled.rtz ]] && cp -f "$game/texture-packs/rechronicled.rtz" "$game/texture-packs/rechronicled.rtz.previous"
cp -f "$here/AidynReChronicled" "$game/AidynReChronicled"
cp -f "$here/texture-packs/rechronicled.rtz" "$game/texture-packs/rechronicled.rtz"
chmod +x "$game/AidynReChronicled"
echo "Done: the title screen now shows v$VERSION_TEXT. Your saves and settings are untouched."
SH
    sed -i "s/\$VERSION_TEXT/$version/" "$out/$pname/apply.sh"
    chmod +x "$out/$pname/apply.sh"
    cat >"$out/$pname/README.txt" <<TXT
Aidyn ReChronicled $version: a test build (a patch over the last release)

Needs the last release installed first: https://github.com/spitzdano/AidynReChronicled/releases/latest
This replaces only the program and one small texture pack. Your saves, settings and ROMs aren't touched.

Linux:   bash apply.sh            (to go back: bash apply.sh --undo)
         If the game isn't in ~/Games/AidynReChronicled: AIDYN_GAME=<its folder> bash apply.sh

The title screen shows v$version when it worked. What to look at is in the release's notes; problems:
https://github.com/spitzdano/AidynReChronicled/issues/new?template=problem-report.yml
TXT
    tar -C "$out" -czf "$out/$pname.tar.gz" "$pname"
    (cd "$out" && sha256sum "$pname.tar.gz" >"$pname.tar.gz.sha256")
    echo "make_release.sh: $out/$pname.tar.gz ($(du -h "$out/$pname.tar.gz" | cut -f1))"
    assets=("$out/$pname.tar.gz" "$out/$pname.tar.gz.sha256")
    if (( windows )); then
        wpname=AidynReChronicled-$version-patch-windows-x86_64
        echo "== Windows build"
        AIDYN_VERSION=$version tools/windows/build_windows.sh
        mkdir -p "$out/symbols/windows-$version"
        cp build/windows/AidynReChronicled.exe build/windows/AidynReChronicled.pdb "$out/symbols/windows-$version/"
        rm -rf "$out/$wpname" "$out/$wpname.zip" "$out/$wpname.zip.sha256"
        mkdir -p "$out/$wpname/texture-packs"
        cp build/windows/AidynReChronicled.exe "$out/$wpname/"
        cp "$out/$pname/texture-packs/rechronicled.rtz" "$out/$wpname/texture-packs/"
        sed 's/$/\r/' >"$out/$wpname/README.txt" <<TXT
Aidyn ReChronicled $version: a test build (a patch over the last release)

Needs the last release installed first: https://github.com/spitzdano/AidynReChronicled/releases/latest
This replaces only the program and one small texture pack. Your saves, settings and ROMs (in %APPDATA%) aren't touched.

1. Close the game. Open the folder you unpacked the release into (the one with AidynReChronicled.exe).
2. Rename AidynReChronicled.exe there to AidynReChronicled-old.exe (to keep it), and
   texture-packs\rechronicled.rtz to rechronicled-old.rtz.
3. Copy AidynReChronicled.exe and the texture-packs folder from this download into it.
4. Run AidynReChronicled.exe. The title screen shows v$version when it worked.
   To go back, delete the new files and rename the old ones back.

Problems: https://github.com/spitzdano/AidynReChronicled/issues/new?template=problem-report.yml
TXT
        (cd "$out" && python3 -c 'import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(sys.argv[2]):
        dirs.sort()
        for f in sorted(files):
            z.write(os.path.join(root, f))' "$wpname.zip" "$wpname" && sha256sum "$wpname.zip" >"$wpname.zip.sha256")
        echo "make_release.sh: $out/$wpname.zip ($(du -h "$out/$wpname.zip" | cut -f1))"
        assets+=("$out/$wpname.zip" "$out/$wpname.zip.sha256")
    fi
    (( publish )) || exit 0
    echo "== GitHub pre-release $tag"
    git tag -a "$tag" -m "Aidyn ReChronicled $version (test build)"
    git push -q origin "$tag"
    notes=$(mktemp)
    trap 'rm -f "$notes"' EXIT
    {
        echo "**A test build of Aidyn ReChronicled $version, as a patch over the last release.** Not the next release: that comes"
        echo "once this has been tried. Install the last release first, then put this over it (its README.txt says how);"
        echo "your saves and settings aren't touched."
        echo
        [[ -f tools/release/notes/$version.md ]] && { cat "tools/release/notes/$version.md"; echo; }
        echo "Built from $(git rev-parse --short HEAD). SHA-256: \`$(cut -d' ' -f1 "$out/$pname.tar.gz.sha256")\` (Linux)"
        (( windows )) && echo "\`$(cut -d' ' -f1 "$out/$wpname.zip.sha256")\` (Windows)"
    } >"$notes"
    gh release create "$tag" "${assets[@]}" --verify-tag --prerelease \
        --title "Aidyn ReChronicled $version (test build)" --notes-file "$notes"
    echo "make_release.sh: published $tag as a pre-release"
    exit 0
fi

# Every pack is made afresh from the art/ submodule (and the fonts), so a release carries what art/ holds now.
echo "== title branding pack"
python3 tools/branding/make_branding.py
echo "== redrawn art pack"
python3 tools/hdui/art_pack.py
echo "== HD letters pack"
python3 tools/hdui/text_pack.py

# Every release's notes in one file, newest first, for the downloads (each notes file's own headings one level down).
release_notes=$out/RELEASE-NOTES.md
mkdir -p "$out"
python3 -I - tools/release/notes "$release_notes" <<'PY'
import re, sys
from pathlib import Path
notes = sorted(Path(sys.argv[1]).glob("*.md"),
               key=lambda p: [int(x) for x in re.findall(r"\d+", p.stem)], reverse=True)
out = ["# Aidyn ReChronicled: release notes", "",
       "What each release changed, newest first. The full list of releases: "
       "https://github.com/spitzdano/AidynReChronicled/releases", ""]
for p in notes:
    out += [f"## {p.stem}", ""]
    out += [re.sub(r"^(#+) ", r"\1# ", line) for line in p.read_text().rstrip().splitlines()]
    out.append("")
Path(sys.argv[2]).write_text("\n".join(out) + "\n")
PY

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
# Each pack as one .rtz (tools/release/rtz.py: RT64's zipped pack): a fraction of the start-up time 40,000 files take.
for p in rechronicled rechronicled-art rechronicled-text; do
    python3 -I tools/release/rtz.py "$packs/$p" "$out/$name/game/texture-packs/$p.rtz"
done
if (( hd )); then  # the HD upscale packs, without what the packs after them replace (settings.json's order)
    for p in hd-ui hd-world; do
        [[ -f hd/texture-packs/$p/rt64.json ]] || die "no hd/texture-packs/$p: git submodule update --init hd"
    done
    python3 -I tools/release/rtz.py hd/texture-packs/hd-ui "$out/$name/game/texture-packs/hd-ui.rtz" \
        --without hd/texture-packs/hd-world "$packs/rechronicled-text" "$packs/rechronicled-art" "$packs/rechronicled"
    python3 -I tools/release/rtz.py hd/texture-packs/hd-world "$out/$name/game/texture-packs/hd-world.rtz" \
        --without "$packs/rechronicled-text" "$packs/rechronicled-art" "$packs/rechronicled"
fi
if (( sound )); then  # the restored instrument samples (src/game/patches.cpp looks beside the executable too)
    [[ -d sound/sample-packs/rechronicled-samples ]] || die "no sound/sample-packs/rechronicled-samples: git submodule update --init sound"
    mkdir -p "$out/$name/game/sample-packs"
    cp -a sound/sample-packs/rechronicled-samples "$out/$name/game/sample-packs/"
fi
cp tools/ally/install.sh tools/release/README.md LICENSE "$release_notes" "$out/$name/"
mkdir -p "$out/$name/ROMs"  # the player's ROMs go here (src/main/main.cpp, store_roms_from_folders)
cp tools/release/ROMs-README.txt "$out/$name/ROMs/README.txt"
chmod +x "$out/$name/install.sh" "$out/$name/game/aidyn-rechronicled.sh" "$out/$name/game/steam_shortcut.py"
# Nothing else ROM-derived may ship: the only pictures are the icon, the branding pack and the redrawn art (the
# restored samples and the HD packs, above, are the exceptions).
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
    cp "$out/$name/game/texture-packs/"*.rtz "$out/$wname/texture-packs/"
    if (( sound )); then
        mkdir -p "$out/$wname/sample-packs"
        cp -a sound/sample-packs/rechronicled-samples "$out/$wname/sample-packs/"
    fi
    cp tools/release/README.md LICENSE "$release_notes" "$out/$wname/"
    # Add to Steam.bat runs add-to-steam.ps1 (the Linux install.sh's Steam step, for Windows), both with CRLF.
    sed 's/$/\r/' tools/windows/add-to-steam.ps1 >"$out/$wname/add-to-steam.ps1"
    cp "tools/windows/Add to Steam.bat" "$out/$wname/"  # kept with CRLF in the repository
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
        echo "Windows 10 or 11 (64-bit) and a Vulkan driver; nothing to install (the Visual C++ runtime is included)."
        echo
    fi
    echo "Put your own NTSC-U 1.0 ROM (\`.z64\`) in the download's \`ROMs\` folder, and the Aidyn Patch / Aidyn Plus ones if"
    echo "you have them, under any names: the game finds them by their contents (without one, the first start asks for it)."
    echo "The README in the download has the rest: controls, Aidyn Patch, settings, adding it to Steam."
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
