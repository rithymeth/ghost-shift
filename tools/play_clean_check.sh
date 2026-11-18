#!/usr/bin/env bash
# Real input path: runs the engine's player (software Vulkan, virtual display) on Scenes/Play_Clean.ascene, where the player
# starts 2.7 m from a stain and faces it. Without input nothing may be cleaned; with the Clean button held exactly the stain
# in front must be cleaned. Needs the same prerequisites as tools/render_lookdev.sh.
# usage: tools/play_clean_check.sh <engine build dir>
set -euo pipefail
B=${1:?engine build dir}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
"$B/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config development >/dev/null
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
run() { xvfb-run -a -s "-screen 0 1280x720x24" "$B/player/aether_player" --pak "$T/cook" --scene Scenes/Play_Clean.ascene \
        --backend vulkan --size 1280x720 --frames 40 "$@" 2>&1 | grep -E "\[Contract\]|Script error" || true; }
idle=$(run); held=$(run --press MouseLeft)
echo "--- no input:"; echo "$idle"; echo "--- Clean held:"; echo "$held"
echo "$idle" | grep -q "cleaned" && { echo "FAIL: something was cleaned with no input"; exit 1; }
echo "$held" | grep -q "cleaned one (2 left)" || { echo "FAIL: holding Clean did not clean the stain in front"; exit 1; }
echo "$held" | grep -q "Script error" && { echo "FAIL: a script error"; exit 1; }
echo "play-clean check: PASS"
