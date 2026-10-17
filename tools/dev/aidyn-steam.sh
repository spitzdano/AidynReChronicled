#!/usr/bin/env bash
# aidyn-steam.sh: starts the development build (build/cmake/AidynReChronicled) from its Steam shortcut on the PC, set up
# with:
#   python3 tools/ally/steam_shortcut.py --launcher tools/dev/aidyn-steam.sh --art build/ally/steam-art
# Like the handheld's launcher, output goes to ~/.local/share/AidynReChronicled/logs/last-run.log (the run before:
# previous-run.log). Steam starts non-Steam games with its runtime's library path; this build is linked against the
# system's libraries, so the system's path is put back. Arguments pass through (--patch / --original).
root=$(dirname "$(readlink -f "$0")")/../..
cd "$root" || exit 1
[[ -n ${SYSTEM_LD_LIBRARY_PATH+set} ]] && export LD_LIBRARY_PATH=$SYSTEM_LD_LIBRARY_PATH
logs=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/logs
mkdir -p "$logs"
[[ -f $logs/last-run.log ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
echo "== $(date '+%F %T') $PWD/build/cmake/AidynReChronicled $* (Steam)" >"$logs/last-run.log"
exec build/cmake/AidynReChronicled "$@" >>"$logs/last-run.log" 2>&1
