# Ghost Shift

A 1–4 player online co-op comedy-horror cleaning game for Windows, built on the
[Aether engine](https://github.com/rithymeth/game-ngine). Players run an underqualified paranormal
cleaning company in the haunted city of Grimborough. See `docs/PRODUCTION_PLAN.md`.

## Status: lookdev stage (pre-M0)

What exists, verified:
- Project created with the engine's `aether_new --template first_person`.
- A moonlit Willow House room lookdev scene (`Content/Scenes/Main.ascene`): window wall, table, lamps,
  a glowing Prankster ghost, spectral vacuum, ectoplasm and cursed stains, one directional "moon" light
  and six coloured point lights.
- Cooked successfully with `aether_cook` on Linux (16 assets, 10 meshes, emissive materials parsed).

What does **not** exist yet:
- All geometry is a **procedural blockout** (`tools/build_lookdev.py`), not final art. No generated or
  production models yet; no game logic beyond the template's first-person controller.
- The render has **not been viewed**. The build container has no GPU; sun shadows are D3D12-only.
  Judge the look by running the packaged player on Windows.

## Build and run (Windows, with the engine checked out next to this repo)

```
aether_package GHOST-SHIFT.aproject --out Build
Build/GHOST-SHIFT
```

## Regenerate the lookdev content

```
python tools/build_lookdev.py Content/Models
python tools/build_lookdev_scene.py .
```
