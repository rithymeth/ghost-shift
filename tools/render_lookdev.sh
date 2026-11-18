#!/usr/bin/env bash
# Renders the lookdev scene to a screenshot with the engine's player on SOFTWARE Vulkan (lavapipe) under a virtual display.
# Needs: an engine build with the window and Vulkan backends (libvulkan-dev, glslang-dev, X11 dev libs), xvfb, mesa-vulkan-drivers.
# usage: tools/render_lookdev.sh <engine build dir> [out.png]
# NOTE: the player writes a BMP even when the name ends in .png; this converts it (needs Python + Pillow).
set -euo pipefail
B=${1:?engine build dir}; OUT=${2:-lookdev.png}; HERE=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
"$B/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$T/cook" --config development >/dev/null
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
xvfb-run -a -s "-screen 0 1280x720x24" "$B/player/aether_player" --pak "$T/cook" --backend vulkan --size 1280x720 --frames 40 --screenshot "$T/shot.bmp" >/dev/null
python3 -c "from PIL import Image; Image.open('$T/shot.bmp').convert('RGB').save('$OUT')"
echo "wrote $OUT"
