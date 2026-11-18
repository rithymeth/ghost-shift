#!/usr/bin/env bash
# M0 loopback check: cook the project, host it with aether_server, join with 3 headless aether_netclient.
# usage: tools/m0_loopback.sh <engine build dir> [out dir]
set -euo pipefail
B=${1:?engine build dir}; OUT=${2:-m0-out}; HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"; rm -rf "$OUT/cook"
"$B/tools/cook/aether_cook" "$HERE/GHOST-SHIFT.aproject" --out "$OUT/cook" --config shipping >/dev/null
"$B/tools/server/aether_server" --pak "$OUT/cook" --port 17777 --max-runtime 14 --no-lan-advertise --log-file "$OUT/server.log" >/dev/null 2>&1 &
sleep 3
for n in Maya Felix Nora; do "$B/tools/server/aether_netclient" --server 127.0.0.1:17777 --build-id 1 --name $n --duration 6 > "$OUT/client_$n.out" 2>&1 & done
wait
grep -h AETHER_NETCLIENT "$OUT"/client_*.out
