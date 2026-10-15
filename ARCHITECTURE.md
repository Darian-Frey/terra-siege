# Architecture

How terra-siege is structured as of 2026-10-15. This document describes the system as it is. The reasons it ended up this way are in [DECISIONS.md](DECISIONS.md), and failure modes are in [ATTACK_VECTORS.md](ATTACK_VECTORS.md).

## System overview

One CMake build produces two executables and three test binaries around a shared static library:

```text
                 ┌──────────────────────────────────────────────┐
                 │ terra_siege_mesh (static lib)                │
                 │  ObjLoader · MeshRegistry · Palette          │
                 │  SidecarProfile · EntityProfileRegistry      │
                 │  deps: raylib 5.0, picojson 1.3.0            │
                 └───────┬──────────────────┬──────────────┬────┘
                         │                  │              │
          ┌──────────────▼───┐   ┌──────────▼────────┐  ┌──▼──────────────┐
          │ terra-siege      │   │ terra-siege-inspect│  │ tests (doctest) │
          │ (the game)       │   │ (OBJ + sidecar     │  │ test_obj_loader │
          │ RAYMATH_IMPL     │   │  editor)           │  │ test_obj_save   │
          │ DEV_MODE option  │   │ RAYMATH_IMPL       │  │ test_sidecar_…  │
          └──────────────────┘   └────────────────────┘  └─────────────────┘
                 ▲    reads at startup        ▲ reads + writes
                 └─────── assets/meshes/*.obj + *.meta.json ──┘
```

External dependencies all come in through CMake FetchContent (shallow): raylib 5.0, doctest v2.4.11 and picojson v1.3.0. A post-build step copies `assets/` next to the game binary. The game is also built with `TERRA_SIEGE_PROJECT_ROOT`, which DEV_MODE uses to write logs to `tests/logs/` and to resolve `assets/meshes`.

### Game frame

```text
main.cpp ── Clock accumulator (FIXED_DT = 1/120 s, capped at MAX_FRAME_TIME)
   │
   ├─ while accumulator ≥ FIXED_DT:  GameState::update(FIXED_DT)
   │     1. Player::update            input → flight assist → Newtonian physics → shields
   │     2. ParticleSystem::update
   │     3. weapon firing             spawnProjectile / beamRaycast on EntityManager
   │     4. EntityManager::update     AI, projectiles, collision, damage, infection
   │     5. Radar::update             contacts, pulse painting, ghosts, missile warnings
   │     6. WaveManager::update       (Wave mode only)
   │     7. camera update             active CameraView, terrain-clamped
   │     8. win/loss latch            SessionResult
   │
   └─ GameState::render(alpha)
         BeginMode3D + rlFrustum far plane 3000
         Planet::draw(cam) → Decorations::render → player shadow + Player::render
         → EntityManager::render(cam, planet) → shield caps
         EndMode3D → drawHUD (flight HUD, weapon/shield blocks, Radar::draw)
```

## Module responsibilities

| Module | Responsibility |
| --- | --- |
| `src/main.cpp` | Window setup, `DisableCursor()`, fixed-timestep loop. |
| `core/Clock.hpp` | Fixed-timestep accumulator. |
| `core/Config.hpp` | Every tuning constant as `constexpr`: the de facto technical spec ([D-027](DECISIONS.md#d-027-confighpp-as-the-single-source-of-tuning-truth-transitional)). |
| `core/GameState` | Top-level app state machine (`AppState`, `GameMode` Wave/Base, `SessionResult`, `CameraView`, `CamMode`). Owns every subsystem (below). Runs the five cameras, menus, loadout, settings, friendly placement, weapon fire dispatch, beam handling, all HUD drawing and the session-result overlay. About 3,100 lines. |
| `core/Particles` | `ParticleSystem`: 2000-slot pool, cube and billboard particles, gravity and bounce flags, xorshift32 RNG. |
| `core/Settings` | Persistent settings in `~/.config/terra-siege/settings.cfg`. |
| `world/Heightmap` | Tileable Perlin fBM, ridged mountains, 2D domain warp, ocean classification, lake flood fill, `WaterType`. Toroidal `sample()`. PNG and stats export. River functions present but disabled. |
| `world/Planet` | Owns the heightmap and 16×16 `TerrainChunk`s. `generate(seed, ProgressCb)`. Wrapped `heightAt()`. Draws each chunk at the copy nearest the camera. `nearestCopy()`, `isGrassland()`. World size is 1024 × `TERRAIN_SCALE` (about 8192 units). |
| `world/TerrainChunk` | Flat-shaded chunk mesh with per-face colour from the `BIOME_*` bands and water types. |
| `world/Decorations` | Visual-only trees, rocks, antennas and crash sites. Noise-mask and biome-gated placement. Distance-culled at 800 and drawn at the nearest toroidal copy. |
| `entity/Entity.hpp` | One type-tagged `Entity` struct (`EntityType`, `DamageType`, `ProjectileOwner`, AI/FX/infection fields, `homeBaseId`). `isFriendlyStructure()` is the single allegiance list for friendly structures. |
| `entity/Player` | Hovercraft input, Newtonian physics, flight assist, 4-sector shields, thrust charge, weapon selection state, render. About 1,170 lines. |
| `entity/EntityManager` | Fixed pools (256 entities, 512 projectiles). All enemy and friendly AI (including Base-mode defenders, landing pads and radar stations), projectile and missile guidance, collision, damage routing (`applyDamage`, `applyShieldHit`), infection, sidecar lookups at spawn, wrap-aware render. About 3,400 lines. |
| `hud/Radar` | Pulse radar: IFF blip shapes, altitude strip, per-slot echo state, ghost-blip pool (32), missile warnings, Carrier jamming. |
| `shield/ShieldRenderer` | `shieldfx` impact-cap visuals for the player and shielded entities. |
| `audio/AudioManager` | Procedural SFX synthesis at startup, alias pools, `play3D`/`play2D` with manual pan and falloff. |
| `wave/WaveManager`, `WaveDef.hpp` | Wave state machine and spawn placement (Wave mode only). |
| `mesh/*` | Shared library: OBJ load (triangulate, recompute face normals) and three save paths. `MeshRegistry` keyed by `EntityType`, with procedural fallback. 32-colour palette. Sidecar load/save (DOM-preserving). `EntityProfileRegistry`. |
| `inspector/*` | `Inspector` owns the mesh, model, orbit camera, shared undo stack (32 snapshots) and file ops. `MenuBar`, `InspectorConfig` (`~/.config/terra-siege/inspector.cfg`), `InspectorFont`. `Tool` interface with 12 tools: Vertex, Primitives, Materials, Topology, Profile, Identity, Hull, Shields, Weapons, Hardpoints, AI, FX. |

**Empty stub files.** These are compiled but contain no code ([BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist), [IMP-014](IMPROVEMENTS.md#imp-014-delete-or-populate-the-empty-stub-modules)):

- `ai/AIController`, `ai/EnemyAI`, `ai/SpatialGrid`
- all of `weapon/`
- `renderer/SceneRenderer`, `renderer/PostFX`, `renderer/ParticleSystem`
- `hud/HUD`, `hud/WeaponDisplay`, `hud/ShieldDisplay`
- `shield/ShieldSystem`, `world/SkyDome`
- `entity/Enemy`, `entity/Friendly`, `entity/Projectile`

Their responsibilities live in `GameState` and `EntityManager`.

### Ownership

`GameState` owns, by value:

- `Planet`, `Decorations`, `Player`, `ParticleSystem`, `EntityManager`
- `Radar`, `WaveManager`, `AudioManager`, `Camera3D`
- `tsmesh::MeshRegistry` and `tsmesh::EntityProfileRegistry`

`init()` loads both registries after `InitWindow` (a GL context is needed to upload models) and hands the profile registry to `EntityManager`. `loadWorld(seed)` generates the planet, then places decorations and the friendly roster for the current `GameMode`.

## Key invariants

- **Fixed timestep.** Physics steps only at `FIXED_DT`. `update()` makes no draw calls and `render()` mutates no state ([D-003](DECISIONS.md#d-003-fixed-120-hz-physics-timestep), [AV-012](ATTACK_VECTORS.md#av-012-frame-rate-dependent-logic-in-update)).
- **No hot-path allocation.** All pools are fixed at startup ([D-012](DECISIONS.md#d-012-flat-type-tagged-entity-pools-no-ecs-no-hot-path-allocation), [AV-006](ATTACK_VECTORS.md#av-006-heap-allocation-in-the-hot-path)).
- **Toroidal wrap everywhere.** Player coordinates are unbounded. Height queries wrap internally, and world-fixed objects draw at `Planet::nearestCopy()` ([D-007](DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world), [AV-005](ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap)). Gameplay distances are not yet wrap-aware ([BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap)).
- **Coordinate system.** +X east, +Y up, +Z north (forward). Yaw 0 faces +Z and increases clockwise. The world origin is the heightmap corner.
- **Camera.** Every view clamps above the terrain ([AV-004](ATTACK_VECTORS.md#av-004-camera-clips-below-terrain)). Tactical uses `up = {0, 0, 1}`. The far plane is overridden to 3000 ([AV-013](ATTACK_VECTORS.md#av-013-raylib-name-and-default-collisions)).
- **Allegiance.** Friendly structures come from `isFriendlyStructure()`. Allied ships are those in `AIState::Infected`/`Infecting` (infected ships and base defenders). Player shots skip friendlies. Enemy shots hit friendlies, allied ships and the player.
- **Data sources.** Tuning constants live in `Config.hpp`. Per-entity stats come from sidecars, falling back to Config ([D-011](DECISIONS.md#d-011-sidecar-entity-profiles-by-default-registry-override-later)).
- **Meshes.** OBJ is the mesh source of truth. Normals are recomputed at load and colour comes from palette material names ([D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth), [D-010](DECISIONS.md#d-010-32-colour-palette-via-obj-material-names)).

## Cross-cutting concerns

- **Concurrency.** Single-threaded. raylib owns the main thread, and audio runs on miniaudio's own thread inside raylib.
- **Error handling.** No exceptions in hot paths. Asset problems degrade gracefully:
  - a missing OBJ falls back to procedural geometry
  - a missing or malformed sidecar falls back to Config defaults with a warning
  - a bad material name renders magenta
- **Logging.** raylib `TraceLog`: `LOG_INFO` in DEV_MODE builds, `LOG_WARNING` in release.
- **Determinism.** Terrain is fully determined by its seed. Wave spawns use a separate local xorshift RNG, so reseeding the terrain doesn't change spawn patterns. Friendly placement is deterministic per seed.
- **DEV_MODE.** Every developer feature is behind `#ifdef DEV_MODE`, with no release overhead. See [CLAUDE.md](CLAUDE.md) for the hotkeys.
- **Persistence.** `~/.config/terra-siege/settings.cfg` (game) and `inspector.cfg` (recent files). Meshes and sidecars are edited in place under `assets/meshes/`.
