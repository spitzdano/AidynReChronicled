#!/usr/bin/env bash
# package_macos.sh: the built program as "Aidyn ReChronicled.app" and a .dmg (macOS, Apple Silicon). Runs on the CI Mac.
#
#   tools/macos/package_macos.sh <version> <sdl2 lib dir> <packs dir> <rom>
#
# version  the release's, e.g. 1.0.4-beta.1 (the .dmg is AidynReChronicled-<version>-macos-arm64.dmg)
# packs    texture-packs/ and sample-packs/ (tools/macos/fetch_packs.py)
# rom      the ROM the program was built from: only for the guard (nothing from it may be in the package)
#
# Ad-hoc signed, not notarized (no Apple Developer account): Gatekeeper asks the player to allow it once.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "package_macos.sh: $*" >&2; exit 1; }
[[ $# -eq 4 ]] || die "usage: $0 <version> <sdl2 lib dir> <packs dir> <rom>"
version=$1 sdl_lib=$2 packs=$3 rom=$4
exe=build/AidynReChronicled
[[ -x $exe ]] || die "$exe isn't built"
[[ -d $packs/texture-packs ]] || die "no texture-packs in $packs"

name="Aidyn ReChronicled"
out=dist/macos
app="$out/$name.app"
rm -rf "$out" && mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"

echo "== program"
cp "$exe" "$app/Contents/MacOS/AidynReChronicled"
strip -x "$app/Contents/MacOS/AidynReChronicled"

echo "== icon"
iconset=$out/AppIcon.iconset
mkdir -p "$iconset"
for s in 16 32 128 256 512; do
    sips -z $s $s tools/macos/icon.png --out "$iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) tools/macos/icon.png --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/AppIcon.icns"
rm -rf "$iconset"

echo "== packs"
cp -R "$packs/texture-packs" "$app/Contents/Resources/"
[[ -d $packs/sample-packs ]] && cp -R "$packs/sample-packs" "$app/Contents/Resources/"

echo "== libraries"
dylibbundler -od -b -x "$app/Contents/MacOS/AidynReChronicled" -d "$app/Contents/Frameworks" \
    -p @executable_path/../Frameworks/ -s "$sdl_lib"
# No build-machine folder left in the program's search paths.
while IFS= read -r rp; do
    case $rp in @*) ;; *) install_name_tool -delete_rpath "$rp" "$app/Contents/MacOS/AidynReChronicled" ;; esac
done < <(otool -l "$app/Contents/MacOS/AidynReChronicled" | awk '/cmd LC_RPATH/{getline; getline; print $2}')

echo "== Info.plist"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$name</string>
    <key>CFBundleDisplayName</key><string>$name</string>
    <key>CFBundleIdentifier</key><string>io.github.spitzdano.AidynReChronicled</string>
    <key>CFBundleExecutable</key><string>AidynReChronicled</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${version%%-*}</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>LSMinimumSystemVersion</key><string>15.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.role-playing-games</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
PLIST
plutil -lint "$app/Contents/Info.plist"

echo "== checks"
bad=0
while IFS= read -r f; do
    if otool -L "$f" 2>/dev/null | tail -n +2 | grep -E "/opt/homebrew|/usr/local|/Users/" ; then
        echo "package_macos.sh: $f still points at a build-machine library" >&2
        bad=1
    fi
done < <(find "$app/Contents/MacOS" "$app/Contents/Frameworks" -type f)
(( bad == 0 )) || exit 1
python3 -I tools/macos/rom_guard.py "$rom" "$app"

echo "== sign (ad hoc)"
codesign --force --sign - "$app"/Contents/Frameworks/*.dylib
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

echo "== dmg"
stage=$out/stage
mkdir -p "$stage"
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"
cp LICENSE "$stage/LICENSE.txt"
cat > "$stage/README.txt" <<'TXT'
Aidyn ReChronicled for macOS (Apple Silicon, macOS 15 or later). BETA: untested by its maker; please report what you find.

1. Drag "Aidyn ReChronicled" to the Applications folder.
2. Open it. macOS says it can't verify the app. Click Done.
3. Open System Settings > Privacy & Security, scroll down and click "Open Anyway" next to the Aidyn message. Confirm with your password.
4. On the first start, choose your own North American 1.0 Aidyn Chronicles ROM (.z64). The app contains no ROM.
   To add more later (Aidyn Patch, Aidyn Plus, Action Edition), put them in the ROMs folder, which the game makes at every start:
   ~/Library/Application Support/AidynReChronicled/ROMs  (in Finder: Go > Go to Folder..., paste that). Any file names will do;
   start the game again and it finds them.

Power users can skip steps 2 and 3 with:  xattr -dr com.apple.quarantine "/Applications/Aidyn ReChronicled.app"

Saves, settings and your ROMs are kept in ~/Library/Application Support/AidynReChronicled.
Updates: the game tells you when a newer version is out; download its .dmg from the release page.
TXT
dmg=$out/AidynReChronicled-$version-macos-arm64.dmg
hdiutil create -volname "$name" -srcfolder "$stage" -ov -format UDZO "$dmg"
rm -rf "$stage"
(cd "$out" && shasum -a 256 "$(basename "$dmg")" > "$(basename "$dmg").sha256")
echo "package_macos.sh: $dmg ($(du -h "$dmg" | cut -f1))"
