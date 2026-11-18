# First playable: what is proven, what is not

**Windows build (2026-11-18):** the rules, server core and 11 test programs build with VS 2022 against an installed engine SDK. 10 of 11 pass; `ghostshift_contract_test` fails 4–6 checks because `GhostTools::extract_requested` reads non-zero on the server shortly after a player joins (the value is 0 right after the pawn is created, and the contract then retreats immediately). Cause not found; the same failure occurs without the trap change.

The design document's first-playable acceptance checklist (section 12), against the evidence in this repository.
"Proven" means a test or a recorded run shows it, on **Linux, with a software renderer, on one machine**. Nothing here has run on
Windows, on a real network, or in front of a person. Placeholder numbers throughout.

| # | Checklist item | Status | Evidence, and what is missing |
|---|---|---|---|
| 1 | Two to four remote players join a session | **Partly** | Four clients join and play over UDP loopback through a link with 40 ms latency, jitter and 3% loss (`session_test`); two clients at 120â€“160 ms round trip and 5% loss (`netcond_test`); a scripted player and the first-person client join the real server. **Not shown:** another machine, the internet, NAT or a relay (the design's critical risk, "Friend joining unreliable"), or a player-hosted game (the server here is dedicated). |
| 2 | Player movement and object interaction synchronise | **Partly** | Predicted movement agrees with the server within 5 cm with zero corrections, also under loss; a speed hack was found and closed; cleaning, tool, tank and trap verdicts replicate. **Not shown:** physics objects, carrying furniture, other players' movement seen on a client (the proxy pawn was added but never looked at), reconciliation against collisions (there are none in the networked path). |
| 3 | Spectral vacuum removes shared contamination | **Proven (headless)** | Two and four clients clean the same targets; the total accepted work is exactly conserved (no double credit, none lost), also across a disconnect. The first-person client cleaned a stain through real input. |
| 4 | Prankster ghost patrols and reacts consistently | **Weak** | The server's ghost is a state machine tied to the haunting (Dormant, Investigating, Pranking, Hunting, Stunned, Contained): it follows players, incapacitates them (never eliminates), calms when the team cleans. It does **not** patrol, and its "prank" is a counter; no item theft. The authored patrol exists only in the offline Luau script, which is a separate implementation. It walks straight through walls. |
| 5 | Trap containment requires coordinated play | **Partly (rule + test)** | With two or more players up, the player who stunned the ghost cannot also activate the trap (`TrapConfig::require_second_player`, `trap_test`: stun by A, A refused, B contains). Solo play is unchanged. **Not shown:** through the first-person client or the scripted playtest, which are single-player and were not updated. |
| 6 | Objectives update correctly for every client | **Mostly (headless)** | The cleaned state is an explicit replicated mask (a flaw where clients inferred "cleaned" from a missing entity was found and fixed); the contract result replicates and a second client reads the same values. The HUD was only looked at for one client at a time. |
| 7 | Contract results award currency once | **Proven** | Two players pressing Extract, and a later press, pay once; replaying a paid contract id pays nothing; mutation checks catch the opposite behaviour. |
| 8 | Company save survives restarting | **Proven** | The assembled check: paid 400, restarted, paid 1500, restarted, the company held 1900 and one completed contract. |
| 9 | Disconnects do not corrupt progress | **Partly** | A player vanishing mid-contract does not change the work total; a damaged save is recovered from the engine's backup, and with nothing readable the company becomes read-only and the files are left untouched. **Not shown:** killing the server in the middle of a save (the engine tests its atomic write; this project did not), a crash during the contract itself (progress is only saved when a contract finishes). |
| 10 | Performance and latency measured on target PCs | **No** | No frame times on real hardware (only a software renderer), no bandwidth measurement against the targets, latency only simulated on loopback. |

## Beyond the checklist

- **A contract does not last 12 to 18 minutes.** A scripted player finishes in about a minute. Nobody has played it; the content (one room, twelve
  pieces) and the numbers are not tuned.
- **Two implementations of the rules.** The C++ rules are the tested ones; the offline Luau game has not been kept in step.
- **The client is a vertical slice.** It is not finished: mouse look and the van trip are untested through it, and it was never run on D3D12.
- **Art is entirely placeholder.** Blockouts from `tools/build_lookdev.py`; no production models or textures exist and no generation credits were spent.
- **Vendored engine code** in `server/` is a modified copy and must be resynced on an engine upgrade.
- **What I could not judge:** how any of it feels. The first useful test is a person on Windows with a GPU.

## Where the evidence is

`docs/M0_RESULTS.md` has one section per feature: the result, the mutation checks that prove each test can fail, and the bugs found
while building it (the movement time-budget hole, the battery that refilled itself, clients inferring "cleaned" from missing entities,
the pawn spawn rule). `docs/images/` has the renders.
