# CLAUDE.md

Handoff for AI-assisted sessions on terra-siege. This file describes **current state**, not history: history is in [CHANGELOG.md](CHANGELOG.md) and `git log`, and rationale is in [DECISIONS.md](DECISIONS.md). Refresh it at the end of every significant session.

The project follows the development documentation standard (`~/Projects/project-scaffold/development_documentation.md`; [D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). In particular:

- **Log when found, not silently acted on.** Bugs go in [BUGS.md](BUGS.md) and refactor ideas in [IMPROVEMENTS.md](IMPROVEMENTS.md), with the next free `BUG-`/`IMP-` ID. Fix them only when asked.
- **Docs are part of the commit.** If a change invalidates FEATURES, ROADMAP, ARCHITECTURE or this file, update them in the same commit.
- **IDs are append-only.** F-, D-, AV-, BUG- and IMP- entries are never deleted or renumbered. Change their status instead.

## Project

A C++17 / raylib 5.0 remake of *Virus* (Argonaut, 1988) for Linux: Newtonian hovercraft planetary defence over a flat-shaded, toroidal procedural world. Solo developer, with Claude Code as the main development partner. Engine Phases 1–3.5 are complete. Phase 4 is nearly done. The current feature work is **Slice C, Base Defence mode**.

## Current state

- `src/core/GameState.*`: working. App state machine, Wave/Base modes, five cameras (ThreatLock stubbed, [BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub)), menus, loadout, HUD drawing, friendly placement, win/loss. About 3,100 lines.
- `src/core/Config.hpp`: working. Every constant. Difficulty preset tables exist but are unused (F-027).
- `src/entity/Player.*`: working. Flight, assist, shields, weapon selection. Has [BUG-005](BUGS.md#bug-005-holding-q-or-e-to-select-a-laser-colour-suppresses-auto-level-roll).
- `src/entity/EntityManager.*`: working. Pools, all AI, projectiles, damage, infection v1, Lander (first cut), C.2 base defenders, landing pads and radar stations (procedural render; no OBJ yet). `rebaseAround()` re-centres entities and projectiles on the player each tick, so the world wrap is handled ([BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap)).
- `src/world/*`: working. Perlin terrain, lakes, decorations, wrap-aware rendering. Rivers disabled (F-003). `SkyDome` is empty.
- `src/hud/Radar.*`: working pulse radar. Range semantics are open ([BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range), [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally)).
- `src/audio/AudioManager.*`: working. No engine loop yet.
- `src/mesh/*`: working. OBJ, palette, sidecars and registries are all tested.
- `src/inspector/*`: working. Phases A, B (first cut), C, D, E and F.1–F.5. F.6 and G not started.
- **Empty stub files:** `src/ai/*`, `src/weapon/*`, `src/renderer/*`, `hud/{HUD,WeaponDisplay,ShieldDisplay}`, `shield/ShieldSystem`, `entity/{Enemy,Friendly,Projectile}`. They contain no code. Don't assume a spatial grid or AI module exists ([BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist)).
- `tests/`: three doctest binaries covering only the mesh and sidecar layer. No CI.
- Docs: all migrated to the standard on 2026-10-15.

## Active task

**Slice C, sub-slice C.4: radar network (F-023).** Design: [project-status/base_mode_v2.md](project-status/base_mode_v2.md), §"Radar network". C.2 (bases, defenders, pads, radar stations) landed on 2026-10-15.

The player's personal radar stays at 300 m ([D-031](DECISIONS.md#d-031-players-personal-radar-stays-at-300-m-in-base-defence-mode)). The network adds coverage beyond it.

**Acceptance:**

- Bases and radar stations within `NETWORK_LINK_RANGE` form clusters that share detection
- The player's personal radar always works at short range; network-only contacts are drawn differently
- Areas outside all coverage are fogged on the radar disc
- Resolves [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range) and [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally) for Base mode
- FEATURES F-023 and ROADMAP Slice C updated in the same commit

## Architectural invariants

Full rationale is in [DECISIONS.md](DECISIONS.md) and [ARCHITECTURE.md](ARCHITECTURE.md). Failure modes are in [ATTACK_VECTORS.md](ATTACK_VECTORS.md).

- **Fixed 120 Hz timestep** ([D-003](DECISIONS.md#d-003-fixed-120-hz-physics-timestep)).
  - No frame-rate-dependent logic in `update()`.
  - No raylib draw calls in `update()`, and no state mutation in `render()`.
  - Edge-triggered input uses the rising-edge helper, not `IsKeyPressed`.
- **No heap allocation in the hot path** ([D-012](DECISIONS.md#d-012-flat-type-tagged-entity-pools-no-ecs-no-hot-path-allocation)). Entities live in fixed type-tagged pools, not an ECS. New per-entity state should reuse existing `Entity` fields where it can.
- **Toroidal world, unwrapped player** ([D-007](DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world)). Height queries wrap internally. Draw world-fixed objects at `planet.nearestCopy(pos, camera.position)`, and never teleport-wrap the player.
- **`Config.hpp` is the single source of tuning constants** ([D-027](DECISIONS.md#d-027-confighpp-as-the-single-source-of-tuning-truth-transitional)).
  - No magic numbers in logic code. Mark playtest values `// TUNE`.
  - Per-entity stats come from `assets/meshes/*.meta.json` sidecars, with Config as the fallback ([D-011](DECISIONS.md#d-011-sidecar-entity-profiles-by-default-registry-override-later)).
  - **Docs link to Config; they never copy its values.**
- **TTK-derived HP** ([D-014](DECISIONS.md#d-014-ttk-derived-enemy-hp)). Rebalance by changing TTK, not HP. `CANNON_DPS` = 100 stays as the DPS reference even though the Cannon is gone.
- **OBJ meshes with palette materials** ([D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth), [D-010](DECISIONS.md#d-010-32-colour-palette-via-obj-material-names)).
  - Materials are `c00`..`c31`, and face normals are recomputed at load.
  - Use `DrawModelEx` for yaw-rotated entities.
  - Procedural geometry is only for terrain, particles, shields and state-driven overlays.
- **Flat-shaded, low-poly** ([D-013](DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures)). No textures, specular or per-face gradients. One sun, ambient about 0.45, fog matching the sky. "Could have run in 1992 on very powerful hardware."
- **Coordinate system.** +X east, +Y up, +Z north (forward). Yaw 0 faces +Z and increases clockwise. The world origin is the heightmap corner.
- **Single Newtonian flight model** ([D-005](DECISIONS.md#d-005-single-newtonian-flight-model-one-craft)). Don't re-add `FlightMode` or `CraftType`.

### Naming

| Name | Use |
| --- | --- |
| `CameraMode` | **Never define it**: a raylib typedef |
| `CameraView` | The five player views: Chase / Velocity / Tactical / ThreatLock / Classic |
| `CamMode` | DEV-only Follow / FreeRoam toggle (F1) |
| `GameMode` | Wave / Base |
| `SessionResult` | Victory / PlayerDestroyed / FriendliesLost |

## Build & test commands

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDEV_MODE=ON   # normal dev configure
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure                     # mesh + sidecar tests
./build/terra-siege                                       # game (run from repo root)
./build/terra-siege-inspect assets/meshes/fighter.obj     # inspector
```

The game binary has the repo path built in (`TERRA_SIEGE_PROJECT_ROOT`) for assets and DEV logs. If the repo moves, delete `build/` and reconfigure.

**DEV_MODE hotkeys** (`-DDEV_MODE=ON`, all behind `#ifdef DEV_MODE`):

| Key | Action |
| --- | --- |
| F1 | Toggle Follow / FreeRoam camera (`CamMode`) |
| F2 | Cycle flight assist level 0–3 |
| F3 | God mode (infinite thrust, invincible, infinite weapons) |
| F4 | Flight recorder, 120 Hz CSV to `tests/logs/` |
| F5 | Reseed terrain |
| F6 | Dump heightmap to `tests/logs/heightmap-<unixtime>.{png,txt}` |
| F7 | Skip wave |
| `[` / `]` / `\` | Debug stepper: in / out / reset |

## Conventions

- PascalCase classes, camelCase methods, `m_` member prefix, ALL_CAPS Config constants.
- `#pragma once` on every header. `std::filesystem::path` for paths. No exceptions in hot paths; use return codes or `std::optional`.
- Comments explain *why*, not *what*. Default to none. Match the density of the surrounding code.
- Commit messages: `type(scope): summary`. Scopes seen so far: `slice-C.3`, `inspector-F.2`, `world`, `radar`, `weapons`. Reference F-/D-/BUG-/IMP- IDs in the body where relevant. Commit or push only when asked.
- New FEATURES, DECISIONS, BUGS, IMPROVEMENTS and ATTACK_VECTORS entries use the next free ID and the format already in each file.

## Pitfalls

- See [ATTACK_VECTORS.md](ATTACK_VECTORS.md) for the canonical list. The ones that bite most often:
  - **Wrap** ([AV-005](ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap)): anything new that draws or measures world-fixed objects must use the nearest toroidal copy.
  - **Camera terrain clamp** ([AV-004](ATTACK_VECTORS.md#av-004-camera-clips-below-terrain)): every view queries `heightAt` before lerping. Don't remove it.
  - **raylib traps** ([AV-013](ATTACK_VECTORS.md#av-013-raylib-name-and-default-collisions)):
    - the `rlFrustum` far-plane override at 3000
    - Tactical view `camera.up = {0, 0, 1}`
    - `RAYMATH_IMPLEMENTATION` only on executables
    - `MatrixMultiply(A, B)` computes `B·A`
  - **Beam vs Carrier** ([AV-008](ATTACK_VECTORS.md#av-008-carrier-shields-regenerate-under-sustained-beam-fire)): the Beam Laser resets `timeSinceHit` every tick on target. That is the Carrier fight mechanic.
  - **OBJ save paths** ([AV-009](ATTACK_VECTORS.md#av-009-obj-save-corrupts-authored-content)): `saveObjVertices` is byte-exact (T-06/T-07); `saveObjMaterials` / `saveObjMesh` are untested.
- `Planet::generate()` takes a `ProgressCb` for the loading screen. Keep it on every regeneration path ([AV-014](ATTACK_VECTORS.md#av-014-loading-progress-callback-lost-on-terrain-regeneration)).
- The 120° cluster-submunition cone in old docs **is not implemented** ([BUG-016](BUGS.md#bug-016-cluster-submunition-120-search-cone-documented-but-not-implemented)). Don't "preserve" code that isn't there.
- `project-status/archive/` holds superseded specs. `terrain_rebuild.md` (sine synthesis) is obsolete and must not be implemented ([D-006](DECISIONS.md#d-006-tileable-perlin-gradient-fbm-terrain)).
- `gen_sfx.sh` (rFXGen pipeline) is untracked and `tools/` is git-ignored. Neither is part of the build.

## Out of scope

Things not to change without asking:

- Accepted decisions in DECISIONS.md. Only revisit one if its reversal conditions are met; otherwise ask.
- The fixed timestep, pool sizes, the wrap model and the flight model.
- `assets/meshes/*.obj` and `*.meta.json`. These are authored content; edit them through the inspector or on request.
- Balance values marked `// TUNE`, and TTK targets.
- `project-status/archive/`, which is historical. Only correct factual banners there.
- Adding dependencies beyond raylib, doctest and picojson.
- Fixing logged BUG-/IMP- entries opportunistically while working on something else.
