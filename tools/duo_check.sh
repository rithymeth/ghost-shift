#!/usr/bin/env bash
# Two players contain the ghost: one stuns, the OTHER activates the trap (the stunner's own activation is refused while two players are up).
# usage: tools/duo_check.sh <ghost-shift build dir> <aether_cook executable>
set -euo pipefail
G=${1:?ghost-shift build dir}; COOK=${2:?aether_cook}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
EXE=""; [ -f "$G/ghostshift_server.exe" ] && EXE=".exe"; [ -f "$G/RelWithDebInfo/ghostshift_server.exe" ] && { G="$G/RelWithDebInfo"; EXE=".exe"; }
"$COOK" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config shipping >/dev/null
GHOSTSHIFT_HAUNTING_RISE=2 GHOSTSHIFT_SAVE_DIR="$T/company" "$G/ghostshift_server$EXE" --pak "$T/cook" --port 17902 --max-runtime 200 --no-lan-advertise --status-interval 0 > "$T/server.log" 2>&1 &
sleep 3
"$G/ghostshift_playtest$EXE" 127.0.0.1:17902 Felix --partner > "$T/partner.out" 2> "$T/partner.err" &
sleep 2
"$G/ghostshift_playtest$EXE" 127.0.0.1:17902 Maya --full --duo | grep GHOSTSHIFT_PLAYTEST || true
wait
grep -E "ghost contained|COMPLETED|RETREAT" "$T/server.log" | cut -c1-170
grep -q "ghost contained" "$T/server.log" || { echo "FAIL: the ghost was not contained"; exit 1; }
grep -q "GHOSTSHIFT_PARTNER {\"contained\":1" "$T/partner.out" || { echo "FAIL: the partner did not see the containment"; exit 1; }
echo "duo check: PASS (stunned by one player, contained by the other)"
