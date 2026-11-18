#!/usr/bin/env bash
# The assembled server end to end: cook, host the real scene with the Ghost Shift systems, let a scripted player clean the three
# stains and extract, restart the server and check that the company (money, contract ids) survived, then play again.
# usage: tools/assembled_check.sh <ghost-shift build dir> <engine build dir>
set -euo pipefail
G=${1:?ghost-shift build dir}; E=${2:?engine build dir}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
"$E/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config shipping >/dev/null
serve() { GHOSTSHIFT_SAVE_DIR="$T/company" "$G/ghostshift_server" --pak "$T/cook" --port 17901 --max-runtime "$1" --no-lan-advertise --status-interval 0 > "$2" 2>&1 & }
play() { "$G/ghostshift_playtest" 127.0.0.1:17901 "$@" | grep GHOSTSHIFT_PLAYTEST; }
echo "== session 1"; serve 45 "$T/s1.log"; sleep 3; R1=$(play Maya); echo "$R1"; wait
grep -E "company:|COMPLETED|RETREAT|Ghost Shift scene" "$T/s1.log" | cut -c1-170
echo "== session 2 (server restarted; this time the ghost is contained first, so the contract completes)"; serve 60 "$T/s2.log"; sleep 3; R2=$(play Maya --full); echo "$R2"; wait
grep -E "company:|COMPLETED|RETREAT|ghost contained" "$T/s2.log" | cut -c1-170
P1=$(echo "$R1" | sed 's/.*"payout":\([0-9]*\).*/\1/'); P2=$(echo "$R2" | sed 's/.*"payout":\([0-9]*\).*/\1/')
grep -q "money $P1," "$T/s2.log" || { echo "FAIL: the restarted server did not load the company (money $P1)"; exit 1; }
[ "$P1" -gt 0 ] && [ "$P2" -gt 0 ] || { echo "FAIL: a session paid nothing"; exit 1; }
grep -q "COMPLETED" "$T/s2.log" || { echo "FAIL: session 2 was not a completion"; exit 1; }
[ "$P2" -gt "$P1" ] || { echo "FAIL: a completion should pay more than a retreat"; exit 1; }
echo "== server started a third time, no player: the company must hold both payouts"
serve 5 "$T/s3.log"; wait
grep -E "company:" "$T/s3.log" | cut -c1-170
grep -q "1 contract(s) completed" "$T/s3.log" || { echo "FAIL: the company should show one completed contract"; exit 1; }
grep -q "money $((P1 + P2))," "$T/s3.log" || { echo "FAIL: the company did not keep both payouts (expected money $((P1 + P2)))"; exit 1; }
echo "assembled check: PASS (paid $P1, then $P2 after a restart; the company held $((P1 + P2)))"
