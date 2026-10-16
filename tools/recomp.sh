#!/usr/bin/env bash
# tools/recomp.sh: regenerate the recompiled code with the toolkit's pinned tools.
#
#   tools/recomp.sh        both
#   tools/recomp.sh cpu    N64Recomp only: aidyn.us.toml -> RecompiledFuncs/
#   tools/recomp.sh rsp    RSPRecomp only: each RSP_CONFIGS toml -> rsp/
#
# Output folders are deleted and rebuilt each run, so nothing stale survives.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
. tools/toolkit.env
. tools/build.conf


what=${1:-all}
case $what in cpu|rsp|all) ;; *) echo "usage: $0 [cpu|rsp|all]" >&2; exit 2 ;; esac
tools/check_rom.sh

if [[ $what != rsp ]]; then
    [[ -f build/aidyn.us.elf ]] || { echo "build/aidyn.us.elf missing: run tools/build_elf.sh first" >&2; exit 1; }
    rm -rf RecompiledFuncs && mkdir RecompiledFuncs
    "$TOOLKIT/bin/N64Recomp" aidyn.us.toml
    echo "recomp.sh: $(find RecompiledFuncs -name '*.c' | wc -l) C files in RecompiledFuncs/"
fi

if [[ $what != cpu ]]; then
    rm -rf rsp && mkdir rsp
    for cfg in "${RSP_CONFIGS[@]}"; do
        "$TOOLKIT/bin/RSPRecomp" "$cfg"
    done
    echo "recomp.sh: $(ls rsp | wc -l) file(s) in rsp/"
fi
