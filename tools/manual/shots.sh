#!/usr/bin/env bash
# shots.sh: screenshots of the port for the manual -> build/manual/shots/<name>.png (1920x1080).
#
# Runs the game through tools/dev/pad.sh with a scratch copy of the test save (build/dev/test.mpk: Alaron in the first
# forest, see tools/dev/newsave.sh), floats its window at 1920x1080 on the Dell and walks the screens the manual shows.
# Uses settings.json as it is, so the shots show the player's own settings (16:9, HD packs). Refuses to run while the
# game is open. The pictures come from the ROM: they stay in build/ (gitignored) and the player's data folder.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
P=tools/dev/pad.sh
OUT=build/manual/shots
[[ -f build/dev/test.mpk ]] || { echo "shots.sh: no build/dev/test.mpk (make one with tools/dev/newsave.sh)" >&2; exit 1; }
mkdir -p "$OUT"
cp build/dev/test.mpk build/manual/shots.mpk

win() { hyprctl eval "hl.dispatch(hl.dsp.window.$1)" >/dev/null; }
close() { win 'close({ window = "class:AidynReChronicled" })'; }
shot() {  # shot <name>: the whole window
    $P shot "manual-$1"
    mv "build/dev/manual-$1.png" "$OUT/$1.png"
    echo "  $OUT/$1.png"
}
alive() { [[ $($P alive) == ALIVE ]] || { echo "shots.sh: the game closed (see build/play.log)" >&2; exit 1; }; }

AIDYN_MPK=$PWD/build/manual/shots.mpk $P start
trap close EXIT
win 'float({ action = "enable", window = "class:AidynReChronicled" })'
win 'resize({ x = 1920, y = 1080, relative = false, window = "class:AidynReChronicled" })'
win 'center({ window = "class:AidynReChronicled" })'

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
$P tap DD DD; sleep 0.5; $P tap A; sleep 3
shot options
alive
