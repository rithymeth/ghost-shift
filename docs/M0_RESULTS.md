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

# Contract results, payout and the company save — results

From the design doc: grades D to S, retreat for partial rewards, payout = base x difficulty (+ bonuses - penalties),
"contract results award currency once", "company save survives restarting", "disconnects do not corrupt progress".
**All numbers are placeholders** (`ContractConfig`); property damage is not modelled, so the penalty is always 0.

Rules: score = 40 x cleaned + 30 x ghost contained + 20 x safety (4 incapacitations = 0) + 10 x pace (par 900 s), grade
S >= 95, A >= 85, B >= 70, C >= 55, else D. Payout for a completed contract = 1000 x grade multiplier (D 0.5 .. S 1.5);
a retreat (extracting before everything is cleaned and the ghost contained) is graded D and pays 40% of the base scaled
by how much was cleaned. The company (money, level, reputation, contracts, best grade) is saved with the engine's save
system: atomic write, CRC, previous file kept as `.bak`.

`ghostshift_contract_test`, 27 checks, all passing: grading boundaries and payouts; a missing save starts a new company;
a paid contract survives a restart; replaying a paid contract id pays nothing; a damaged save is recovered from the
backup and says so (the last contract's pay is lost, not everything); with nothing readable the company becomes
read-only, refuses to be paid into and **leaves the damaged files untouched**; two end-to-end runs with two real clients
(a full completion graded S for 1500, and a retreat graded D for 200); both players pressing Extract, and pressing it
again later, pays exactly once; clients see the result (replicated).

Mutation checks, each caught: replayed contracts pay again, a retreat pays like a completion, an unreadable save stays
writable (would overwrite it).

Not built or tested: the pay screen and any UI, level-up unlocks and the upgrade bench, per-player progression and its
validation (the company is the host's), property damage and the penalty, bonus objectives, difficulty selection, a
future-version save file (the engine refuses it; not exercised here), and a crash in the middle of a save (the engine
tests the atomic write; this game's use of it was not killed mid-write). A recovery from the backup loses the most
recent contract's pay, by design of the backup.

# The real input path, and the glow bug it exposed

`tools/play_clean_check.sh <engine build dir>` runs the engine's own player (software Vulkan, virtual display) on
`Scenes/Play_Clean.ascene`, where the first-person player starts 2.7 m from the ectoplasm and faces it:

| Run | Result |
|---|---|
| No input | nothing cleaned |
| Left mouse (Clean) held | exactly the stain in front is cleaned (`cleaned one (2 left)`); the other two, out of range and not in front, are left alone; no script errors |

This is the first time the **real** input path was exercised: until now cleaning was only tested headless with an
auto-clean switch or over RPC.

**Bug found by looking at the frames:** after a stain was cleaned its puddle disappeared but its green/purple point light
stayed on the floor, because nothing linked a stain to the light that makes it glow. Stains and their lights now share a
`Glow:<name>` tag: the Luau `Contract` script dims the light as the stain is scrubbed and removes it with the stain, and
the C++ server `CleaningSystem` does the same (tested: the glow goes, an unrelated light stays; mutation caught).

Not covered: the vacuum in the player's hands (there is no first-person vacuum model or cleaning effect), a HUD (the
player cannot see how much is left), looking down with the mouse, sound, and the networked first-person client (this run
is the offline player; the networked path is tested only with headless clients). The player turns with the mouse, which
this scripted run does not exercise.

# First-person HUD — `docs/images/hud_idle.png`, `hud_held.png`, `hud_done.png`

The offline player now draws a HUD from a UI layout (`Content/UI/Hud.aui`, generated by `tools/build_hud.py`), shown by a
`WidgetComponent` on the director entity and driven by name from the Luau `Contract` script (`UI.SetText`, `UI.SetValue`):
contamination count, a progress bar, the contract timer, a control hint, an aim prompt ("HOLD LEFT MOUSE TO CLEAN" while
a stain is in range and in front) and "CONTRACT COMPLETE" when everything is cleaned. Font: Roboto (Apache-2.0, copied from
the engine's AETHER-01 project; see `THIRD_PARTY_NOTICES.md`). The engine's player draws a game's own UI layers, but the
engine's built-in HUD is hard-wired to its AETHER-01 project, so this uses the generic UI path.

Seen in real-player renders (software Vulkan): idle, aimed at a stain: `0 / 3`, empty bar, aim prompt; Clean held for a few
seconds: `1 / 3`, a third of the bar, the cleaned stain's glow gone; the auto-clean scene at the end: `3 / 3`, full bar,
"CONTRACT COMPLETE". The headless server still runs the same script without errors (its UI calls return false with no UI).
`tools/m1_headless.sh` and `tools/play_clean_check.sh` both still pass.

Not covered: the HUD shows only what the offline Luau rules know. The haunting stage, who is downed, trap and extract state,
the contract result and the company payout live in the C++ server and are replicated, but no HUD displays them yet. There
is no pause or results screen. The layout was judged at 1280x720 only; other resolutions and aspect ratios were not
looked at. The hint text is hard to read where a bright stain sits behind it. Nothing was checked on real hardware.

# Ghost types, and the Caretaker — results

`GhostBrain` now has a `type` (the design doc's six archetypes: Prankster, Poltergeist, Mimic, Wailer, Hoarder, Caretaker).
**Only the Prankster and the Caretaker have behaviour of their own so far; the other four are names only.**

Caretaker (design doc: "recontaminates moved belongings", counter "restore room layout"). Placeholder numbers: while it is
Pranking or Hunting it re-dirties a spot the team already cleaned every 12 s, at most 3 times per contract, so a team can
always finish. The contract counts the new contamination (the late-registration path). Stunning or containing it stops
it; its interval restarts when it becomes active again; a Dormant Caretaker does nothing.

`ghostshift_caretaker_test`, 11 checks, all passing: nothing before the interval; a cleaned spot is dirty again afterwards
and it is the spot that was cleaned; the cap of 3; the team can finish for good after the cap; a Prankster never does it;
stunned, recovering, contained and dormant Caretakers; mutation checks (any ghost type re-dirties, no cap, ghost state
ignored, interval not restarting) are each caught.

"Restore the room layout" is not built: there are no movable props yet, so the Caretaker re-dirties cleaning spots only.
The systems are tested in isolation (no network); the Caretaker has not been run in a networked session.

**An assembly gap to be honest about:** the C++ systems (cleaning, haunting, traps, contract, caretaker) are exercised only
inside test programs. There is no `ghostshift_server` executable yet that loads the cooked scene and runs them together, and
the scene's authored ghost is the Luau patrol, not a `GhostBrain`. Building that server, and deciding how scene entities
map to the replicated ghost, director and contamination archetypes, is the next structural step.

# The assembled server (`ghostshift_server`) — the C++ systems now run together

Until now the C++ systems (cleaning, trap, haunting, Caretaker, contract) ran only inside test programs. `ghostshift_server`
(`server/main.cpp`, a modified copy of the engine's `aether_server`) now loads the cooked game headless, maps the scene onto
the replicated entities (`src/gs/scene_setup.cpp`: tag `Ghost` becomes the replicated ghost with a `GhostBrain`, optional tag
`GhostType:<Name>`; tag `Director` becomes the replicated director with `Haunting` and `ContractResult`), hands those entities'
logic to C++ (their offline Luau scripts and the template player's controller are removed on the server), loads the company from
`$GHOSTSHIFT_SAVE_DIR` (default `./company`) and runs every system each tick, in the order the tests use.

`tools/assembled_check.sh <ghost-shift build dir> <engine build dir>`: cook, host the real `Main` scene, let a scripted
real-time player (`ghostshift_playtest`: joins, walks, cleans three stains by position, extracts) play over real UDP, restart
the server, play again, start it a third time with nobody in it:

| Check | Result |
|---|---|
| Scene mapped: 1 ghost, 1 director, 3 contamination, 3 scripts handed to C++ | passed |
| The player walks, cleans all three stains (45 accepted requests, 0 rejected), extracts | passed; the server logs `RETREAT: grade D, payout 400 (paid)` |
| The result replicates to the client | passed |
| After a server restart the company is loaded (money 400) and the next session pays again | passed |
| A third start with nobody in it holds both payouts (800) | passed |

`ghostshift_scene_setup_test` (8 checks, two mutations caught) covers the mapping itself.

Two script bugs found on the way, both mine, both informative: (1) the player first walked next to the ectoplasm and then
tried a stain 4.9 m away; the server rejected every request, which is the range rule working in the assembled server;
(2) a client that sends no input while standing still leaves the server pawn's velocity unchanged, so reversing direction cost
about a metre. A real client sends an input every frame, including "no movement", and the playtest now does.

It is a **retreat, not a completion**: the ghost is never contained in this run. Completing a contract in the assembled server
needs the trap sequence (place, bait, stun, activate) from a networked client, which no scripted player does yet. The ghost
here is the authored Prankster, so the Caretaker and the other types are not exercised in this run. The Windows player cannot
join this server: it would need a custom client that carries the Ghost Shift components (the engine's player joins the
engine's own server). Everything ran on one machine over loopback.

# A full contract on the assembled server — completed, graded, paid, persisted

`tools/assembled_check.sh` now plays two sessions against `ghostshift_server`, restarting it between them:

| Session | What the scripted player does | Server result |
|---|---|---|
| 1 | cleans all three stains, extracts | `RETREAT: grade D, payout 400` (the ghost was never contained) |
| 2 (server restarted) | places the trap beside the ghost, stuns it, activates, cleans all three stains, extracts | `Trap: ghost contained`, then `COMPLETED: grade S, payout 1500` |
| 3 (started with nobody in it) | none | `company loaded (money 1900, 1 contract(s) completed)` |

So the whole loop of the design doc's first-playable checklist now runs in one process: join, move, clean, contain, extract,
results, currency awarded once, company saved and loaded across restarts. The completed contract is graded S because the run
took seconds against a 900 s par, with no attacks. The final replicated ghost state the client saw was Contained (5).

**Balance problem this run exposed (my placeholder rules, not a bug):** the ghost was contained while still Dormant, with no
baiting. The Prankster starts about 4.2 m from the spawn point; a trap can be placed 3 m from the player, a stun reaches 5 m,
the capture radius is 3 m, so any ghost within roughly 6 m of where a player stands can be trapped on the first seconds of a
contract. The design doc's "baiting" is not required by these numbers, and a one-person S in seconds is not the intended 15 to
25 minute contract. This needs tuning in playtests or a rule change (for example: a Dormant ghost cannot be stunned, the trap
needs time to arm, or the stun needs line of sight). I left the rules as they are rather than choose a design for you.

`GHOSTSHIFT_HAUNTING_RISE` (environment variable) overrides the haunting's rise per second on the server, for tuning and tests;
the check does not use it.

Still not covered: two or more players coordinating the sequence, the ghost fighting back during the sequence (it was Dormant),
the Caretaker and other types in the assembled server, a custom first-person client that can join this server, and anything
across two machines or on Windows.

# Trap balance fix: baiting is now required

The previous section found that a Dormant ghost could be trapped in the first seconds. Smallest change, a config switch
(`TrapConfig::stun_needs_awake`, on by default): **a Dormant ghost cannot be stunned.** The team has to let the haunting wake
the ghost and bait it to the trap. This is my choice among the options I listed (not stunning a Dormant ghost, a trap that
needs time to arm, line of sight); it is a placeholder, easy to reverse, and the design is yours.

Tests: `ghostshift_trap_test` has a new check (a Dormant ghost cannot be stunned; an awake one can); removing the rule fails 7
checks. `tools/assembled_check.sh` now waits, standing by the trap, until the replicated ghost state shows it awake, and runs
the server with `GHOSTSHIFT_HAUNTING_RISE=2` so that takes about 9 s instead of about 25 s. Result: the ghost woke after
8.7 s, was stunned and contained, and the contract completed `grade S, payout 1500`; the company held 1900 after a restart.

**Still not a 15 to 25 minute contract.** Even with baiting, the run is an S in roughly 25 s: the scene has only three stains,
so there is little to clean, and the grade's pace term (par 900 s) never bites. Pacing is a content problem (more
contamination, locked rooms, more tasks) and a tuning problem (the haunting's rise, the grade thresholds), not something this
rule fixes. Nothing here has been played by a person, so none of these numbers has been felt yet.

# Tank, van and a bigger room — results

**Why:** a contract with three stains is over in seconds. The design doc's "manage tank and battery" gives cleaning a rhythm.

**Rule (server-authoritative, placeholder numbers):** each player's vacuum has a tank of 3 contamination units (`CleaningConfig::tank_capacity`).
Every accepted scrub request adds 1/15 of a unit; a request that would overflow it is rejected and `tank_full` is set (replicated, so a
HUD can show it). The tank is emptied with `EmptyTank`, only within 3 m of an entity tagged `Van`, and not while the player is downed.
Tanks are per player. There is no battery yet.

**Scene:** six more contamination spots (nine in all, spread across the 12 x 12 m room, each with its glow light) and a van blockout
at the front left (`docs/images/lookdev_04_nine_stains.png`, `lookdev_05_van.png`; the van is a white box with a glowing stripe, a
placeholder).

`ghostshift_tank_test` (10 checks): the tank fills; it holds 3 units (45 requests) and then reports full; a full tank blocks cleaning;
no van or a far van cannot empty it; at the van it empties and cleaning resumes; each player has a tank; a downed player cannot empty
it. Four mutations (no capacity limit, empty from anywhere, downed players can empty, empty with no van) are each caught. The older
tests give their pawns a roomy tank, because they test other rules. All nine test programs pass.

**The assembled check, now with nine stains:** the scripted player walks to the nearest stain, scrubs it, and goes to the van when its
tank fills. Retreat session: 9 cleaned, 2 van trips, 135 accepted requests, `RETREAT: grade D, payout 400`. Completion session: the
ghost woke after 8.7 s, was stunned and contained, 9 cleaned, 2 trips, `COMPLETED: grade S, payout 1500`; the company held 1900 after a
restart. Zero movement corrections.

**Pacing, honestly:** the scripted player still finishes in about 35 s (it never hesitates, aims or looks around). That is far from the
12 to 18 minutes the design doc gives Willow House. A person will be slower, but I do not know by how much and nobody has played this.
More area, more contamination types with different tools, locked rooms and the haunting's pace are what could close that gap; the
numbers here are placeholders.

**The offline game was not updated:** the Luau offline player (`Contract.luau`) has no tank, van or trap. It counts the nine stains but
the rules there are the old ones. Two implementations of the rules still exist; the C++ one is the one tested over the network.

Not covered: the tank on any HUD, the battery, tool types (vacuum, UV scrubber, sprayer) with different contamination, and a person
playing any of it.

# Tool types — results

**Rule (server-authoritative, placeholder numbers),** from the design doc's table of contamination and tools:

| Contamination (tag) | Tool | Resource (placeholder) |
|---|---|---|
| Ectoplasm (`Kind:Ecto`) | Spectral vacuum | tank: fills, 3 units |
| Cursed stains (`Kind:Stain`) | UV scrubber | battery: drains, 2 units (power hungry) |
| Haunted dust (`Kind:Dust`) | Purification sprayer | canister: drains, 4 units |
| no `Kind` tag | any tool | whichever is selected |

A player selects a tool (`SelectTool`, replicated); a request with the wrong tool is refused and counted (`wrong_tool`); each tool has its
own resource; the van recharges all of them. Possessed furniture (tether) and residual energy (neutralizer) are **not built**.

**Scene:** twelve contamination pieces: four ectoplasm, five stains, three dust drifts (`docs/images/lookdev_06_three_kinds.png`). My first
dust was dark grey on a dark floor and read as shadows; it is now pale with a faint cold glow so the three kinds are distinguishable by sight.

`ghostshift_tools_test` (14 checks), all passing. Four mutations are each caught: any tool cleans any kind, the UV scrubber never drains,
the van does not recharge the battery, a battery that reaches zero refills itself.

**A real bug the test caught while I was writing it:** I first marked "this player's battery and canister are not initialised yet" with
the value -1 and re-initialised anything below zero. After exactly 30 requests float rounding leaves the battery at about -0.0000002,
which the code took for "uninitialised" and refilled: an infinite battery. The test showed 60 accepted requests and a battery still at
2.0. It now uses an explicit flag, and the fourth mutation above reintroduces the old behaviour to prove the check would catch it.
(I also removed a check of mine that read `|| true` and could never fail.)

**Assembled server, twelve pieces:** the scripted player switches tool as needed (10 switches), services the tools at the van (2 trips),
180 accepted requests: retreat session `grade D, payout 400`; after a restart the completion session contained the ghost and finished
`COMPLETED: grade S, payout 1500`; the company held 1900 after another restart.

Not built or tested: any HUD for the tool, tank, battery or canister; the battery draining while a tool is merely held; the UV scrubber
"revealing" hidden markings (stains are always visible here); collateral damage from the sprayer; a person choosing tools. The offline
Luau game ignores tool kinds. Pacing is still about a minute for a scripted player, not the 12 to 18 minutes of the design doc.

# First-person client (`ghostshift_client`) — compiled, run against the assembled server

`client/main.cpp`, an opt-in target (`-DGHOSTSHIFT_BUILD_CLIENT=ON`; it needs an SDK installed from an engine build with the window and
Vulkan backends, and the SDK used for the headless tests was installed without them, so I reinstalled one from a full engine build).
It follows the structure of the engine's `aether_player` and is written against the SDK's `Game`, `SceneRenderer` and `Window`.
Design: the cooked scene is loaded **locally** for what you see (models, lights, HUD); every rule and position comes from the server;
a presentation layer maps the replicated entities onto the local scene (the ghost and trap follow the server, other players get a proxy
pawn, a cleaned stain and its glow disappear); input goes out as the same RPCs the tests use.
Controls: WASD move, mouse look, hold left mouse to clean, 1/2/3 tool, F empty/recharge at the van, T place trap, Q stun, R activate, X extract.

**What was seen** (software Vulkan under a virtual display, `docs/images/client_01_idle.png`, `client_02_cleaned_one.png`):
the client joins the server, renders the room with the first-person vacuum in view, and its HUD is driven by replicated state
(`HAUNTING QUIET`, `CONTAMINATION 0 / 12`, `VACUUM TANK 0.0 / 3.0`). A client that faces a cursed stain, selects the UV scrubber and holds
Clean gets exactly that stain cleaned by the server (`cleaned one (11 left)`); the client then shows `CONTAMINATION 1 / 12` and
`UV SCRUBBER BATTERY 1.0 / 2.0` (one unit spent) and the purple stain and its glow are gone. Zero movement corrections.
`tools/client_check.sh <client build> <server build> <engine build>`: an idle client cleans nothing; the scrubbing client cleans exactly
one stain (two consecutive runs passed).

**A design flaw this exposed, fixed:** my first presentation layer hid a stain whenever its replicated entity was missing, and the HUD
showed `5 / 12` cleaned with nobody cleaning. The engine's replication client treats a snapshot with no acknowledged baseline (at start-up,
or whenever the client reads packets slowly, as this software-rendered one does at about 5 fps) as the complete world, and despawns
everything missing from it: in one second the client counted 129 spawns and 96 despawns, including its own pawn. "Missing" can therefore
never mean "cleaned". The server now replicates `ContractResult::cleaned_mask`, a bit per contamination in registration order
(`CleaningSystem::CleanedMask`), set once and never cleared; the client hides a stain only when its bit is set. Tested in
`ghostshift_contract_test` (mask 0b11 after cleaning both, 0b01 after one, seen identically by a client).

**Weaknesses found, not fixed:**
- The engine host spawns each pawn at `x = number of players × 2`, not at a spawn point, and a client that exits without a goodbye keeps
  its pawn until the 5 s silence timeout, so a second player can spawn 2 m to the side. That is vendored engine code (`server/src/dedicated_server.cpp`).
  Real spawn points (tagged scene entities) are needed; it also puts later players inside walls.
- With about 70 scene entities the server replicates every static wall, floor tile and light, which consumes the 1 KB snapshot budget.
  Only dynamic entities need identities; `replicate_scene` could be off.

**Not covered:** mouse look (only the initial look direction was scripted), the trap, stun and extract through the client, seeing another
player's proxy pawn, the van trip, the other tools, D3D12/Windows (I could only run software Vulkan), a real window, and any person
playing it. The client is a vertical slice, not a finished client.

Also added: blockouts for the trap (`gs_trap`) and for another player's pawn (`gs_pawn`), and hidden proxy entities for them in the
scene, parked 100 m below the floor until the client moves them into place. The offline check (`tools/m1_headless.sh`) still passes.
