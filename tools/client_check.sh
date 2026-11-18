#!/usr/bin/env bash
# The first-person client against the assembled server (software Vulkan, virtual display):
#  1. a client that does nothing must not clean anything (and the HUD must start at 0 / 12, checked by eye in the screenshot);
#  2. a client facing a cursed stain with the UV scrubber and the clean button held must get exactly that stain cleaned by the server.
# usage: tools/client_check.sh <ghost-shift build dir with the client> <ghost-shift build dir with the server> <engine build dir>
set -euo pipefail
C=${1:?client build dir}; G=${2:?server build dir}; E=${3:?engine build dir}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
"$E/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config shipping >/dev/null
GHOSTSHIFT_SAVE_DIR="$T/company" "$G/ghostshift_server" --pak "$T/cook" --port 17930 --max-runtime 240 --no-lan-advertise --status-interval 0 > "$T/srv.log" 2>&1 &
SRV=$!; trap 'kill $SRV 2>/dev/null || true' EXIT; sleep 3
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
client() { xvfb-run -a -s "-screen 0 1280x720x24" "$C/ghostshift_client" --pak "$T/cook" --server 127.0.0.1:17930 --size 960x540 "$@" 2>&1 | grep -E "ghostshift_client: (done|saved|the session|could not)" || true; }
echo "== idle client"; client --name Maya --frames 30 --screenshot "$T/idle.bmp"
grep -q "cleaned one" "$T/srv.log" && { echo "FAIL: an idle client got something cleaned"; exit 1; }
echo "(waiting for the idle client's pawn to time out: it left without a goodbye, and a second pawn would spawn 2 m to the side)"; sleep 8
echo "== client facing a cursed stain with the UV scrubber, clean held"; client --name Felix --frames 120 --yaw 78.2 --select 1 --press MouseLeft --screenshot "$T/clean.bmp"
grep -q "cleaned one (11 left)" "$T/srv.log" || { echo "FAIL: the server did not clean the stain the client was scrubbing"; grep -v "isn.t built" "$T/srv.log" | tail -8 | cut -c1-150; exit 1; }
[ "$(grep -c 'cleaned one' "$T/srv.log")" = "1" ] || { echo "FAIL: more than the one stain in front was cleaned"; exit 1; }
echo "screenshots: $T/idle.bmp $T/clean.bmp"; echo "client check: PASS"
