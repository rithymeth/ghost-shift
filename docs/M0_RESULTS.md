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

# M1 slice (gameplay baseline) — headless results

`tools/m1_headless.sh <engine build dir>` runs `Scenes/M1_Test.ascene` (the director auto-cleans) on `aether_server`.

| Check | Result |
|---|---|
| Contract director finds the 3 contamination entities | passed |
| Cleaning shrinks and removes each, completion fires once | passed (`COMPLETE: 3/3` at 4.5 s) |
| Prankster ghost patrols 5 waypoints in order, no script errors | passed |

Not proven: the real input path (hold Clean while facing a stain within 3 m) has not been exercised, because the headless
server has no input. Ghost states, pranks, tools beyond the vacuum, trap, objectives UI, rewards and save are not built.
Cleaning is single-player authority for now; it is not yet replicated or validated across clients.

# Networked cleaning (server-authoritative) — results

A client sends `GhostCleaner::Clean(net_id)` (an `Fn_Server` RPC on its own pawn). The server decides: live target,
within 3 m of the pawn, at most one accepted request per 0.1 s per player; 15 accepted requests remove one contamination.
Counters (`requests`, `accepted`, `rejected`) replicate back so the client sees the verdict.

Run: build against the engine SDK, then run `ghostshift_cleaning_test` (a real `DedicatedServer` and `NetClient` over loopback UDP).

| Check | Result |
|---|---|
| Client sees its own pawn with a GhostCleaner | passed |
| In-range contamination is removed by accepted requests | passed |
| 25 requests at a target 3.5 m away: none accepted, all rejected, target survives | passed |
| Verdict counters replicate to the client | passed |
| Bogus net id changes nothing | passed |
| Burst inside one cooldown window is throttled | passed |
| Contract not complete while one remains | passed |
| Mutation check: with the range check disabled the test reports 5 failures | confirmed |

Limits: one client, one machine, loopback only. The pawn does not move yet (its position is its spawn slot), so range is
checked against that. Tank/battery limits, the Luau `Contract` script (offline play) and this C++ system are still two
separate implementations of the same rule. Two clients cleaning the same target, disconnect during a clean, and
reconnect are untested. The vendored server host (`server/`) is a modified copy of the engine's; resync on engine upgrade.

## Building (Linux, against the installed engine SDK)

```
cmake --install <engine build> --prefix <sdk dir>
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<sdk dir>
cmake --build build && build/ghostshift_cleaning_test
```
(The engine's `cmake --install` stops at the very end if `aether_player` was not built, as on a headless Linux build; the
libraries, headers and CMake package files are installed before that point.)

# Networked movement (predicted, server-authoritative) — results

The vendored server host now runs the engine's `MovementServer` and clients run `MovementClient` (prediction +
reconciliation). Pawns carry `NetMovement`, so the server moves each pawn from its owner's inputs and cleaning range
follows the server's pawn position instead of its spawn slot.

Same test (`ghostshift_cleaning_test`, real server + client over loopback UDP), 16 checks, all passing:

| Check | Result |
|---|---|
| Server moves the pawn from the owner's inputs | passed |
| Client prediction and server agree within 5 cm; zero reconciliation corrections for an honest client | passed |
| A pawn that walks into range can clean the far contamination it could not before | passed |
| Walking away puts it out of range again | passed |
| Speed hack (claiming `dt = 0.1` every tick) | **found and fixed** |

**Finding:** the engine's `MovementServer` trusts each input's `dt`. A client claiming dt = 0.1 at 30 ticks/s moved
**16.92 m in one real second against a 6 m/s limit (2.8x)**. The vendored server now gives each pawn a simulated-time
budget: it may spend at most the real time that has passed, plus a 0.25 s burst. The same attack now moves
**6.22 m**. This fix lives in the vendored host; it is worth reporting upstream to the engine.

Limits: loopback, one client, no packet loss or latency simulated, so reconciliation under bad networks is untested
(GDD target: usable at 100-150 ms RTT). Jumping, collision with walls and the 3D first-person camera are not wired
in the networked path; the pawn walks on a flat plane. A second client and disconnect/reconnect are untested.
