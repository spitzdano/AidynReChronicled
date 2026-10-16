#!/usr/bin/env bash
# newsave.sh [pak]: new game -> through the intro -> save in the first forest, on a scratch pak (default build/dev/test.mpk).
# loadgame.sh with AIDYN_MPK=<pak> then loads it. About 5 minutes.
P=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
pad=$P/tools/dev/pad.sh
export AIDYN_MPK=${1:-$P/build/dev/test.mpk}
rm -f "$AIDYN_MPK"
$pad start; sleep 6; $pad tap START; sleep 3; $pad tap A; sleep 4
$pad tap START; sleep 1; $pad tap A; sleep 6
for _ in $(seq 1 120); do TAPGAP=2.5 $pad tap A; done
$pad shot newsave_field
$pad tap START; sleep 3; $pad tap DD; sleep 0.5; $pad tap A; sleep 3; $pad tap A; sleep 3; $pad shot newsave_saved; $pad tap A; sleep 2
python3 - "$AIDYN_MPK" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
n = sum(1 for i in range(16) if any(d[24 * 32 + i * 32:24 * 32 + i * 32 + 32]))
print(f"newsave.sh: {n} save(s) on {sys.argv[1]}")
sys.exit(0 if n else 1)
PY
