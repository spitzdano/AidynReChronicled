#!/usr/bin/env bash
# build_windows.sh: the game for Windows (x86_64), cross-compiled here on Linux, in build/windows; the files to ship
# in build/windows/dist.
#
# clang-cl and lld-link (LLVM) against Microsoft's C runtime and Windows SDK, which xwin (github.com/Jake-Shadle/xwin,
# a pinned, checksummed release, kept in build/windows-tools) downloads from Microsoft into build/xwin. Running this
# accepts Microsoft's licence for those files (xwin --accept-license): they are only used to build, never committed or
# shipped. Rendering is Vulkan (RT64), as on Linux. Our own C and C++ runtimes are linked in (tools/windows/toolchain.cmake),
# so the exe doesn't care which Visual C++ Redistributable is installed (MSVCP140_ATOMIC_WAIT.dll is only in recent
# ones); DirectX Shader Compiler's dxcompiler.dll does need one (any 2015-2022, x64), which the README says.
#
# Needs: clang (clang-cl), lld (lld-link), llvm (llvm-rc, llvm-lib), ninja, cmake, ImageMagick (the icon), and the
# Linux build in build/cmake (RT64's file_to_c is run on this machine while building), RecompiledFuncs/ and rsp/
# (tools/recomp.sh) and the submodules, like the normal build.
#
#   tools/windows/build_windows.sh          build (incremental) and assemble build/windows/dist
#   tools/windows/build_windows.sh --clean  start the build folder over (needed after toolchain.cmake changes)
#
# dist: AidynReChronicled.exe, SDL2.dll and DirectX Shader Compiler's dxcompiler.dll and dxil.dll (RT64 builds shaders
# with it as it runs; from RT64's contrib folder). AidynReChronicled.pdb, beside the exe in build/windows, holds the
# function names for reading problem reports and isn't shipped.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "build_windows.sh: $*" >&2; exit 1; }
B=build/windows
T=build/windows-tools
XWIN_VERSION=0.10.0
XWIN_SHA256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88

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
# The exe itself mustn't need the Visual C++ runtime DLLs (only dxcompiler.dll does).
if llvm-readobj --coff-imports "$B/dist/AidynReChronicled.exe" | grep -qiE 'Name: (msvcp|vcruntime)[0-9]'; then
    die "AidynReChronicled.exe needs the Visual C++ runtime DLLs: check CMAKE_MSVC_RUNTIME_LIBRARY (--clean after changes)"
fi
echo "build_windows.sh: $B/dist ($(du -sh "$B/dist" | cut -f1))"
