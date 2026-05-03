# CLAUDE.md — terra-siege Project Context

## Read This First

Before making any changes, read these documents in this order:

1. **This file** — project overview, architecture rules, current state
2. **readme_rebuild.md** — guide to the five rebuild design documents
3. The specific rebuild document for the system you are implementing

The rebuild documents (`flight_mode_rebuild.md`, `terrain_rebuild.md`,
`combat_tuning.md`, `camera_system.md`, `radar_system.md`) take precedence
over this file where there is a conflict. They represent more recent design
decisions made after this file was last updated.

---

## What Is terra-siege?

A modern C++17 / raylib reimagining of **Virus** (Argonaut Software / Firebird,
1988) for Linux (Windows later). Defend your planet against waves of alien
attackers from the cockpit of a hovercraft. Preserves the flat-shaded low-poly
polygon aesthetic of the original while adding directional shields, weapon
upgrades, auto-turret, homing missiles, and a full tactical radar.

**GitHub:** https://github.com/Darian-Frey/terra-siege
**Developer:** Solo, Linux Mint, Antigravity IDE + Claude Code.
**Original:** Jez San, Argonaut Software — the same techniques led to the
Super FX chip and Star Fox (SNES).

---

## Technology Stack

| Component | Choice |
|-----------|--------|
| Language | C++17 |
| Renderer / window / audio | raylib 5.0 (FetchContent) |
| Math | raymath.h (bundled with raylib) |
| Build | CMake 3.16+ |
| RNG | xorshift32 (hand-rolled, deterministic) |

**Build commands:**
```bash
# Standard debug
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/terra-siege

# Dev mode — F1–F4 keys, flight recorder, free camera, debug overlay
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDEV_MODE=ON
cmake --build build -j$(nproc)
./build/terra-siege
```

### Critical Naming Collisions

| Name | Problem | Correct Usage |
|------|---------|--------------|
| `CameraMode` | raylib typedef — compile error | Never use this name |
| `CamMode` | Existing enum for Follow/FreeRoam dev toggle | Use only for that |
| `CameraView` | New enum for five player views | Use this for view switching |
| `FlightMode` | **Removed in flight rebuild** | Do not re-add |
| `CraftType` | **Removed in flight rebuild** | Do not re-add |
| `smooth()` | **Removed in terrain rebuild** | Do not re-add |
| `applyRadialFalloff()` | **Removed in terrain rebuild** | Do not re-add |

---

## Project Structure

```
terra-siege/
├── CMakeLists.txt
├── CLAUDE.md                   ← This file
├── readme_rebuild.md           ← Read after this file
├── flight_mode_rebuild.md      ← Flight physics rebuild spec
├── terrain_rebuild.md          ← Terrain generation rebuild spec
├── combat_tuning.md            ← Combat balance and weapon specs
├── camera_system.md            ← Five-view camera system spec
├── radar_system.md             ← Radar display system spec
├── README.md                   ← User-facing documentation
├── assets/shaders/
│   ├── terrain_flat.vs/.fs     ← Stub — not yet wired in
│   ├── shield_pulse.fs         ← Stub
│   └── exhaust_trail.fs        ← Stub
└── src/
    ├── main.cpp                ← Fixed-timestep loop, DisableCursor()
    ├── core/
    │   ├── Clock.hpp           ← Fixed-timestep accumulator (120 Hz)
    │   ├── Config.hpp          ← ALL constants — no magic numbers elsewhere
    │   ├── GameState.hpp       ← State machine + CamMode enum
    │   └── GameState.cpp       ← Full implementation
    ├── world/
    │   ├── Heightmap.hpp/cpp   ← Currently D-S — REBUILD to sine waves
    │   ├── TerrainChunk.hpp/cpp← Flat-shaded mesh, WaterType colouring
    │   ├── Planet.hpp/cpp      ← Chunk grid, heightAt(), ProgressCb
    │   └── SkyDome.hpp/cpp     ← Stub
    ├── entity/
    │   ├── Entity.hpp          ← Stub — needs base struct in Phase 3
    │   ├── Player.hpp/cpp      ← REBUILD to single Newtonian model
    │   ├── Enemy.hpp/cpp       ← Stub — Phase 3
    │   ├── Friendly.hpp/cpp    ← Stub — Phase 3
    │   ├── Projectile.hpp/cpp  ← Stub — Phase 3
    │   └── EntityManager.hpp/cpp ← Stub — Phase 3
    ├── weapon/                 ← All stubs — Phase 3
    ├── shield/                 ← All stubs — Phase 4
    ├── ai/                     ← All stubs — Phase 3
    ├── renderer/               ← All stubs — Phase 5
    ├── hud/                    ← All stubs — Phase 4
    │   ├── Radar.hpp/cpp       ← Stub — replace with radar_system.md spec
    ├── audio/                  ← All stubs — Phase 5
    └── wave/                   ← All stubs — Phase 3
```

---

## Architecture Rules — Do Not Change Without Good Reason

### Fixed-Timestep Loop (120 Hz)
Physics runs at exactly 120 Hz decoupled from render rate. Lives in `main.cpp`.
Never put frame-rate-dependent logic in `update()`.

```
accumulator += frameTime (capped at MAX_FRAME_TIME)
while accumulator >= FIXED_DT (1/120s):
    game.update(FIXED_DT)
    accumulator -= FIXED_DT
render(alpha = accumulator / FIXED_DT)
```

### Config.hpp — Single Source of Truth
Every constant is `constexpr` in `namespace Config`. No magic numbers anywhere
else. When tuning, change Config.hpp and recompile.

### No Heap Allocation in Hot Path
Entity, particle, and projectile pools must be pre-allocated at startup.
No `new`/`malloc` inside `update()` or `render()`.

### Separation of Update and Render
Nothing in `update()` calls raylib draw functions.
Nothing in `render()` mutates game state.

### Procedural Geometry — No Model Files
All 3D assets are raylib `Mesh` structs built in C++ code. No `.obj`/`.glb`.
Keeps flat-shaded aesthetic consistent, eliminates asset pipeline.

### Coordinate System
- **+Z = North (forward)**
- **+X = East (right)**
- **+Y = Up**
- Player yaw=0 faces +Z. Yaw increases clockwise (right turn).
- World origin at heightmap corner (0,0,0).
- World centre ≈ (4096, ?, 4096) at current TERRAIN_SCALE=4 and 2049 heightmap.
  After terrain rebuild with TERRAIN_SCALE=8: centre ≈ (8192, ?, 8192).

### Camera Terrain Clamping — Do Not Remove
The follow camera must always query terrain height before lerping to desired
position. Without this the camera clips underground when turning toward hills:

```cpp
float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
desiredPos.y = std::max(desiredPos.y, camGroundH + 3.5f);
desiredPos.y = std::max(desiredPos.y, playerPos.y + 2.0f);
```

This applies to all five camera views. The terrain query uses wrapped coordinates
after the terrain rebuild.

---

## Current Build State

### Completed ✅

**Phase 1 — Foundation**
- CMakeLists.txt, FetchContent raylib 5.0, DEV_MODE flag
- main.cpp fixed-timestep loop, DisableCursor()
- Clock.hpp, Config.hpp, GameState state machine
- Planet / TerrainChunk / Heightmap — terrain generates and renders
- Loading screen with progress bar (Planet::generate takes ProgressCb)
- Far clip plane extended to 3000 via manual rlFrustum override

**Phase 1.5 — Terrain Features**
- Heightmap 2049×2049 (32×32 chunks, ~8.4M triangles, 8192×8192 world)
- Diamond-Square + 12-pass smoothing + radial falloff
- WaterType map: Ocean / Lake / River — distinct colours
- River carving (downhill flow), lake flooding (flood-fill local minima)
- Debug HUD panel: X, Z, Alt, AGL, Speed, Heading, Pitch, Roll, Assist
- Hull health bar

**Phase 2 (partial) — Player Craft**
- Player physics, input, flight assist (4 levels)
- Follow camera with terrain clamping
- DEV_MODE F-keys: F1 (camera), F2 (assist), F4 (flight recorder)
- Flight recorder — 120Hz CSV logging to tests/logs/

### Pending Rebuild 🔧

**These systems exist but are being replaced:**
- `Heightmap` — Diamond-Square replaced by sine wave generation
  (see `terrain_rebuild.md`)
- `Player` — dual flight model (Classic + Arcade + 5 craft types) replaced
  by single Newtonian model (see `flight_mode_rebuild.md`)
- `GameState` camera — single follow camera replaced by five-view system
  (see `camera_system.md`)

### Stubs — Not Yet Implemented ⏳
All files in: `weapon/`, `shield/`, `ai/`, `renderer/`, `hud/`, `audio/`,
`wave/`, plus `entity/Enemy`, `entity/Friendly`, `entity/Projectile`,
`entity/EntityManager`, `entity/Entity` (base struct), `world/SkyDome`

---

## Planned Systems (Design Complete)

### Flight Model (flight_mode_rebuild.md)
Single Newtonian physics model. Thrust along local UP axis, constant gravity,
near-zero drag. Mouse pitch/yaw, keyboard backup. Four-level flight assist as
difficulty modifier. Fuel system. Landing/crash detection. One hovercraft mesh.

**New Config block:** `NEWTON_*` constants replace all `ARCADE_*` and `CLASSIC_*`

### Terrain (terrain_rebuild.md)
Sine wave Fourier synthesis replacing Diamond-Square. 16 terms in three octaves
(Continental 4 + Regional 6 + Local 6). Irrational frequency ratios (φ, √2, √3,
√5, √7, √11) eliminate visible tiling. Domain warping for organic shapes.
Toroidal world — player wraps at edges, `heightAt()` uses modular coordinates.
Rivers and lakes unchanged — same code runs on sine terrain.

**Removed from Heightmap:** `diamondSquare()`, `smooth()`, `applyRadialFalloff()`
**Added to Heightmap:** `buildSineTerms()`, `sineWaveGenerate()`
**Updated:** `generate()`, `sample()` (modular wrap)
**Updated:** `Planet::heightAt()` (modular wrap)
**Added to Player:** `wrapPosition()` called at end of `applyPhysics()`

### Camera System (camera_system.md)
Five views selectable with keys 1–5. 2-second fade label on switch.

| Key | View | Behaviour |
|-----|------|-----------|
| 1 | Chase | Follows ship nose — default combat |
| 2 | Velocity | Follows velocity vector — terrain avoidance |
| 3 | Tactical | Fixed overhead, north up — battlefield awareness |
| 4 | Threat-lock | Rotates to keep nearest enemy in frame |
| 5 | Classic | Fixed diagonal-down — original Virus feel |

New enum: `CameraView { Chase=0, Velocity=1, Tactical=2, ThreatLock=3, Classic=4 }`
Tactical view: `camera.up = {0, 0, 1}` — CRITICAL, not `{0, 1, 0}`
Threat-lock: stubs to Chase until Phase 3 enemies exist
New GameState members: `m_cameraView`, `m_viewLabelTimer`, `m_threatCamYaw`,
`m_threatLockTarget`

### Combat System (combat_tuning.md)
TTK-derived HP values. All HP is arithmetic from TTK × Cannon DPS (100 DPS).
Never edit HP constants directly — change TTK targets and re-derive.

Enemy total HP: Drone=8, Seeder=50, Fighter=200, Bomber=500, Carrier=2500, Turret=400

Nine weapons across three slots (Primary / Secondary / Special):
- Primary: Cannon → Plasma Cannon → Beam Laser
- Secondary: Standard Missile or Cluster Missile (round-start selection, not pickup)
- Special: Auto Turret / Shield Booster / EMP

**Missile selection — Option B (round-start):** Player chooses missile type at
wave start via selection UI showing wave composition. No mid-wave swapping.
Standard (N=3.5, evadable, best vs Bombers). Cluster (N=6.0, very hard to shake,
splits into 4 submunitions, best vs Drones/swarms).

**Beam Laser critical detail:** Sets `timeSinceHit=0` each tick while on target —
this suppresses Carrier shield regeneration. Do not omit this.

Enemy shields: Fighters (omnidirectional, 40HP). Bombers (150HP). Carriers
(four-sector directional, 250HP each). Drones/Seeders/Turrets: no shields.

Player shields: Directional four-sector, 100HP each, 400HP total.

### Radar System (radar_system.md)
Three implementation tiers:

**Tier 1 (Phase 4 baseline — mandatory before wave testing):**
120px disc bottom-right, 16px altitude strip, IFF colours, blip shapes by enemy
type, proximity pulse blink, inner/outer range rings.

**Tier 2 (Phase 4 complete):**
Threat vector arrows, inbound missile warning ring, zone overlay, Carrier drone
count, screen-edge missile direction indicator.

**Tier 3 (Phase 5 polish):**
Ghost blips for lost contacts, radar jamming near Carriers, boost visual,
camera-view-aware sizing (Tactical hides disc, Classic enlarges disc 20%).

Blip shapes: Drone=dot, Seeder=dot(larger), Fighter=triangle, Bomber=square,
Carrier=diamond, Turret=cross, Friendly=circle, Pickup=star.

Ghost pool: 32 pre-allocated slots (no heap allocation).
Jamming: `sin(id * constant + time)` for smooth deterministic jitter.
`worldToRadar()` rotates by `-playerYaw` so heading is always up on disc.

---

## Enemy Design

| Enemy | Hull HP | Shield HP | Shield Type | TTK (cannon) |
|-------|---------|-----------|-------------|-------------|
| Swarm Drone | 8 | 0 | None | 0.08s (1 shot) |
| Seeder | 50 | 0 | None | 0.5s |
| Fighter | 160 | 40 | Omnidirectional | 2.0s |
| Bomber | 350 | 150 | Omnidirectional | 5.0s |
| Carrier | 1500 | 250×4 | Four-sector directional | 25.0s |
| Ground Turret | 400 | 0 | None | 4.0s |

AI states: IDLE → PURSUE → ATTACK → EVADE (Bomber adds STRAFE_FRIENDLY)
Swarm Drones: flocking (separation / alignment / cohesion), no state machine
SpatialGrid: 2D grid, cell size = Config::SPATIAL_CELL_SIZE (60 units), rebuilt
each tick from flat entity arrays.

Friendly units: Collector, Repair Station, Radar Booster.
All friendly units destroyed = game over.

---

## Difficulty System

| Preset | Flight Assist | Aggression | Shield Recharge | Pickups |
|--------|---------------|------------|-----------------|---------|
| Veteran | Raw (0) | 1.3× | 0.6× | 0.5× |
| Pilot | Minimal (1) | 1.0× | 1.0× | 1.0× |
| Recruit | Standard (2) | 0.8× | 1.4× | 1.5× |
| Commander | Full (3) | 0.6× | 2.0× | 2.0× |

Flight assist coefficients: `{0.0, 0.18, 0.42, 0.75}` (ASSIST_LEVEL_COEFFS).

---

## Key Config Constants (Current → Post-Rebuild)

```
// Timing (unchanged)
FIXED_DT            = 1/120s
MAX_FRAME_TIME      = 0.05s

// World (terrain rebuild changes these)
HEIGHTMAP_SIZE      = 2049 → 1025   (faster generation, same visual quality)
CHUNK_COUNT         = 32  → 16
TERRAIN_SCALE       = 4.0 → 8.0    (world stays ~8192×8192)
TERRAIN_HEIGHT_MAX  = 180 → 120    (tunable for sine terrain)
SEA_LEVEL           = 0.20 → 0.30  (no falloff means more natural ocean coverage)

// Flight (all ARCADE_* and CLASSIC_* removed, replaced by NEWTON_*)
NEWTON_GRAVITY         = 9.8f
NEWTON_THRUST          = 24.0f
NEWTON_DRAG            = 0.02f
NEWTON_PITCH_MAX       = 1.30f
NEWTON_FUEL_MAX        = 100.0f
NEWTON_FUEL_BURN_RATE  = 3.5f

// Camera (simplified — no speed-dependent values)
CAM_DISTANCE        = 18.0f
CAM_HEIGHT          = 8.0f
CAM_FOV             = 75.0f
CAM_LERP            = 8.0f

// Combat (TTK-derived — change TTK targets, not HP directly)
TTK_FIGHTER         = 2.0s
TTK_BOMBER          = 5.0s
TTK_CARRIER         = 25.0s

// Radar
RADAR_BASE_RANGE    = 300.0f
RADAR_BOOST_RANGE   = 500.0f
RADAR_ALT_STRIP_RANGE = 150.0f
```

---

## Implementation Order

```
Step 1  flight_mode_rebuild.md    Gut dual model → Newtonian rebuild
Step 2  terrain_rebuild.md        D-S → sine waves, toroidal wrap
Step 3  camera_system.md          Five camera views
Step 4  Phase 2 completion        Gamepad, exhaust particles, ground shadow
─────── Phase 2 complete ──────────────────────────────────────────────────
Step 5  combat_tuning.md          Phase 3: entities, weapons, waves
Step 6  radar_system.md Tier 1    Before any wave testing begins
Step 7  radar_system.md Tier 2    Full Phase 4 HUD + shields
─────── Phase 4 complete ──────────────────────────────────────────────────
Step 8  Phase 5 polish            Particles, audio, day/night, weather,
                                  radar Tier 3
Step 9  Phase 6 extended          Score persistence, cockpit cam, replay
```

---

## DEV_MODE Features

Compile with `-DDEV_MODE=ON`. All dev features are `#ifdef DEV_MODE` guarded
and produce zero overhead in release builds.

| Key | Action |
|-----|--------|
| F1 | Toggle Follow/FreeRoam camera (CamMode, separate from CameraView) |
| F2 | Cycle flight assist level (0–3) |
| F3 | God mode — invincible, infinite fuel (Phase 3+) |
| F4 | Toggle flight recorder (120Hz CSV to tests/logs/) |

F5 (craft cycle) and F6 (flight mode toggle) are removed by the flight rebuild.

---

## Coding Standards

- No magic numbers — add to Config.hpp
- No heap allocation in hot path — pre-allocated pools only
- No raylib draw calls in `update()`
- `#pragma once` on all headers
- PascalCase classes, camelCase methods, `m_` prefix for members,
  ALL_CAPS for Config constants
- Comments explain *why*, not *what*
- `std::filesystem::path` for all file paths — no raw string literals
- Never name anything `CameraMode` — raylib already defines it

---

## Aesthetic Guidelines

Visual target: "modernised but stylistically similar" to the 1988 original.
- Flat-shaded polygons, no texture mapping
- Visible polygon faces — low-poly is a feature, not a bug
- One directional sun, ambient ~0.45, no specular
- Colour by height/type bands, no gradients within a face
- Fog matches sky colour at max draw distance
- Simple particles only — no complex shaders before Phase 5

"Looks like it could have run in 1992 on very powerful hardware — not a modern
game with a retro filter applied."

---

## Notes

- `Planet::heightAt()` must use modular (wrapped) coordinates after terrain rebuild.
  Call this before any ground-height query — player physics, camera clamping,
  terrain object placement all depend on it being correct.
- The rlFrustum override in `GameState::render()` extends the far clip plane to
  3000. Do not remove it — raylib's default 1000 unit far plane clips the world.
- The loading screen progress callback (ProgressCb) in Planet::generate() must
  be preserved in the terrain rebuild — startup time will increase slightly and
  the loading screen is needed.
- Ghost blip pool (Radar) is pre-allocated at 32 slots — no heap allocation.
- Cluster missile 120° submunition search cone must not be widened to full-sphere —
  it is what makes the weapon spread across a swarm rather than all targeting one.
- Beam Laser sets `timeSinceHit=0` each tick while on target. This is the Carrier
  fight mechanic. Do not omit it.
