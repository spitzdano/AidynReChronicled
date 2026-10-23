#!/usr/bin/env bash
# build_windows.sh: the game for Windows (x86_64), cross-compiled here on Linux, in build/windows; the files to ship
# in build/windows/dist.
#
# clang-cl and lld-link (LLVM) against Microsoft's C runtime and Windows SDK, which xwin (github.com/Jake-Shadle/xwin,
# a pinned, checksummed release, kept in build/windows-tools) downloads from Microsoft into build/xwin. Running this
# accepts Microsoft's licence for those files (xwin --accept-license): they are only used to build, never committed or
# shipped. Rendering is Vulkan (RT64), as on Linux. Our own C and C++ runtimes are linked in (tools/windows/toolchain.cmake);
# DirectX Shader Compiler's dxcompiler.dll and dxil.dll need Microsoft's (msvcp140.dll, vcruntime140.dll,
# vcruntime140_1.dll), so those three ship beside them, app-local (Microsoft's redistributable files, from the Visual
# Studio package below, pinned by its checksum): players need no Visual C++ Redistributable installed.
#
# Needs: clang (clang-cl), lld (lld-link), llvm (llvm-rc, llvm-lib), ninja, cmake, ImageMagick (the icon), and the
# Linux build in build/cmake (RT64's file_to_c is run on this machine while building), RecompiledFuncs/ and rsp/
# (tools/recomp.sh) and the submodules, like the normal build.
#
#   tools/windows/build_windows.sh          build (incremental) and assemble build/windows/dist
#   tools/windows/build_windows.sh --clean  start the build folder over (needed after toolchain.cmake changes)
#
# dist: AidynReChronicled.exe, SDL2.dll, DirectX Shader Compiler's dxcompiler.dll and dxil.dll (RT64 builds shaders
# with it as it runs; from RT64's contrib folder) and the Visual C++ runtime they need. AidynReChronicled.pdb, beside the exe in build/windows, holds the
# function names for reading problem reports and isn't shipped.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "build_windows.sh: $*" >&2; exit 1; }
B=build/windows
T=build/windows-tools
XWIN_VERSION=0.10.0
XWIN_SHA256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
# The Visual C++ 2022 runtime (14.44), as Visual Studio's own package (its manifest's entry
# Microsoft.VC.14.44.17.14.CRT.Redist.X64.base): VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT in it.
VCRT_URL=https://download.visualstudio.microsoft.com/download/pr/45d3b8dd-bced-4b37-9974-142f748d710c/4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5/Microsoft.VC.14.44.17.14.CRT.Redist.X64.base.vsix
VCRT_SHA256=4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5

[[ ${1:-} == --clean ]] && rm -rf "$B"
for t in clang-cl lld-link llvm-rc llvm-lib ninja cmake magick curl; do
    command -v "$t" >/dev/null || die "needs $t"
done
[[ -f RecompiledFuncs/recomp_overlays.inl && -f rsp/aspMain.cpp ]] || die "run tools/recomp.sh first"
[[ -x build/cmake/file_to_c ]] || die "build the Linux version first (build/cmake/file_to_c is used while building)"

if [[ ! -d build/xwin/crt || ! -d build/xwin/sdk ]]; then
    name=xwin-$XWIN_VERSION-x86_64-unknown-linux-musl
    if [[ ! -x $T/$name/xwin ]]; then
        echo "== xwin $XWIN_VERSION"
        mkdir -p "$T"
        curl -fsSL -o "$T/$name.tar.gz" "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/$name.tar.gz"
        echo "$XWIN_SHA256  $T/$name.tar.gz" | sha256sum -c --quiet - || die "xwin download doesn't match its checksum"
        tar -C "$T" -xzf "$T/$name.tar.gz"
    fi
    echo "== Microsoft's C runtime and Windows SDK (xwin, about 1 GB to download)"
    "$T/$name/xwin" --accept-license --arch x86_64 --cache-dir "$T/xwin-cache" splat --output build/xwin
    rm -rf "$T/xwin-cache"
fi

if [[ ! -f $B/CMakeCache.txt ]]; then
    cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=tools/windows/toolchain.cmake \
        -DAIDYN_VERSION="${AIDYN_VERSION:-}" >/dev/null
else
    cmake "$B" -DAIDYN_VERSION="${AIDYN_VERSION:-}" >/dev/null  # make_release.sh passes the release's version
fi
cmake --build "$B" --target AidynReChronicled

rm -rf "$B/dist" && mkdir -p "$B/dist"
cp "$B/AidynReChronicled.exe" "$B/dist/"
cp lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll "$B/dist/"
cp lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll lib/rt64/src/contrib/dxc/bin/x64/dxil.dll "$B/dist/"
vcrt=$T/vc-crt-redist-14.44.vsix
if [[ ! -f $vcrt ]]; then
    echo "== the Visual C++ runtime (Microsoft's redistributable files)"
    mkdir -p "$T"
    curl -fsSL -o "$vcrt.tmp" "$VCRT_URL"
    echo "$VCRT_SHA256  $vcrt.tmp" | sha256sum -c --quiet - || die "the Visual C++ runtime package doesn't match its checksum"
    mv "$vcrt.tmp" "$vcrt"
fi
python3 -I - "$vcrt" "$B/dist" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
want = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
for name in z.namelist():
    parts = name.split("/")
    if "Microsoft.VC143.CRT" in parts and "x64" in parts and "debug_nonredist" not in parts and parts[-1] in want:
        open(f"{sys.argv[2]}/{parts[-1]}", "wb").write(z.read(name))
        want.discard(parts[-1])
if want:
    sys.exit(f"build_windows.sh: not in the Visual C++ runtime package: {sorted(want)}")
PY
# The exe itself mustn't need the Visual C++ runtime DLLs (only dxcompiler.dll and dxil.dll do: they're beside it).
if llvm-readobj --coff-imports "$B/dist/AidynReChronicled.exe" | grep -qiE 'Name: (msvcp|vcruntime)[0-9]'; then
    die "AidynReChronicled.exe needs the Visual C++ runtime DLLs: check CMAKE_MSVC_RUNTIME_LIBRARY (--clean after changes)"
fi
echo "build_windows.sh: $B/dist ($(du -sh "$B/dist" | cut -f1))"
