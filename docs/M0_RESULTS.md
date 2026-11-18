# M0 networking spike — results so far

Run: `tools/m0_loopback.sh <engine build dir>` (engine `aether_server` + `aether_netclient`, Linux, GCC, one machine, loopback).

| Check | Result |
|---|---|
| Server loads the Ghost Shift scene headless | passed: 40 entities replicated |
| 3 clients join at once | passed: all `join_result: Ok`, ~0.11 s |
| Each client sees scene + pawns | passed: 43 entities, `pawn_seen: true` |
| Snapshots flow | passed: 91 snapshots per client in 6 s |
| Server RPC round trip | passed: ack in ~0.066 s |
| Clean leave | passed: 3/8 -> 0/8 |

## Not proven (do not treat as done)
- A 4th player (the engine host slot) and the in-game player/camera, as opposed to headless clients.
- Movement sync, client prediction, shared objects (the GDD M0 exit criterion).
- Two machines, the internet, NAT traversal or a relay (the GDD's critical risk). Engine docs say no cross-machine or NAT run is recorded.
- Windows. Packaged-player networking is not covered by this check.
