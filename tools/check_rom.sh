#!/usr/bin/env bash
# tools/check_rom.sh: baserom.z64 must exist and match rom.sha1 (C9: fail visibly).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
if [[ ! -e baserom.z64 ]]; then
    echo "baserom.z64 is missing. Link your own dump: ln -s /path/to/rom.z64 baserom.z64" >&2
    exit 1
fi
want=$(cut -d' ' -f1 rom.sha1)
have=$(sha1sum baserom.z64 | cut -d' ' -f1)
if [[ $want != "$have" ]]; then
    echo "baserom.z64 SHA-1 is $have; this project expects $want (AIDYN_CHRONICLES, NAYE v0)." >&2
    exit 1
fi
