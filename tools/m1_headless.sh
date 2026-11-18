#!/usr/bin/env bash
# M1 slice check: runs the auto-clean test scene on the headless server and prints the script log.
# usage: tools/m1_headless.sh <engine build dir> [out dir]
set -euo pipefail
B=${1:?engine build dir}; OUT=${2:-m1-out}; HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"; rm -rf "$OUT/cook" "$OUT/server.log"
"$B/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$OUT/cook" --config shipping >/dev/null
"$B/tools/server/aether_server" --pak "$OUT/cook" --scene Scenes/M1_Test.ascene --port 17778 --max-runtime 14 \
  --status-interval 0 --no-lan-advertise --log-file "$OUT/server.log" >/dev/null 2>&1
grep -E "\[Contract\]|\[Ghost\]|ERROR" "$OUT/server.log"
grep -q "COMPLETE: 3/3" "$OUT/server.log" && echo "M1 headless check: PASS" || { echo "M1 headless check: FAIL"; exit 1; }
