#!/usr/bin/env bash
# loadgame.sh: start the game and load the first save on the pak (set AIDYN_MPK to a scratch copy for tests).
# AIDYN_START_WAIT: seconds to the title screen (default 6; ~20 with the big world pack loading).
P=$(dirname "$(readlink -f "$0")")/pad.sh
$P start; sleep "${AIDYN_START_WAIT:-6}"; $P tap START; sleep 3; $P tap DD; sleep 0.5; $P tap A; sleep 3; $P tap A; sleep 12
$P alive
