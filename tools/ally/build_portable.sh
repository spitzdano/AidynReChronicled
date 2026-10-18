#!/usr/bin/env bash
# build_portable.sh: a build of the game that runs on other Linux machines (the Bazzite handheld), in build/portable.
#
# Builds in a rootless podman container from tools/ally/Containerfile (Ubuntu 24.04, glibc 2.39), so the executable
# needs nothing newer than glibc 2.39. RT64's file dialogs use the desktop portal (NFD_PORTAL) instead of GTK. The
# two libraries a distribution may lack are copied into build/portable/dist/lib: SDL2 (built in the container with
# every backend loaded at run time, so it needs only libc) and libatomic. They are found through the
# executable's RPATH ($ORIGIN/lib, which beats LD_LIBRARY_PATH, so Steam's runtime libraries can't replace them).
# Needs RecompiledFuncs/ and rsp/ (tools/recomp.sh) and the submodules, like the normal build.
#
#   tools/ally/build_portable.sh          build (incremental) and assemble build/portable/dist
#   tools/ally/build_portable.sh --clean  start the build folder over
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
IMAGE=localhost/aidyn-portable-build
B=build/portable

[[ ${1:-} == --clean ]] && rm -rf "$B"
[[ -f RecompiledFuncs/recomp_overlays.inl && -f rsp/aspMain.cpp ]] || { echo "build_portable.sh: run tools/recomp.sh first" >&2; exit 1; }
if ! podman image exists "$IMAGE"; then
    podman build -t "$IMAGE" -f tools/ally/Containerfile tools/ally
fi

# AIDYN_VERSION (make_release.sh: the release's version) is what the title screen shows; the container has no git.
podman run --rm -e AIDYN_VERSION="${AIDYN_VERSION:-}" -v "$PWD:/src" -w /src "$IMAGE" bash -euo pipefail -c '
B=build/portable
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DNFD_PORTAL=ON -DCMAKE_EXE_LINKER_FLAGS= -DAIDYN_VERSION="$AIDYN_VERSION" >/dev/null
cmake --build $B --target AidynReChronicled
rm -rf $B/dist && mkdir -p $B/dist/lib
cp $B/AidynReChronicled $B/dist/
strip $B/dist/AidynReChronicled
# DT_RPATH (not RUNPATH): searched before LD_LIBRARY_PATH, which Steam fills with its runtime'"'"'s libraries.
patchelf --force-rpath --set-rpath "\$ORIGIN/lib" $B/dist/AidynReChronicled
cp -L /opt/sdl2/lib/libSDL2-2.0.so.0 /usr/lib/x86_64-linux-gnu/libatomic.so.1 $B/dist/lib/
echo "== needs"
objdump -T $B/dist/AidynReChronicled | grep -o "GLIBC_[0-9.]*" | sort -Vu | tail -1
readelf -d $B/dist/AidynReChronicled | grep -E "NEEDED|RPATH|RUNPATH"
'
echo "build_portable.sh: $B/dist (AidynReChronicled + lib/)"
