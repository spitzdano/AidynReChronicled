#!/usr/bin/env bash
# collect.sh [--from-save PAK] [menu_secs] [field_secs]: scripted tour that feeds tools/hdui/build_pack.py.
# Runs the game with AIDYN_DUMP_TEXTURES=build/hdui/dumps and AIDYN_UI_LOG=build/hdui/ui on a scratch Controller Pak
# (build/dev/collect.mpk, never your own save): boot logos, title, menu, name entry, the intro with its dialogue,
# a save, then random input in the pause menus (default 240 s) and in the field (default 120 s).
# --from-save PAK skips the new game: it loads the first save on a copy of PAK (tools/dev/newsave.sh makes one).
# Uses tools/dev/pad.sh, so the window needs to be visible (workspace 1). Adds to earlier dumps; run
# build_pack.py afterwards.
set -euo pipefail
P=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
pad=$P/tools/dev/pad.sh
export AIDYN_DUMP_TEXTURES=$P/build/hdui/dumps AIDYN_UI_LOG=$P/build/hdui/ui AIDYN_MPK=$P/build/dev/collect.mpk
mkdir -p "$P/build/dev"; rm -f "$AIDYN_MPK"
from_save=
if [[ ${1:-} == --from-save ]]; then from_save=$2; shift 2; cp "$from_save" "$AIDYN_MPK"; fi
fuzz() {  # fuzz <secs> <buttons...>: random taps (and stick pushes when NONE is listed)
    local end=$((SECONDS + $1)); shift; local b=("$@")
    while (( SECONDS < end )); do
        local k=${b[RANDOM % ${#b[@]}]}
        if [[ $k == NONE ]]; then $pad hold 0.$((RANDOM % 8 + 2)) NONE $((RANDOM % 161 - 80)) $((RANDOM % 161 - 80))
        else TAPGAP=0.35 $pad tap "$k"; fi
        [[ $($pad alive) == ALIVE ]] || { echo "collect.sh: the game died (see build/play.log)" >&2; exit 1; }
    done
}
if [[ -n $from_save ]]; then
    "$P/tools/dev/loadgame.sh" >/dev/null
else
$pad start; sleep 6                                     # straight to the title (settings.json skip_intro)
$pad tap START; sleep 3; $pad tap A; sleep 4          # menu -> New Game -> name entry
TAPGAP=0.4 $pad tap DR DD DL DU START; sleep 1; $pad tap A; sleep 6
for _ in $(seq 1 120); do TAPGAP=2.5 $pad tap A; done  # intro cutscenes and dialogue
$pad tap START; sleep 3; $pad tap DD; sleep 0.5; $pad tap A; sleep 3; $pad tap A; sleep 3; $pad tap A; sleep 2; $pad tap A; sleep 2
$pad shot collect_saved
fi
# Every pause-menu entry once (Load, Save, Options, Theater, Credits, Help), then random input there.
for n in 0 1 2 3 4 5; do
    $pad tap START; sleep 3
    for _ in $(seq 1 "$n"); do TAPGAP=0.4 $pad tap DD; done
    $pad tap A; sleep 5; $pad shot "collect_menu$n"; TAPGAP=1 $pad tap B B; sleep 2
    [[ $($pad alive) == ALIVE ]] || { echo "collect.sh: the game died (see build/play.log)" >&2; exit 1; }
done
# Each pause page (Menu, equipment, journal) opened twice: reopening goes straight to the last page, which the game
# draws differently (in 32x64 blocks, from wherever the picture now lies in memory).
for page in 0 1 2; do
    $pad tap START; sleep 3
    for _ in $(seq 1 "$page"); do $pad tap R; sleep 2.5; done
    sleep 3; TAPGAP=1.5 $pad tap B; sleep 2
    $pad tap START; sleep 6; $pad shot "collect_reopen$page"
    for _ in $(seq 1 "$page"); do $pad tap L; sleep 2.5; done
    TAPGAP=1.5 $pad tap B; sleep 2
    [[ $($pad alive) == ALIVE ]] || { echo "collect.sh: the game died (see build/play.log)" >&2; exit 1; }
done
$pad tap START; sleep 3
fuzz "${1:-240}" A A B DU DD DL DR L R CU CD CL CR     # pause menu pages, options, journal, save/load screens
$pad tap B B B; sleep 2
fuzz "${2:-120}" NONE NONE A B START Z CU CD CL CR     # field: HUD, dialogue, inventory, maybe a fight
$pad shot collect_end
hyprctl dispatch 'hl.dsp.window.close({ window = "class:AidynReChronicled" })' >/dev/null  # like Shift+Esc
echo "collect.sh: done ($(ls "$AIDYN_DUMP_TEXTURES" | grep -c tile.json) textures dumped, $(wc -l <"$AIDYN_UI_LOG/images.jsonl") UI draws logged)"
