#!/usr/bin/env bash
# pad.sh: drive the game for testing through AIDYN_PAD_FILE (src/main/input.cpp). State lives in build/dev.
#   pad.sh start            launch (env passes through: AIDYN_MPK, AIDYN_SETTINGS_DIR, AIDYN_DUMP_TEXTURES, AIDYN_UI_LOG, AIDYN_FPS)
#   pad.sh tap BTN...       A B Z START DU DD DL DR L R CU CD CL CR R3 L3 F2 (the key: redrawn art off / on) (TAPGAP=seconds between taps, default 0.5)
#   pad.sh hold SECS BTN [X Y [RX RY]]   hold a button (NONE for sticks only), left stick X Y -80..80,
#                                        right stick RX RY -1..1
#   pad.sh shot NAME        screenshot of the game window -> build/dev/NAME.png
#   pad.sh alive            ALIVE / DEAD
# The window is moved to workspace 1 (the Dell) on start.
P=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
D=$P/build/dev; mkdir -p "$D"; F=$D/pad.txt
bit() { case $1 in A) echo 8000;; B) echo 4000;; Z) echo 2000;; START) echo 1000;; DU) echo 800;; DD) echo 400;; DL) echo 200;; DR) echo 100;; L) echo 20;; R) echo 10;; CU) echo 8;; CD) echo 4;; CL) echo 2;; CR) echo 1;; R3) echo 10000;; L3) echo 20000;; F2) echo 40000;; NONE) echo 0;; *) echo "pad.sh: unknown button $1" >&2; exit 1;; esac; }
case $1 in
  start) if pgrep -f "build/cmake/AidynReChronicled" >/dev/null; then echo "pad.sh: the game is already running (close it first)" >&2; exit 1; fi
         echo "0 0 0" >"$F"
         (cd "$P" && AIDYN_PAD_FILE=$F setsid stdbuf -oL -eL build/cmake/AidynReChronicled >"$P/build/play.log" 2>&1 &)
         for _ in $(seq 1 40); do hyprctl clients -j | grep -q AidynReChronicled && break; sleep 0.5; done
         hyprctl dispatch 'hl.dsp.window.move({ workspace = "1", follow = false, window = "class:AidynReChronicled" })' >/dev/null ;;
  tap)   shift; for b in "$@"; do echo "$(bit "$b") 0 0" >"$F"; sleep 0.2; echo "0 0 0" >"$F"; sleep "${TAPGAP:-0.5}"; done ;;
  hold)  echo "$(bit "$3") ${4:-0} ${5:-0} ${6:-0} ${7:-0}" >"$F"; sleep "$2"; echo "0 0 0" >"$F" ;;
  shot)  g=$(hyprctl clients -j | python3 -c 'import json,sys
for c in json.load(sys.stdin):
    if c["class"]=="AidynReChronicled": print("%d,%d %dx%d"%(c["at"][0],c["at"][1],c["size"][0],c["size"][1]))')
         grim -g "$g" "$D/$2.png" ;;
  alive) pgrep -f "build/cmake/AidynReChronicled" >/dev/null && echo ALIVE || echo DEAD ;;
  *)     sed -n '2,9p' "$0"; exit 2 ;;
esac
