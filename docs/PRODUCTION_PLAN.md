# Ghost Shift on Aether — Production Plan (draft 1)

Source: GDD v1.3 (3D first-person 1–4 player co-op comedy-horror cleaning game, Windows first, stylized/colorful art).

## 0. Findings that shape the plan
- The GDD's concept images are schematic, not in-engine art. The art target is a stylized, saturated, glowing-neon look (see concept sheet): 4 characters, 6 ghosts, 7 tools, 5 maps.
- The engine has the pieces (renderer with clusters/shadows/decals/PBR materials, net replication+RPC, physics, nav+AI, audio, UI, save, loc, gameplay tags/abilities, interaction, inventory, quests). The only game target proven is Windows x64 / D3D12; this Linux container can build/test but not certify the render.
- Engine asset reality: `assets/models` holds only test glTF files, and the proven import path is **static glTF + base color**. Skinned/animated glTF has test fixtures only. Character animation, PBR texture sets and emissive maps are therefore **risks to verify first**, not assumptions.
- The "game-development" skill is built around 2D PixelLab pixel art. This game is 3D, so PixelLab does not apply. The connected 3D generator is **Meshy** (api.meshy.ai, reachable; balance reads 14 credits). No generation has been run.

## 1. Quality bar (what "best quality" means, measurable)
| Item | Target |
|---|---|
| Characters | ~15–25k tris, 2k PBR (base/normal/ORM/emissive), rigged, shared humanoid skeleton |
| Ghosts | ~8–15k tris, emissive + fresnel/translucent shader, 6 rigs |
| Tools | 5–10k tris each, emissive glow parts, first-person + world LODs |
| Environments | modular kit pieces (walls, floors, windows, stairs) + 25–35 prop families, baked/dynamic lighting, decals for stains |
| Render | glTF with PBR + emissive, dynamic lights, bloom, shadows, 60 FPS target (measure, don't assume) |
| Validation | each asset imported + cooked + rendered in-engine, screenshot compared to concept sheet |

## 1b. Render-first pipeline (why render comes before content)
1. **R0 Render spike (no paid assets):** extend/clone `pbr_demo`/`gltf_demo` into a Ghost Shift lookdev scene: Willow House blockout with neon-ghost shader, emissive, bloom, fog, decals. Uses procedural/primitive geometry. Proves the *look* and what glTF features (emissive, skins, KHR_materials) the importer really supports.
2. **R1 Asset import audit:** import a free/CC0 or one Meshy sample (after approval) to verify skinning, animation, texture sets, LOD handling, cook, and packaged render.
3. Only after R0/R1 pass: bulk asset generation.

## 2. Asset production (Meshy; all paid steps need your approval)
Tiering — generate in this order, one sample per category for art-review before bulk:
1. **Hero sample #1:** Spectral vacuum (tool) — simplest, defines the style. 
2. Prankster ghost (ghost style sample), Maya Sparks (character sample).
3. Willow House kit + props (modular kit first: wall, floor, door, window, stairs, 10 props).
4. Remaining 3 characters (shared skeleton), 5 ghosts, 6 tools.
5. Maps 2–5 kits (post vertical slice).
Pipeline per asset: concept → Meshy image-to-3D (concept sheet panel as reference) → retopo/remesh to budget → texture → auto-rig (humanoids) → export glTF/GLB → validate/import → in-engine screenshot → record in `.game-dev/progress.json`.
Credit gate: cost unknown until quoted; current 14 credits likely covers little or nothing. Need your top-up decision + a cap.

## 3. Game-code milestones (from GDD §11, mapped to engine)
- **M0 Engine spike (2–3 wk):** two networked clients in one room with synced objects (net + physics_net). Decide relay/NAT approach.
- **M1 First playable — Willow House (4–6 wk):** first-person controller, spectral vacuum + contamination, Prankster ghost AI (behavior tree + nav), trap, objectives, results/pay once, save, 1–4 player.
- **M2 Vertical slice:** HQ lobby, progression, second map, UI screens, polish.
- **M3 Content / M4 Alpha-beta / M5 Release** per GDD.
New game lives in `games/GHOST-SHIFT` (new sibling, follows AETHER-01 layout: project file, Content, ASSET_CATALOG.json). Respect: `net` must not include `gameplay`; register components in `mcp/modules.cpp`; run full `ctest` (42 checks).

## 4. Immediate next steps (no spend)
1. Scaffold `games/GHOST-SHIFT` from the AETHER-01 template + CMake option.
2. Build R0 lookdev scene and screenshot it (Linux headless Vulkan if available).
3. Run R1 import audit with free assets.
4. Create `.game-dev/` ledger (asset requirements, progress) with these grounded requirements.

## 5. Open decisions for you
1. Approve Meshy usage? Cap in credits and top-up (balance 14)?
2. Start with R0 render spike + scaffold now (free)?
3. Scope target: M1 Willow House first playable (recommended) vs whole game.
4. Windows-only validation: you will need to run Windows builds for D3D12 render proof.
