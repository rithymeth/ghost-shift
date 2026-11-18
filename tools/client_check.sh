#!/usr/bin/env bash
# The first-person client against the assembled server (software Vulkan, virtual display):
#  1. a client that does nothing must not clean anything;
#  2. a client facing haunted dust near its spawn point with the sprayer and the clean button held must get exactly that dust cleaned;
#  3. (fresh server, haunting sped up) a client facing the ghost taps T (place trap), Q (stun, once the ghost is awake and has come to
#     it), R (activate) and X (extract): the server must contain the ghost and record the extraction.
# usage: tools/client_check.sh <ghost-shift build dir with the client> <ghost-shift build dir with the server> <engine build dir>
set -euo pipefail
C=${1:?client build dir}; G=${2:?server build dir}; E=${3:?engine build dir}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
"$E/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config shipping >/dev/null
SRV=""; trap 'kill $SRV 2>/dev/null || true' EXIT
start_server() { # <port> <haunting rise per second> <log>
  rm -rf "$T/company"; GHOSTSHIFT_HAUNTING_RISE=$2 GHOSTSHIFT_SAVE_DIR="$T/company" "$G/ghostshift_server" --pak "$T/cook" --port "$1" --max-runtime 240 --no-lan-advertise --status-interval 0 > "$3" 2>&1 &
  SRV=$!; sleep 3; }
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
client() { local port=$1; shift; xvfb-run -a -s "-screen 0 1280x720x24" "$C/ghostshift_client" --pak "$T/cook" --server 127.0.0.1:$port --size 960x540 "$@" 2>&1 | grep -E "ghostshift_client: (done|saved|the session|could not)" || true; }

start_server 17930 0.8 "$T/srv1.log"
echo "== 1. idle client"; client 17930 --name Maya --frames 30 --screenshot "$T/idle.bmp"
grep -q "cleaned one" "$T/srv1.log" && { echo "FAIL: an idle client got something cleaned"; exit 1; }
echo "(waiting for the idle client's pawn to time out: it left without a goodbye)"; sleep 8
echo "== 2. client facing haunted dust with the sprayer, clean held"; client 17930 --name Felix --frames 120 --yaw 51.3 --select 2 --press MouseLeft --screenshot "$T/clean.bmp"
grep -q "cleaned one (11 left)" "$T/srv1.log" || { echo "FAIL: the server did not clean the dust the client was scrubbing"; grep -v "isn.t built" "$T/srv1.log" | tail -8 | cut -c1-150; exit 1; }
[ "$(grep -c 'cleaned one' "$T/srv1.log")" = "1" ] || { echo "FAIL: more than the one dust drift in front was cleaned"; exit 1; }
kill $SRV 2>/dev/null || true; wait $SRV 2>/dev/null || true; sleep 1

start_server 17931 2 "$T/srv2.log"
echo "== 3. trap, stun, activate and extract through the client (about 30 s)"
client 17931 --name Maya --yaw 14.5 --seconds 30 --tap T@3 --tap Q@17 --tap R@19 --tap X@23 --screenshot "$T/sequence.bmp"
grep -q "Trap: ghost contained" "$T/srv2.log" || { echo "FAIL: the client's T, Q, R sequence did not contain the ghost"; grep -v "isn.t built" "$T/srv2.log" | tail -8 | cut -c1-150; exit 1; }
grep -q "RETREAT" "$T/srv2.log" || { echo "FAIL: the client's X did not extract"; exit 1; }
grep -E "Trap:|Contract: (RETREAT|COMPLETED)" "$T/srv2.log" | cut -c1-120
echo "screenshots: $T/idle.bmp $T/clean.bmp $T/sequence.bmp"; echo "client check: PASS"
