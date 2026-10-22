#!/usr/bin/env bash
# shots.sh: screenshots of the port for the manual -> build/manual/shots/<name>.png (1920x1080).
#
# Runs the game through tools/dev/pad.sh with a scratch copy of the test save (build/dev/test.mpk: Alaron in the first
# forest, see tools/dev/newsave.sh), floats its window at 1920x1080 on the Dell and walks the screens the manual shows.
# Settings: a scratch copy of the player's settings.json (their texture packs and picture) with the FPS counter off and
# the controls card on, in build/manual/settings. Only the window of the game it started is moved or closed (by its
# pid), never the player's own. The pictures come from the ROM: they stay in build/ (gitignored) and the player's data
# folder. The combat shot isn't made here (a fight can't be walked into reliably): put one in by hand.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
P=tools/dev/pad.sh
OUT=build/manual/shots
[[ -f build/dev/test.mpk ]] || { echo "shots.sh: no build/dev/test.mpk (make one with tools/dev/newsave.sh)" >&2; exit 1; }
mkdir -p "$OUT"
cp build/dev/test.mpk build/manual/shots.mpk
DATA=${XDG_DATA_HOME:-$HOME/.local/share}/AidynReChronicled
SET=$PWD/build/manual/settings
rm -rf "$SET" && mkdir -p "$SET/texture-packs"
for pack in "$DATA"/texture-packs/*/; do ln -s "$pack" "$SET/texture-packs/$(basename "$pack")"; done
python3 - "$DATA/settings.json" "$SET/settings.json" <<'PY'
import json, sys
j = json.load(open(sys.argv[1]))
j["graphics"].update({"fps_counter": False, "controls_card": True, "fullscreen": False})
json.dump(j, open(sys.argv[2], "w"), indent=4)
PY

PID=
win() { hyprctl eval "hl.dispatch(hl.dsp.window.$1)" >/dev/null; }
close() { [[ -n $PID ]] && win "close({ window = \"pid:$PID\" })"; }
shot() {  # shot <name>: the whole window
    $P shot "manual-$1"
    mv "$($P where | head -1)/manual-$1.png" "$OUT/$1.png"
    echo "  $OUT/$1.png"
}
alive() { [[ $($P alive) == ALIVE ]] || { echo "shots.sh: the game closed (see build/play.log)" >&2; exit 1; }; }

AIDYN_MPK=$PWD/build/manual/shots.mpk AIDYN_SETTINGS_DIR=$SET $P start
trap close EXIT
sleep 4
for p in $(pgrep -x AidynReChronicl); do
    tr '\0' '\n' <"/proc/$p/environ" 2>/dev/null | grep -qx "AIDYN_SETTINGS_DIR=$SET" && PID=$p
done
[[ -n $PID ]] || { echo "shots.sh: the test game didn't start" >&2; exit 1; }
win "float({ action = \"enable\", window = \"pid:$PID\" })"
win "resize({ x = 1920, y = 1080, relative = false, window = \"pid:$PID\" })"
win "center({ window = \"pid:$PID\" })"

sleep 8; alive
shot title
$P tap START; sleep 3
shot main-menu
$P tap DD; sleep 0.5; $P tap A; sleep 3
shot load-game
$P tap A; sleep 14; alive
shot field
$P tap START; sleep 3
shot pause
$P tap R; sleep 3
shot inventory
$P tap R; sleep 3
shot calendar
$P tap L; sleep 2; $P tap L; sleep 2
$P tap DD DD; sleep 0.5; $P tap A; sleep 3      # the list: Load Game, Save Game, Options, Theater, Display, ReChronicled
shot options
$P tap B; sleep 2
$P tap DD DD; sleep 0.5; $P tap A; sleep 2
shot display
$P tap B; sleep 2
$P tap DD; sleep 0.5; $P tap A; sleep 2
shot rechronicled
alive
