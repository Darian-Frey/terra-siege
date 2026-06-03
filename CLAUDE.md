# CLAUDE.md — terra-siege Project Context

This is the primary AI-assistant context document. Read it fully before making changes. For user-facing documentation see [README.md](README.md); for plan-of-work see [ROADMAP.md](ROADMAP.md). Detailed design docs in [project-status/](project-status/) and [project-status/archive/](project-status/archive/) capture rationale; this file covers architecture rules, invariants, and gotchas.

---

## What Is terra-siege?

A modern C++17 / raylib reimagining of **Virus** (Argonaut Software / Firebird, 1988) for Linux (Windows later). Defend a planet against alien attackers from the cockpit of a hovercraft. Preserves the original's flat-shaded low-poly polygon aesthetic; modernises with directional shields, weapon upgrades, auto-turret, homing missiles, tactical radar, OBJ-based authoring pipeline.

**GitHub:** <https://github.com/Darian-Frey/terra-siege>
**Developer:** Solo, Linux Mint, Antigravity IDE + Claude Code
**Origin:** Jez San, Argonaut Software — same techniques later led to the Super FX chip and Star Fox (SNES)

---

## Technology Stack

| Component | Choice | Reason |
|-----------|--------|--------|
| Language | C++17 | `std::variant`, `std::optional`, structured bindings, filesystem |
| Renderer / window / audio | raylib 5.0 (FetchContent) | Native 3D, flat-shading friendly, miniaudio backend |
| Math | raymath.h (bundled with raylib) | `Vector3`, `Matrix`, `Quaternion` ops |
| Build | CMake 3.16+ | Single `CMakeLists.txt`; no submodules |
| Test harness | doctest (FetchContent, header-only) | ~10× faster compile than Catch2; chosen for mesh subsystem tests |
| RNG | xorshift32 (hand-rolled) | Deterministic, seedable terrain generation |

**Build commands:**

```bash
# Standard debug
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/terra-siege               # game
./build/terra-siege-inspect <obj> # mesh inspector

# Dev mode — F-key dev hotkeys + debug overlays
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDEV_MODE=ON
cmake --build build -j$(nproc)
./build/terra-siege

# Tests
cd build && ctest --output-on-failure
```

A single build produces two executables (`terra-siege` and `terra-siege-inspect`) linking a shared `terra_siege_mesh` static lib (`ObjLoader`, `MeshRegistry`, `Palette`). See [README.md — Building](README.md#building).

---

## Critical Naming Collisions

| Name | Problem / Status | Correct Usage |
|------|------------------|--------------|
| `CameraMode` | raylib typedef — compile error if redeclared | Never define a type with this name |
| `CamMode` | Internal enum for Follow / FreeRoam dev camera toggle | Use only for that |
| `CameraView` | Five player views (Chase / Velocity / Tactical / ThreatLock / Classic) | Use for view switching |
| `FlightMode` | **Removed** in the flight rebuild (single Newtonian model) | Do not re-add |
| `CraftType` | **Removed** in the flight rebuild (one hovercraft mesh) | Do not re-add |
| `smooth()` (Heightmap) | Pending removal — sine-wave terrain rebuild is the only rebuild spec not yet shipped | Live in current code; do not assume removed |
| `applyRadialFalloff()` (Heightmap) | Same as `smooth()` — planned removal pending sine-wave rebuild | Live in current code |

---

## Project Structure

```text
terra-siege/
├── CMakeLists.txt                # Game + inspector + tests + shared mesh static lib
├── CLAUDE.md                     # This file
├── README.md                     # User-facing docs (build, controls, current status)
├── ROADMAP.md                    # Three-track plan (engine + features + tooling)
├── base_mode_v2.md               # Active Slice C design — asymmetric Base Mode
├── terra_siege_inspect_roadmap.md# Inspector roadmap (phases A–G, F.1–F.6)
├── project-status/               # Active reference + historical archive
│   ├── game_modes_and_features.md# v1 design — §§4-9 still apply (Part 2 superseded)
│   └── archive/                  # Superseded docs (see archive/README.md)
├── assets/
│   ├── meshes/                   # Entity OBJ files + 32-colour palette
│   └── shaders/                  # GLSL stubs — not yet wired in
├── tests/                        # doctest-based tests (mesh subsystem)
└── src/
    ├── main.cpp                  # Fixed-timestep loop, DisableCursor()
    ├── core/
    │   ├── Clock.hpp             # Fixed-timestep accumulator (120 Hz)
    │   ├── Config.hpp            # ALL tuning constants — single source of truth
    │   ├── GameState.hpp/cpp     # State machine + menu overlays + CamMode enum
    │   ├── Particles.hpp/cpp     # 2000-slot pool, gravity + bounce flags
    │   └── Settings.hpp/cpp      # Persistent settings (~/.config/terra-siege)
    ├── world/
    │   ├── Heightmap.hpp/cpp     # Diamond-Square + smoothing + rivers + lakes
    │   ├── TerrainChunk.hpp/cpp  # Flat-shaded mesh builder, WaterType colouring
    │   ├── Planet.hpp/cpp        # Chunk orchestration, heightAt() query
    │   └── SkyDome.hpp/cpp       # Stub
    ├── entity/
    │   ├── Entity.hpp            # Type-tagged struct (single pool layout)
    │   ├── Player.hpp/cpp        # Hovercraft Newtonian physics, input, assist
    │   ├── Enemy.hpp/cpp         # Drone / Fighter / Seeder / Bomber / Carrier / Tank / Turret
    │   ├── Friendly.hpp/cpp      # Collector / Repair Station / Radar Booster / Base
    │   ├── Projectile.hpp/cpp    # Cannon / Plasma / Beam / Missile / Cluster
    │   └── EntityManager.hpp/cpp # Flat pools + spatial grid + AI dispatch
    ├── mesh/                     # Shared (game + inspector + tests) → terra_siege_mesh
    │   ├── ObjLoader.hpp/cpp     # Text-format OBJ load + round-trip-safe save
    │   ├── MeshRegistry.hpp/cpp  # Startup-loaded Models keyed by EntityType
    │   └── Palette.hpp           # 32-colour palette + material-name lookup
    ├── inspector/                # terra-siege-inspect binary
    │   ├── main.cpp              # CLI parsing + window setup
    │   ├── Inspector.hpp/cpp     # Mesh/camera/model owner + orbit camera + tool registry
    │   ├── Tool.hpp              # Pluggable tool interface (TAB cycles)
    │   └── VertexTool.hpp/cpp    # Vertex pick + drag + axis lock (first tool)
    ├── weapon/                   # Plasma, Beam, Missiles, Auto Turret, EMP, Shield Booster
    ├── shield/                   # Player directional shields (Phase 4)
    ├── ai/                       # AIController, EnemyAI, SpatialGrid
    ├── renderer/                 # SceneRenderer, ParticleSystem, PostFX (PostFX stub)
    ├── hud/                      # HUD, Radar (tier 1/2/3), WeaponDisplay, ShieldDisplay
    ├── audio/                    # AudioManager (Phase 5 — positional audio)
    └── wave/                     # WaveManager, WaveDef
```

---

## Architecture Rules — Do Not Change Without Good Reason

### Fixed-Timestep Loop (120 Hz)

Physics runs at exactly 120 Hz decoupled from render rate. Lives in `main.cpp`. Never put frame-rate-dependent logic in `update()`.

```text
accumulator += frameTime (capped at MAX_FRAME_TIME = 0.05s)
while accumulator >= FIXED_DT (1/120s):
    game.update(FIXED_DT)
    accumulator -= FIXED_DT
render(alpha = accumulator / FIXED_DT)
```

### Config.hpp — Single Source of Truth (transitional)

Every gameplay constant lives in `Config.hpp` as `constexpr`. No magic numbers in logic code.

**Transitional note:** as the entity-profile sidecar system lands ([inspector roadmap Phase F](terra_siege_inspect_roadmap.md#phase-f--terra-siege-entity-sidecar)), per-entity-type constants (`FIGHTER_HP`, `BOMBER_SPEED`, weapon stats, AI thresholds, etc.) will migrate out of `Config.hpp` into per-mesh `*.meta.json` sidecars. Physics, world-scale, and engine-config constants stay in `Config.hpp`. See the inspector roadmap for migration ordering.

### Entity System — Flat Pools, Not ECS

The game has < 500 live entities. A full ECS is unnecessary overhead. Use `std::vector<Entity>` flat pools per type in `EntityManager`. Do not introduce a heavy ECS framework.

### No Heap Allocation in Hot Path

Entity pools, particle pools (2000 slots), projectile pools, and the radar ghost-blip pool (32 slots) are all pre-allocated at startup. No `new` / `malloc` inside `update()` or `render()`.

### Separation of Update and Render

Nothing in the physics tick (`update`) touches raylib draw calls. Nothing in `render()` mutates game state.

### OBJ Mesh Pipeline

Entity meshes live in `assets/meshes/*.obj` and load at startup into a `MeshRegistry` keyed by `EntityType`. Materials use a 32-colour palette via material names (`c00`..`c31`) — files stay text-editable in Blender and round-trip cleanly through `terra-siege-inspect`. Procedural geometry is still used for terrain, particles, dynamic shield panels, and any per-frame procedural overlays.

`DrawModelEx` (not `rlPushMatrix`) is used for yaw-rotated entity rendering. Damage flash (tinting `WHITE`) does not override per-vertex baked colours — accepted tradeoff, defer to shader-based fix later.

### Sidecar Entity Profiles (in progress)

Per-mesh `*.meta.json` files carry identity, hull, shields, weapons, hardpoints, AI profile, FX, and resources. **Sidecar by default, registry override later** — one `*.meta.json` per OBJ. Game-side reader is `EntityProfileRegistry`. Migration is incremental, gated by the inspector roadmap's Phase F.* sub-phases.

### Coordinate System

- **+Z = North (forward)**
- **+X = East (right)**
- **+Y = Up**
- Player yaw=0 faces +Z. Yaw increases clockwise (right turn).
- World origin at heightmap corner (0,0,0). World centre at `(worldSize/2, ?, worldSize/2)`.

### Camera System — Five Views

| Key | View | Behaviour |
|-----|------|-----------|
| 1 | Chase | Follows ship nose — default combat |
| 2 | Velocity | Follows velocity vector — terrain avoidance |
| 3 | Tactical | Fixed overhead, north up — battlefield awareness. **`camera.up = {0, 0, 1}` — NOT `{0, 1, 0}`** |
| 4 | Threat-lock | Rotates to keep nearest enemy in frame |
| 5 | Classic | Fixed diagonal-down — original Virus feel |

`CameraView` enum is distinct from `CamMode` (Follow / FreeRoam dev toggle). F1 in DEV_MODE toggles `CamMode`.

### Camera Terrain Clamping — Do Not Remove

The follow camera must always query terrain height before lerping to desired position. Without this the camera clips underground when turning toward hills:

```cpp
float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
desiredPos.y = std::max(desiredPos.y, camGroundH + 3.5f);
desiredPos.y = std::max(desiredPos.y, playerPos.y + 2.0f);
```

This applies to all five views.

### Far Clip Plane — rlFrustum Override

`GameState::render()` extends the far clip to 3000 via manual `rlFrustum` override. Do not remove — raylib's default 1000-unit far plane clips the world.

---

## Current State (Summary)

For the live phase table see [README.md — Status](README.md#status). High level:

- **Phases 1, 1.5, 2** ✅ — terrain (Diamond-Square + rivers + lakes), Newtonian flight, ship + five-view camera, ground shadow, exhaust particles, settings persistence
- **Phase 3** ✅ through **5h** — enemy roster (Drone, Fighter, Seeder, Ground Turret, Bomber, Carrier, Tank), full weapon roster (Cannon, Plasma, Beam, Missile, Cluster, Depth Charge, Auto Turret, EMP, Shield Booster), wave manager with pre-flight loadout, friendly units (Collector, Repair Station, Radar Booster, Base), bomber strafe AI, collector economy loop, ground tank + base auto-turret + friendly-fire filter
- **Phase 4 (partial)** — player directional shields + pie HUD; radar tier 1 / 2 / 3
- **OBJ pipeline + terra-siege-inspect** — full migration of entity meshes; inspector with `Tool` registry, orbit camera, vertex pick/drag
- **Phase 5 / 6** — not started

What's next is tracked in [ROADMAP.md](ROADMAP.md): three parallel tracks (Engine remaining phases / Features A→B→C slicing / Tooling Phase A → F.*).

---

## Enemy Roster (TTK-derived HP)

HP values are arithmetic from TTK (time-to-kill) targets × Cannon DPS (100 DPS reference). Change TTK, not HP, when rebalancing.

| Enemy | Hull HP | Shield HP | Shield Type | TTK (cannon) |
|-------|---------|-----------|-------------|-------------|
| Swarm Drone | 8 | 0 | None | 0.08s (1 shot) |
| Seeder | 50 | 0 | None | 0.5s |
| Fighter | 160 | 40 | Omnidirectional | 2.0s |
| Bomber | 350 | 150 | Omnidirectional | 5.0s |
| Carrier | 1500 | 250×4 | Four-sector directional | 25.0s |
| Ground Turret | 400 | 0 | None | 4.0s |
| Ground Tank | (5h roster) | 0 | None | TUNE |

AI states: `IDLE → PURSUE → ATTACK → EVADE` (Bomber adds `STRAFE_FRIENDLY`). Swarm Drones: flocking (separation / alignment / cohesion). Spatial grid cell size = `Config::SPATIAL_CELL_SIZE` (60 units), rebuilt each tick.

Friendly units: Collector, Repair Station, Radar Booster, Base. All friendly units destroyed = game over.

---

## Difficulty System

Four presets, selected at game start:

| Preset | Flight Assist | Aggression | Shield Recharge | Pickups |
|--------|---------------|------------|-----------------|---------|
| Veteran | Raw (0) | 1.3× | 0.6× | 0.5× |
| Pilot | Minimal (1) | 1.0× | 1.0× | 1.0× |
| Recruit | Standard (2) | 0.8× | 1.4× | 1.5× |
| Commander | Full (3) | 0.6× | 2.0× | 2.0× |

Flight assist coefficients: `Config::ASSIST_LEVEL_COEFFS[4] = {0.0, 0.18, 0.42, 0.75}`.

- Level 0 (Raw): No correction. Pure momentum physics.
- Level 1 (Minimal): Auto-levelling roll only.
- Level 2 (Standard): Auto-level + lateral velocity dampening.
- Level 3 (Full): Auto-level + dampen + predictive terrain look-ahead raycast.

---

## DEV_MODE Features

Compile with `-DDEV_MODE=ON`. All dev features are `#ifdef DEV_MODE` guarded and produce zero overhead in release builds.

| Key | Action |
|-----|--------|
| F1 | Toggle Follow/FreeRoam camera (`CamMode`, separate from `CameraView`) |
| F2 | Cycle flight assist level (0–3) |
| F3 | God mode — infinite thrust + invincible + infinite weapons |
| F4 | Toggle flight recorder (120 Hz CSV to `tests/logs/`) |
| F5 | Reroll terrain seed |
| F6 | Dump heightmap to `tests/logs/heightmap-<unixtime>.{png,txt}` |
| F7 | Skip wave (silently kill all enemies + flush pending spawns) |
| `[` / `]` / `\` | Step in / out / reset (debug stepper) |

Removed: F5 (craft cycle) and F6 (flight mode toggle) — gone with the flight rebuild that collapsed dual physics into single Newtonian.

---

## Key Config Constants

```cpp
// Timing
FIXED_DT             = 1/120s   // physics tick rate
MAX_FRAME_TIME       = 0.05s    // spiral-of-death guard

// World — current Diamond-Square parameters
HEIGHTMAP_SIZE       = 1025     // (sine-wave rebuild planned: see archive/terrain_rebuild.md)
CHUNK_COUNT          = 16
TERRAIN_SCALE        = 4.0f
TERRAIN_HEIGHT_MAX   = 55.0f
SEA_LEVEL            = 0.20f
RIVER_COUNT          = 6
LAKE_COUNT           = 12

// Flight (Newtonian, post-rebuild)
NEWTON_GRAVITY        = 9.8f
NEWTON_THRUST         = 24.0f
NEWTON_DRAG           = 0.02f
NEWTON_PITCH_MAX      = 1.30f
NEWTON_FUEL_MAX       = 100.0f
NEWTON_FUEL_BURN_RATE = 3.5f

// Camera
CAM_DISTANCE         = 18.0f
CAM_HEIGHT           = 8.0f
CAM_FOV              = 75.0f
CAM_LERP             = 8.0f

// Combat (TTK-derived)
TTK_FIGHTER          = 2.0s
TTK_BOMBER           = 5.0s
TTK_CARRIER          = 25.0s

// Radar
RADAR_BASE_RANGE      = 300.0f
RADAR_BOOST_RANGE     = 500.0f
RADAR_ALT_STRIP_RANGE = 150.0f
```

Values may drift — `Config.hpp` is ground truth.

---

## Coding Standards

- **No magic numbers** — add to `Config.hpp` (or sidecar once migrated)
- **No heap allocation in hot path** — pre-allocated pools only
- **No raylib draw calls in `update()`** — strict separation
- **`#pragma once`** on all headers
- **Naming** — PascalCase classes, camelCase methods, `m_` prefix for members, ALL_CAPS for Config constants
- **Comments** — explain *why*, not *what*; default to none
- **No exceptions in hot paths** — return codes or `std::optional`
- **Paths** — `std::filesystem::path`, never raw string literals
- **Never name anything `CameraMode`** — raylib already defines it

---

## Aesthetic Guidelines

Visual target: "modernised but stylistically similar" to the 1988 original.

- Flat-shaded polygons, no texture mapping
- Visible polygon faces — low-poly is a feature, not a bug
- One directional sun, ambient ~0.45, no specular
- Colour by height/type bands or 32-colour palette, no gradients within a face
- Fog matches sky colour at max draw distance
- Simple particles only — no complex shaders before Phase 5

"Looks like it could have run in 1992 on very powerful hardware — not a modern game with a retro filter applied."

---

## Critical Implementation Notes (Traps)

These have bitten development before — preserve unless you understand why:

- **`Planet::heightAt()` will need modular (wrapped) coordinates** after the planned toroidal-wrap terrain rebuild. Current code uses unwrapped; revisit when sine-wave terrain lands.
- **`rlFrustum` far-plane override at 3000** in `GameState::render()`. Do not remove — raylib's default 1000-unit far plane clips the world.
- **Loading-screen `ProgressCb`** in `Planet::generate()` must be preserved across any terrain regen — startup time is non-trivial.
- **Ghost-blip pool (Radar)** — pre-allocated at 32 slots. No heap allocation.
- **Cluster missile 120° submunition search cone** must not be widened to full-sphere — it is what makes the weapon spread across a swarm rather than all targeting one.
- **Beam Laser sets `timeSinceHit = 0` each tick while on target** — this suppresses Carrier shield regeneration. Do not omit; it's the Carrier fight mechanic.
- **Tactical view (camera 3) uses `camera.up = {0, 0, 1}`** — not `{0, 1, 0}`. The world's vertical axis swaps to Z so north stays "up" on screen.
- **OBJ load recomputes face normals at load time** — original file `vn` lines are ignored. Source-of-truth stays in geometry, not in baked normals.
- **`saveObjVertices` (round-trip)** — only `v` lines are rewritten; comments, materials, faces, `vn`, `vt` are preserved byte-exact. An edit-marker comment is appended (deduped on re-save). T-06 + T-07 in `tests/test_obj_save.cpp` verify this.
- **`RAYMATH_IMPLEMENTATION`** is defined on each executable target (`terra-siege`, `terra-siege-inspect`), not on the shared `terra_siege_mesh` static lib.

---

## Related Documents

**Active (root):**

- [README.md](README.md) — user-facing description, build/run, controls, status table
- [ROADMAP.md](ROADMAP.md) — three-track plan (engine / features / tooling)
- [base_mode_v2.md](base_mode_v2.md) — active Slice C design (asymmetric Base Mode)
- [terra_siege_inspect_roadmap.md](terra_siege_inspect_roadmap.md) — inspector spec

**Active design reference (project-status/):**

- [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md) — §§4-9 still active (resources, weapons, helicopter, smoke); Part 2 superseded by `base_mode_v2.md`

**Historical** ([project-status/archive/](project-status/archive/)):

- Earlier CLAUDE.md variants (phase-2-era and rebuild-era)
- `VIRUS_REMAKE_ARCHITECTURE.md` — original architectural rationale
- `project-roadmap.md` — Phase 2 flight-modes design (shipped)
- Rebuild specs: `flight_mode_rebuild.md` (shipped), `terrain_rebuild.md` (NOT shipped — Diamond-Square still in code), `camera_system.md` (shipped), `combat_tuning.md` (shipped), `radar_system.md` (shipped), `REBUILD_ROADMAP.md`, `readme_rebuild.md`

The terrain rebuild (sine-wave Fourier synthesis) is the one rebuild spec that has *not* been implemented — current code is still Diamond-Square. Revisit when there's appetite to land it.
