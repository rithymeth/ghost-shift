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

# Bad networks and two clients — results

`ghostshift_netcond_test`: two clients, each behind an in-process UDP relay (`tests/netem_relay.h`) that adds 60 ms
one-way latency, up to 20 ms jitter (so packets reorder) and 5% loss in both directions: roughly 120-160 ms RTT, the
GDD's stated usable range. Real UDP sockets, simulated time. `tc netem` was not available in the build container.

| Check | Result |
|---|---|
| Both clients join and see their own pawn over the lossy link | passed |
| Prediction under loss: Maya walks out and back; predicted and server positions agree within 5 cm after settling | passed (0 corrections, 60 inputs, 42 acks) |
| The relays really dropped packets (37 of 625 and 35 of 600) | passed |
| Two clients clean the same targets at once; the contract completes once | passed |
| Work is conserved: exactly 45 accepted requests for 3 targets x 15 (no double credit, none lost) | passed |
| Mutation check: changing the work per request makes the conservation check fail | confirmed |

Reading it honestly: zero corrections is expected here. The movement step is deterministic and the server runs every
input it receives (each packet repeats the last 8), so a loss rate this low only delays acks. Corrections would appear
if the server and client disagreed about the world (collision, a moving platform, pushes), and none of that exists in
the networked path yet. The 100-150 ms target is checked for movement and cleaning, not for a full 4-player session.

Still untested: a disconnect or timeout in the middle of a clean, reconnect, loss above 5%, four clients, and any real
network (two machines, NAT). The relay is test code; it is not a substitute for a real internet run.

# Four players, a pulled cable and a reconnect — results

`ghostshift_session_test`: four clients behind impaired links (40 ms one-way latency, up to 20 ms jitter, 3% loss each
way) cleaning four contamination. After 20 accepted requests one client vanishes with no goodbye (the relay and client
stop, as with a pulled cable).

| Check | Result |
|---|---|
| Four players join, each sees its own pawn | passed |
| A player vanishes mid-contract; the other three finish it | passed |
| Work conserved across the disconnect: exactly 60 accepted requests (4 x 15) | passed |
| The server times the silent player out (engine default: 5 s of silence) and removes the pawn | passed (4 -> 3 players) |
| The same player rejoins on a new connection and gets a new pawn | passed |
| Contamination that appears after the contract started is registered (the Caretaker case) | passed (found and fixed while writing this test) |
| The rejoined player cleans it; exactly 15 accepted requests; the contract completes again | passed |
| Mutation check: disabling the late registration makes 3 checks fail | confirmed |

The accepted-request total is tracked by the cleaning system, not summed from pawns, so it survives a player leaving.

Still untested: a disconnect while a request is in flight on a reliable channel, the same name joining while the old
connection has not timed out yet, loss above 5%, a real network (two machines, NAT, relay), Windows, and any of this in
the first-person player rather than headless test clients. Mid-session progress is not saved, so a rejoining player
today gets a fresh pawn and no memory of what they did.

# Haunting and ghost AI (server-authoritative) — results

`ghostshift_haunting_test`: real server and two clients over loopback UDP. Rules come from the design doc (five haunting
stages; ghost states Dormant, Investigating, Pranking, Hunting, Stunned, Contained; cleaning lowers intensity; attacks
incapacitate rather than eliminate; every dangerous event has a counteraction). **All numbers are placeholders**
(`HauntingConfig`, `CleaningConfig`) to be tuned in playtests; they are not design values.

| Check | Result |
|---|---|
| Nobody in the property: intensity stays 0, ghost stays Dormant | passed |
| Intensity climbs Quiet, Stirring, Active, Dangerous in order; ghost follows Dormant, Investigating, Pranking, Hunting | passed (first attack at ~86 s with nobody cleaning) |
| The ghost pranks while Active; a hunting ghost incapacitates a player | passed |
| Clients see the stage, the ghost's state and who is downed (replicated) | passed |
| A downed player's clean requests are not accepted | passed; mutation (rule removed) fails 2 checks |
| A hunted team can still finish the contract; cleaning relief calms the ghost out of Hunting | passed (completed at ~96 s, intensity 32) |
| Nobody stays down afterwards | passed |
| Stun freezes a hunting ghost for its duration, then it recovers | passed (no tool calls it yet) |
| After recovering, a player is protected for 3 s before they can be re-attacked | passed; mutation (protection removed) fails |

**Weak spot found while testing:** my first end-to-end test passed even with the protection window removed, so it did not
show that the window matters; two players and fast relief let the team finish anyway. The protection is now tested
directly (ghost adjacent to a recovering player), but whether a *single* player or a slower team can be locked out has
not been measured. Tune with playtests.

Not built or tested: Contained (the trap), items and tools the Prankster steals, Stun being triggered by a real tool,
ghost pathfinding (it walks straight at the player through walls), the other five ghost archetypes, scene-authored ghosts
(the test spawns them with archetype "ghost"; the project's Luau `GhostPatrol` and C++ `HauntingSystem` are separate
implementations), and anything rendered.

# Containment: trap, stun, Contained — results

Rules (from the design doc's "coordinate placement, baiting, and activation"; all numbers are PLACEHOLDERS): a player can
place one trap within 3 m of themselves (placing again moves it); a player can stun a ghost within 5 m (20 s cooldown);
the trap contains a ghost only if the ghost is **Stunned and within 3 m of the trap**. A ghost that is not stunned
resists. A downed player cannot place, stun or activate. A contained ghost ends the haunting (intensity 0). Extraction
needs every ghost contained **and** every contamination cleaned.

`ghostshift_trap_test`, 22 checks, all passing: isolated rules (range, NaN input, one trap, resistance, cooldown, distance,
downed players) plus one end-to-end run with two real clients over loopback UDP: Maya places the trap, an early activation
fails, the hunting ghost is baited toward the trap, Felix stuns it inside the radius, Maya activates, the ghost is
contained, the other client sees the ghost and the trap (replicated), the haunting drops to 0, extraction is refused until
the stains are cleaned and allowed after.

Mutation checks, each caught by the check meant to catch it: capture without a stun, capture from any distance, no stun
cooldown, downed players allowed to use tools.

Not built or tested: the trap as a physical, carryable object, real tools (the RPCs exist; no tool item calls them), the
trap failing or being stolen by a Prankster, multiple ghosts, the ghost avoiding or resisting the bait, ghost pathfinding
(it still walks straight through walls), "extract" as an actual action that ends the contract and pays out, and anything
rendered or played. The Luau `GhostPatrol` and the C++ ghost remain separate implementations.

# First render (software Vulkan) — what it shows and what it does not

`tools/render_lookdev.sh <engine build dir>` renders the lookdev scene with the engine's own player on lavapipe (software
Vulkan) under Xvfb: `docs/images/lookdev_01_software_vulkan.png`, 1280x720, frame 40 (7.6 s of game time).

Seen in the render: the player loads the project and runs its three scripts (the ghost patrols); the room is lit by the
warm lamps and the coloured point lights, which throw coloured pools on the floor (magenta stains, green ectoplasm);
the Prankster glows cyan with its hat and casts a shadow; the lamps and the ghost have a soft glow.

What it does not show: the moonlight through the window is not readable (the ghost stands in front of it), and the
scene is a blockout (primitive shapes, no textures, no ceiling), so this is a lighting and pipeline check, not an art
check. It is a software renderer, so frame rate and GPU behaviour say nothing about real hardware, and the engine states
that some features (sun shadows) are D3D12-only, so the Windows render may differ.

# Lookdev iteration 2 (software Vulkan) — `docs/images/lookdev_02_wide.png`, `lookdev_03_table.png`

Two fixed-camera shots (`Lookdev_A`, `Lookdev_B`, built by `tools/build_lookdev_scene.py`) make before/after comparisons fair.
What the first renders showed, and what changed:

| Problem seen in the render | Change |
|---|---|
| The window was a solid glowing box (read as a projector screen) and blocked the moonlight | Now a real opening with a frame and mullions; the moon's light enters and lands as a window-shaped patch on the floor |
| The ghost's cyan washed out to near-white | Darker base colour, lower emissive strength: it reads as cyan |
| No ceiling, so it did not feel like an interior | Ceiling added; the lamps now light it |
| The vacuum read as an olive blob (metallic yellow under cyan light) | Less metallic, brighter yellow |

Still weak (blockout stage): the ghost is a pile of spheres, the vacuum is hard to read, the floor is dark, the moon patch is
modest, and there are no textures, props or detail. Raising the moon's intensity from 1.2 to 4.0 barely changed the patch,
which I did not investigate. These renders are on a software renderer; the Windows GPU render may differ.
