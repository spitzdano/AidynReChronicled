#!/usr/bin/env bash
# aidyn-rechronicled.sh: starts Aidyn ReChronicled from the handheld package (the Steam shortcut runs this).
# Output goes to ~/.local/share/AidynReChronicled/logs/last-run.log (the run before: previous-run.log), the place to
# look when something goes wrong. Arguments pass through (--patch / --original).
here=$(dirname "$(readlink -f "$0")")
logs=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/logs
mkdir -p "$logs"
[[ -f $logs/last-run.log ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
cd "$here" || exit 1
{ echo "== $(date '+%F %T') $here/AidynReChronicled $*"; } >"$logs/last-run.log"
exec "$here/AidynReChronicled" "$@" >>"$logs/last-run.log" 2>&1
