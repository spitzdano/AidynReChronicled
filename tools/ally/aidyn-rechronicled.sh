#!/usr/bin/env bash
# aidyn-rechronicled.sh: starts Aidyn ReChronicled from the handheld package (the Steam shortcut runs this).
# Output goes to ~/.local/share/AidynReChronicled/logs/last-run.log (the run before: previous-run.log), the place to
# look when something goes wrong. Arguments pass through (--patch / --original / --plus).
here=$(dirname "$(readlink -f "$0")")
logs=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/logs
mkdir -p "$logs"
[[ -f $logs/last-run.log ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"
cd "$here" || exit 1
{ echo "== $(date '+%F %T') $here/AidynReChronicled $*"; } >"$logs/last-run.log"
GAME=$here/AidynReChronicled
# The game's output goes to the log; if it died of a crash signal (segfault, abort, bus error, illegal instruction,
# arithmetic) without having written a problem report itself (it does for its own crash check), the game is run once
# more with --report to write one (reports/ in the data folder: the logs, settings and saves in one .zip to send).
start=$(date +%s)
"$GAME" "$@" >>"$logs/last-run.log" 2>&1
status=$?
case $status in
    132|134|135|136|139)
        reports=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled/reports
        newest=$(ls -t "$reports"/report-*.zip 2>/dev/null | head -1)
        if [[ -z $newest || $(stat -c %Y "$newest") -lt $start ]]; then
            echo "== the game ended with status $status (signal $((status - 128))): writing a problem report" >>"$logs/last-run.log"
            "$GAME" --report "The game ended without closing properly (signal $((status - 128)))" "$@" >>"$logs/last-run.log" 2>&1
        fi
        ;;
esac
exit $status
