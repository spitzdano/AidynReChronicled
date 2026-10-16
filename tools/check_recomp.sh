#!/usr/bin/env bash
# tools/check_recomp.sh: compile-only check of the generated code (Gates 2 and 3).
#   RecompiledFuncs/*.c  with clang   against the toolkit's N64Recomp include/recomp.h
#   rsp/*.cpp            with clang++ against lib/N64ModernRuntime's librecomp headers
# Prints every file that fails, and exits non-zero if any did.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
. tools/toolkit.env

log=build/check_recomp.log
mkdir -p build && : >"$log"
fail=0

check() {  # check <compiler> <flags...> -- <files...>
    local cc=$1; shift
    local flags=()
    while [[ $1 != -- ]]; do flags+=("$1"); shift; done; shift
    (( $# )) || return 0
    printf '%s\0' "$@" | xargs -0 -P "$(nproc)" -n 1 bash -c '
        out=$("$0" "$@" 2>&1) || { printf "FAIL %s\n%s\n" "${@: -1}" "$out"; exit 1; }
    ' "$cc" "${flags[@]}" -c -o /dev/null >>"$log" 2>&1 || fail=1
}

# Functions N64Recomp doesn't recompile are called but never declared: the runtime provides the
# libultra ones (renamed <name>_recomp), the project's src/ the ones listed in the toml's `ignored`.
# Declare them here so the compile check can stand alone; the list is also useful as the to-do list
# of what the runtime and src/ must supply (build/runtime_provided.txt).
{ grep -rhoE '\b[A-Za-z0-9_]+\(rdram, ctx\)' RecompiledFuncs 2>/dev/null || true; } | sed 's/(rdram, ctx)//' | sort -u >build/called_recomp.txt
{ grep -hoE 'void [A-Za-z0-9_]+\(' RecompiledFuncs/funcs.h 2>/dev/null || true; } | sed 's/void //; s/(//' | sort -u >build/declared_recomp.txt
comm -23 build/called_recomp.txt build/declared_recomp.txt >build/runtime_provided.txt
{ echo '#include "recomp.h"'; sed 's/.*/void &(uint8_t* rdram, recomp_context* ctx);/' build/runtime_provided.txt; } >build/runtime_provided.h

mapfile -t cfiles < <(find RecompiledFuncs -name '*.c' 2>/dev/null | sort)
check clang -O0 -w -include build/runtime_provided.h -I RecompiledFuncs -I "$TOOLKIT/src/N64Recomp/include" -- "${cfiles[@]}"

mapfile -t rspfiles < <(find rsp -name '*.cpp' 2>/dev/null | sort)
rt=lib/N64ModernRuntime
check clang++ -std=c++20 -msse4.1 -O0 -w -I "$rt/librecomp/include" -I "$rt/ultramodern/include" -I "$TOOLKIT/src/N64Recomp/include" \
    -I "$rt/thirdparty" -- "${rspfiles[@]}"

echo "check_recomp.sh: ${#cfiles[@]} C file(s), ${#rspfiles[@]} RSP file(s) checked."
if (( fail )); then
    grep '^FAIL' "$log" | sed 's/^/  /'
    echo "Some files failed to compile; full output in $log" >&2
    exit 1
fi
echo "check_recomp.sh: all compile ($(wc -l <build/runtime_provided.txt) call(s) rely on the runtime: build/runtime_provided.txt)."
