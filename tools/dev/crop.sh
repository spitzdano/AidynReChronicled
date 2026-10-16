#!/usr/bin/env bash
# crop.sh NAME X Y W H: screenshot of part of the game window (window coordinates) -> build/dev/NAME.png
P=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
read -r wx wy < <(hyprctl clients -j | python3 -c 'import json,sys
for c in json.load(sys.stdin):
    if c["class"]=="AidynReChronicled": print(c["at"][0], c["at"][1])')
grim -g "$((wx + $2)),$((wy + $3)) ${4}x${5}" "$P/build/dev/$1.png"
