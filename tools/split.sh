#!/usr/bin/env bash
# tools/split.sh: run the toolkit's pinned splat on this project's config.
# Checks baserom.z64 against rom.sha1 first, so a wrong dump fails here.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."
. tools/toolkit.env
. tools/build.conf

tools/check_rom.sh
"$TOOLKIT/.venv/bin/python" -I tools/prepare_symbols.py
"$TOOLKIT/.venv/bin/python" -I tools/prepare_yaml.py
"$TOOLKIT/.venv/bin/python" -m splat split "$YAML" "$@"
