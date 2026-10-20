#!/usr/bin/env bash
# aidyn-steam.sh: starts the development build (build/cmake/AidynReChronicled) on the PC, from its Steam shortcut (set up
# with `python3 tools/ally/steam_shortcut.py --launcher tools/dev/aidyn-steam.sh --art build/ally/steam-art`) and from
# the app menu's entries (~/.local/share/applications/aidyn-rechronicled*.desktop).
# Like the handheld's launcher, output goes to ~/.local/share/AidynReChronicled/logs/last-run.log (the run before:
# previous-run.log). Steam starts non-Steam games with its runtime's library path; this build is linked against the
# system's libraries, so the system's path is put back. Arguments pass through (--patch / --original / --plus).
# The heap trace is on (src/game/patches.cpp, "Heap trace": no measurable cost, 60 fps either way) while a crash from
# a damaged prop model is hunted: if a model is damaged again, heap-trace.txt in the data folder says what did it.
# AIDYN_HEAP_TRACE=0 in the environment turns it off.
root=$(dirname "$(readlink -f "$0")")/../..
cd "$root" || exit 1
[[ -n ${SYSTEM_LD_LIBRARY_PATH+set} ]] && export LD_LIBRARY_PATH=$SYSTEM_LD_LIBRARY_PATH
export AIDYN_HEAP_TRACE=${AIDYN_HEAP_TRACE-1}
logs=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/logs
mkdir -p "$logs"
[[ -f $logs/last-run.log ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
echo "== $(date '+%F %T') $PWD/build/cmake/AidynReChronicled $*${SteamGameId:+ (Steam)}, heap trace $AIDYN_HEAP_TRACE" >"$logs/last-run.log"
exec build/cmake/AidynReChronicled "$@" >>"$logs/last-run.log" 2>&1
