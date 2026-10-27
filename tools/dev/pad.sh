#!/usr/bin/env bash
# pad.sh: drive the game for testing through AIDYN_PAD_FILE (src/main/input.cpp). Pad file, play.log and screenshots
# live in the dev folder: AIDYN_DEV_DIR, else a Claude session's own <CLAUDE_JOB_DIR>/tmp/aidyn-dev (so sessions can
# test side by side), else build/dev with the log in build/play.log (by hand). Builds are still shared: build/cmake.
#   pad.sh start            launch, always windowed (AIDYN_WINDOWED) and deaf to real controllers (with AIDYN_PAD_FILE
#                           the game opens none), opened straight on the Dell's workspace; every AIDYN_* variable passes
#                           through (AIDYN_MPK, AIDYN_SETTINGS_DIR, AIDYN_DUMP_TEXTURES, AIDYN_UI_LOG, AIDYN_FPS...)
#   pad.sh tap BTN...       A B Z START DU DD DL DR L R CU CD CL CR R3 L3 F2 (the key: redrawn art off / on) (TAPGAP=seconds between taps, default 0.5)
#   pad.sh hold SECS BTN [X Y [RX RY]]   hold a button (NONE for sticks only), left stick X Y -80..80,
#                                        right stick RX RY -1..1
#   pad.sh shot NAME        screenshot of the game window -> <dev folder>/NAME.png
#   pad.sh alive            ALIVE / DEAD
#   pad.sh dell             move the game window to the Dell's workspace (start does this)
#   pad.sh close            close the test game (only the one this script started, never spitzdano's)
#   pad.sh where            print the dev folder and the log's path
# Test runs use no anti-aliasing or supersampling (AIDYN_TEST_LIGHT=1, src/main/settings.cpp; AIDYN_TEST_LIGHT=0 turns it off).
# The window is moved to the Dell's workspace on start and checked (pad.sh dell does it again); never fullscreen it.
P=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
if [[ -n ${AIDYN_DEV_DIR:-} ]]; then D=$AIDYN_DEV_DIR
elif [[ -n ${CLAUDE_JOB_DIR:-} ]]; then D=$CLAUDE_JOB_DIR/tmp/aidyn-dev
else D=$P/build/dev; fi
mkdir -p "$D"; F=$D/pad.txt
LOG=$D/play.log; [[ $D == "$P/build/dev" ]] && LOG=$P/build/play.log
bit() { case $1 in A) echo 8000;; B) echo 4000;; Z) echo 2000;; START) echo 1000;; DU) echo 800;; DD) echo 400;; DL) echo 200;; DR) echo 100;; L) echo 20;; R) echo 10;; CU) echo 8;; CD) echo 4;; CL) echo 2;; CR) echo 1;; R3) echo 10000;; L3) echo 20000;; F2) echo 40000;; NONE) echo 0;; *) echo "pad.sh: unknown button $1" >&2; exit 1;; esac; }
# The game this script started: the process given this pad file (AIDYN_PAD_FILE). spitzdano's own game window has the same
# class, so windows are always picked by this pid, never by class.
test_pid() {
  local p
  for p in $(pgrep -x AidynReChronicl); do
    tr '\0' '\n' <"/proc/$p/environ" 2>/dev/null | grep -qx "AIDYN_PAD_FILE=$F" && { echo "$p"; return 0; }
  done
  return 1
}
# The test window's field (workspace name or stableId), by pid.
test_window() {
  hyprctl clients -j | python3 -c 'import json,sys
for c in json.load(sys.stdin):
    if c["pid"] == int(sys.argv[1]): print(c["workspace"]["name"] if sys.argv[2] == "ws" else c.get("stableId", ""))' "$1" "$2"
}
# Tests run on the Dell (spitzdano plays on the other screens): the window goes to the workspace the Dell shows and stays
# off fullscreen; checked until it's there (a move sent as the window maps can be lost).
dell_ws() {
  hyprctl monitors -j | python3 -c 'import json,sys
for m in json.load(sys.stdin):
    if "DELL" in m["description"].upper(): print(m["activeWorkspace"]["name"])'
}
to_dell() {
  local ws mon
  ws=$(dell_ws)
  [[ -n $ws ]] || { echo "pad.sh: no Dell monitor found (is it on?)" >&2; return 1; }
  local pid
  for _ in $(seq 1 60); do
    pid=$(test_pid) || { sleep 0.5; continue; }
    mon=$(test_window "$pid" ws)
    [[ $mon == "$ws" ]] && return 0
    [[ -n $mon ]] && hyprctl dispatch "hl.dsp.window.move({ workspace = \"$ws\", follow = false, window = \"pid:$pid\" })" >/dev/null
    sleep 0.5
  done
  echo "pad.sh: could not move the game window to the Dell's workspace $ws" >&2; return 1
}
case $1 in
  start) if test_pid >/dev/null; then echo "pad.sh: a test game is already running (pad.sh close)" >&2; exit 1; fi
         echo "0 0 0" >"$F"
         # Opened by Hyprland straight on the Dell's workspace, without taking the focus (exec_cmd's window rules), so
         # it never shows on spitzdano's screen first. Hyprland's own environment is used, so the caller's AIDYN_* settings
         # go into a small launcher script. Without Hyprland or the Dell: started here and moved after.
         ws=$(dell_ws 2>/dev/null)
         L=$D/launch.sh
         { echo '#!/usr/bin/env bash'; printf 'cd %q || exit 1\n' "$P"
           for v in $(compgen -e | grep -E '^(AIDYN_|SDL_AUDIO|XDG_DATA_HOME)'); do printf 'export %s=%q\n' "$v" "${!v}"; done
           printf 'export AIDYN_PAD_FILE=%q AIDYN_WINDOWED=1 AIDYN_TEST_LIGHT=${AIDYN_TEST_LIGHT-1}\n' "$F"
           printf 'exec stdbuf -oL -eL build/cmake/AidynReChronicled >%q 2>&1\n' "$LOG"; } >"$L"
         if [[ -n $ws && $L != *[\"\\]* ]] && [[ $(hyprctl dispatch "hl.dsp.exec_cmd(\"bash $L\", { workspace = \"$ws silent\" })" 2>&1) == ok ]]; then :
         else (cd "$P" && AIDYN_PAD_FILE=$F AIDYN_WINDOWED=1 AIDYN_TEST_LIGHT=${AIDYN_TEST_LIGHT-1} setsid stdbuf -oL -eL build/cmake/AidynReChronicled >"$LOG" 2>&1 &); fi
         to_dell ;;
  close) pid=$(test_pid) && hyprctl dispatch "hl.dsp.window.close({ window = \"pid:$pid\" })" >/dev/null
         for _ in $(seq 1 20); do test_pid >/dev/null || exit 0; sleep 0.5; done
         echo "pad.sh: the test game is still running" >&2; exit 1 ;;
  dell)  to_dell ;;
  tap)   shift; for b in "$@"; do echo "$(bit "$b") 0 0" >"$F"; sleep 0.2; echo "0 0 0" >"$F"; sleep "${TAPGAP:-0.5}"; done ;;
  hold)  echo "$(bit "$3") ${4:-0} ${5:-0} ${6:-0} ${7:-0}" >"$F"; sleep "$2"; echo "0 0 0" >"$F" ;;
  shot)  # the window itself (grim -T, its foreign-toplevel id), so another window on top doesn't end up in the picture
         pid=$(test_pid) || { echo "pad.sh: no test game running" >&2; exit 1; }
         grim -T "$(test_window "$pid" id)" "$D/$2.png" ;;
  alive) test_pid >/dev/null && echo ALIVE || echo DEAD ;;
  where) echo "$D"; echo "$LOG" ;;
  *)     sed -n '2,/^P=/p' "$0" | sed '$d'; exit 2 ;;
esac
