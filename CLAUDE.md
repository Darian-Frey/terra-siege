# CLAUDE.md — terra-siege Project Handoff

This document is the complete project context for Claude Code. Read it fully before
making any changes. It covers what the project is, all architectural decisions made,
what has been built, what still needs building, and how we intend to build it.

---

## What Is terra-siege?

terra-siege is a modern C++17 / raylib reimagining of **Virus** (Argonaut Software /
Firebird, 1988) for Linux (Windows later). The original Virus was a landmark 1988 game
by Jez San that rendered a real-time filled 3D polygon landscape on home computers — the
same techniques led directly to the Super FX chip and Star Fox on SNES.

terra-siege is an independent fan project. It keeps the original's core loop:
defend your planet against waves of alien attackers from the cockpit of a hovercraft.
It modernises the feature set with a larger view distance, directional shields,
weapon upgrades, an auto-turret, homing missiles, and a full radar system, while
deliberately preserving the flat-shaded low-poly polygon aesthetic.

**GitHub:** https://github.com/Darian-Frey/terra-siege
**Developer:** Solo developer, Linux Mint, building in Antigravity IDE with Claude Code.

---

## Technology Stack

| Component | Choice | Reason |
|-----------|--------|--------|
| Language | C++17 | `std::variant`, `std::optional`, structured bindings |
| Renderer / window / audio | raylib 5.0 | Native 3D API, flat-shading friendly, cross-platform |
| Build system | CMake 3.16+ with FetchContent | Fetches raylib automatically, no submodules |
| Math | raymath.h (bundled with raylib) | Vector3, Matrix, Quaternion ops |
| RNG | xorshift32 (hand-rolled) | Deterministic, seedable terrain generation |

**Critical naming note:** raylib defines `CameraMode` as a typedef. Our internal enum
was renamed to `CamMode` to avoid the collision. Watch for this when adding new types.

**Build commands:**
```bash
# Standard debug build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/terra-siege

# Dev mode (enables debug overlay, F1/F2 keys, future cheat features)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDEV_MODE=ON
cmake --build build -j$(nproc)
./build/terra-siege
```

---

## Project Structure

```
terra-siege/
├── CMakeLists.txt              # FetchContent raylib, DEV_MODE flag, asset copy
├── CLAUDE.md                   # This file
├── README.md                   # User-facing documentation
├── assets/
│   └── shaders/                # GLSL shaders (terrain_flat, shield_pulse, exhaust_trail)
│                               # Shader files exist but are empty stubs for now
└── src/
    ├── main.cpp                # Entry point: window init, fixed-timestep loop
    ├── core/
    │   ├── Clock.hpp           # Fixed-timestep accumulator (120 Hz physics)
    │   ├── Config.hpp          # ALL tuning constants — no magic numbers elsewhere
    │   ├── GameState.hpp       # Top-level state machine declaration
    │   └── GameState.cpp       # State machine implementation
    ├── world/
    │   ├── Heightmap.hpp/cpp   # Diamond-Square + smoothing + rivers + lakes
    │   ├── TerrainChunk.hpp/cpp# Flat-shaded mesh builder, WaterType colouring
    │   └── Planet.hpp/cpp      # Chunk orchestration, heightAt() query
    ├── entity/
    │   ├── Entity.hpp          # Base struct (stub — not yet used)
    │   ├── Player.hpp/cpp      # Hovercraft mesh, physics, input, flight assist
    │   ├── Enemy.hpp/cpp       # Stub
    │   ├── Friendly.hpp/cpp    # Stub
    │   ├── Projectile.hpp/cpp  # Stub
    │   └── EntityManager.hpp/cpp # Stub
    ├── weapon/                 # All stubs
    ├── shield/                 # All stubs
    ├── ai/                     # All stubs
    ├── renderer/               # All stubs
    ├── hud/                    # All stubs
    ├── audio/                  # All stubs
    └── wave/                   # All stubs
```

---

## Architecture Decisions (Do Not Change Without Good Reason)

### Fixed-Timestep Loop (Clock.hpp)
Physics runs at exactly 120 Hz decoupled from render rate. The pattern:
```
accumulator += frameTime
while accumulator >= FIXED_DT (1/120s):
    game.update(FIXED_DT)
    accumulator -= FIXED_DT
render(alpha = accumulator / FIXED_DT)   // interpolation factor
```
This lives in `main.cpp`. Never put frame-rate-dependent logic in `update()`.

### Config.hpp — Single Source of Truth
Every gameplay constant lives in `Config.hpp` as `constexpr`. No magic numbers
anywhere else in the codebase. When tuning, change Config.hpp only.

### Entity System — Flat Pools, Not ECS
The game will have < 500 live entities. A full ECS is unnecessary overhead.
Use flat `std::vector<EntityType>` pools per entity type in `EntityManager`.
Do not introduce a heavy ECS framework.

### No Heap Allocation in Hot Path
Entity pools, particle pools, and projectile pools must be pre-allocated at startup.
No `new`/`malloc` inside `update()` or `render()`.

### Separation of Update and Render
Nothing in the physics tick (update) touches raylib draw calls.
Nothing in render mutates game state.

### Procedural Geometry — No Model Files
All 3D assets (player ship, terrain objects, enemies) are built as raylib `Mesh`
structs filled with vertices in C++ code. No `.obj`, `.glb`, or other model files.
This keeps the flat-shaded aesthetic consistent and eliminates an asset pipeline.

### Terrain Architecture
```
Heightmap (1025×1025 floats + WaterType map)
    → Diamond-Square generation
    → 12-pass box-blur smoothing
    → Radial falloff (continent shape)
    → Normalise [0,1]
    → Ocean classification (below SEA_LEVEL)
    → River carving (downhill flow from high sources)
    → Lake flooding (flood-fill local minima)
    ↓
Planet (16×16 grid of TerrainChunk)
    → Each chunk: 64×64 quads, ~8192 triangles
    → Total: ~2.1M triangles for full terrain
    ↓
TerrainChunk
    → Flat-shaded: vertices duplicated per triangle (no shared normals)
    → Per-face colour by height band + WaterType
    → Directional lighting: sun at (0.57, 0.74, 0.36), ambient 0.45
    → Sea-level clamp: anything below SEA_LEVEL snapped to flat ocean surface
```

### Coordinate System
- **+Z = North (forward)**  
- **+X = East (right)**  
- **+Y = Up**  
- Player yaw=0 faces +Z. Yaw increases clockwise (turning right).
- World origin (0,0,0) is the heightmap corner.
- Terrain centre is at (worldSize/2, ?, worldSize/2) where worldSize = 1024 * TERRAIN_SCALE = 4096 units.

### Camera System
Two modes (toggled with F1 in DEV_MODE):
- **Follow** (default, gameplay): smooth lerp behind/above player, terrain-clamped
  so it never goes underground. Banks slightly with player roll.
- **FreeRoam** (dev only): WASD + mouse look, full freedom.

Camera desired position is **always clamped above terrain** before lerping:
```cpp
float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
desiredPos.y = max(desiredPos.y, camGroundH + 3.5f);
desiredPos.y = max(desiredPos.y, playerPos.y + 2.0f);
```
Do not remove this — without it the camera clips underground when turning toward hills.

---

## What Has Been Built (Phases 1, 1.5, 2 partial)

### Phase 1 ✅
- CMakeLists.txt with FetchContent raylib 5.0, DEV_MODE compile flag
- main.cpp with fixed-timestep loop, DisableCursor()
- Clock.hpp accumulator
- Config.hpp with all constants
- GameState state machine (Playing / Paused / MainMenu / GameOver / Victory)
- Planet, TerrainChunk, Heightmap — terrain generates and renders

### Phase 1.5 ✅
- Heightmap resolution bumped to 1025×1025 (~2.1M terrain triangles)
- Diamond-Square + 12-pass smoothing → proper rolling hills, not spike-fields
- Radial falloff → continent sits in ocean, not edge-to-edge land
- River carving: downhill flow simulation from high-altitude sources
- Lake flooding: flood-fill at local terrain minima (inland only, margined away from edges)
- WaterType map (None / Ocean / Lake / River) — distinct colours per water type
- Debug HUD panel (top-right): X, Z, Alt, AGL, Speed, Heading+compass, Flight Assist level
- Hull health bar (bottom-left)

### Phase 2 (partial) ✅
- Player.hpp/cpp: procedural hovercraft mesh (hull, engine pods, cockpit, red stripe, nozzles)
- Player physics: thrust, drag (frame-rate independent via `powf`), speed cap
- Terrain altitude clamping (downward heightAt query each tick)
- Flight assist system: 4 levels (Raw/Minimal/Standard/Full) — coefficients in Config
- Visual banking (roll) driven by turn input
- Visual pitch (nose dips slightly when thrusting)
- Level 3 assist: predictive terrain look-ahead raycast
- Follow camera with terrain clamping
- GameState wired up: player init, update, render
- F1: camera mode toggle (dev mode)
- F2: flight assist level cycle (dev mode)

### Known Issues / Things Still Needing Attention
- Ship mesh component positions may still need minor tuning (cockpit, stripe, pods)
  after the last coordinate fix — verify visually
- Forward direction key is W (thrust) and A/D (turn). Arrow keys also work.
  Q/E = altitude. Shift = boost. This matches the original Virus feel.
- The `drawHUD()` in GameState.cpp currently uses `m_player` coordinates.
  Once enemies are added, it will need extending.

---

## What Needs Building (Phases 2 completion → 6)

### Phase 2 — Complete the player craft
These items still need implementing:

**2a. Gamepad support**
raylib handles gamepads transparently. Add to `Player::handleInput()`:
```cpp
if (IsGamepadAvailable(0)) {
    float stickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
    float stickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
    // map to turn and thrust
}
```

**2b. Engine exhaust particles**
A simple CPU particle emitter at the rear of each engine pod.
Particles: orange/yellow, billboard quads, 0.3s lifetime, velocity = -forward * speed.
Particle pool is pre-allocated (Config::PARTICLE_POOL_SIZE = 2000).

**2c. Ship shadow**
A dark transparent ellipse projected onto the terrain below the ship.
Simple darkening quad at `heightAt(pos.x, pos.z)` world position.

---

### Phase 3 — Enemies, Combat, Wave Manager

**Enemy types to implement (in priority order):**

| Type | Behaviour | Threat Target |
|------|-----------|---------------|
| Fighter | Pursue → strafe → evade state machine | Player |
| Bomber | Low-altitude run toward friendly units | Friendly units |
| Swarm Drone | Flocking (separation/alignment/cohesion) toward player | Player |
| Carrier | Hovers at altitude, spawns drones | Spawn source |
| Ground Turret | Stationary, rotates to track player | Player |

**Enemy AI architecture:**
- Base class `AIController` with virtual `update(dt)` and state enum
- States: IDLE → PURSUE → ATTACK → EVADE
- Bomber adds: STRAFE_FRIENDLY state
- Swarm drones use three-rule flocking, no explicit state machine
- `SpatialGrid` (2D grid, cell size = Config::SPATIAL_CELL_SIZE = 60) for O(1) nearest-entity queries

**Friendly units:**

| Type | Purpose |
|------|---------|
| Collector | Moves between waypoints, scores points when it reaches base |
| Repair Station | Restores player hull HP on proximity |
| Radar Booster | Extends radar range while alive |

Destroying ALL friendly units = game over.

**Wave manager:**
- Data-driven `WaveDef` structs (waveNumber, enemyCount, types[], spawnInterval, hasCarrier, aggressionMultiplier)
- Grace period between waves for shield recharge
- Boss wave every N waves: Carrier + full fighter escort
- `WaveManager::update(dt)` drives spawn scheduling

**Weapon system (Phase 3 alongside enemies):**

Three weapon slots: Primary, Secondary, Special.

```cpp
enum class WeaponType {
    // Primary
    Cannon,         // rapid-fire, unlimited, low damage
    PlasmaCannon,   // slower, splash, limited ammo
    BeamLaser,      // continuous, energy drain
    // Secondary  
    Missile,        // single homing, proportional navigation
    ClusterMissile, // splits into 4 on proximity
    DepthCharge,    // drops down, area effect vs ground
    // Special
    AutoTurret,     // parented to ship, independent targeting AI
    ShieldBooster,  // temporary absorb
    EMP,            // area stun, stops enemy weapons
};
```

**Missile guidance — Proportional Navigation:**
```cpp
// N ≈ 4 (Config::MISSILE_NAV_N)
closing_velocity = dot(relative_velocity, LOS_unit_vector)
LOS_rate = cross(relative_pos, relative_vel) / |relative_pos|²
acceleration = N * closing_velocity * LOS_rate
```

**Auto-turret** runs as an independent update separate from player firing:
```
find nearest enemy within Config::TURRET_RANGE
rotate toward target at Config::TURRET_ROT_SPEED
if angle_to_target < Config::TURRET_FIRE_CONE: fire
```

---

### Phase 3.5 — Terrain Objects

All terrain objects are procedural geometry (no model files). Placement is data-driven
via a rules table (min/max height, terrain band, min spacing between instances).

Functional objects (needed for gameplay):
- Military Base — spawn point for friendly units, 40×40 footprint
- Launch Pad — octagonal platform, corner light posts
- Radar Dish — parabolic fan triangles + rotating support arm
- Gun Tower — cylinder base + rotating turret box (enemy ground turret visual)

Atmospheric objects (Phase 5 polish):
- Tree clusters — 2-3 stacked cones
- Rock formations — irregular low boxes
- Radio towers — tapered box + cross-braces
- Antenna arrays — thin vertical cylinders
- Crash sites — flattened ellipse + debris triangles

**Placement algorithm:**
After heightmap generation, scan the heightmap with the rules table.
Use a seeded RNG (same seed as terrain) for deterministic placement.
Store placed objects in `std::vector<TerrainObject>` on `Planet`.

---

### Phase 4 — Directional Shields, HUD, Radar

**Shield system:**
Four independent quadrant HP pools: Front, Rear, Left, Right.
```cpp
struct ShieldSystem {
    float sectorHP[4];        // Config::SHIELD_HP_MAX each
    float timeSinceHit[4];    // per-sector, recharge delays independently
    float rechargeRate;       // Config::SHIELD_RECHARGE_RATE HP/s
    float rechargeDelay;      // Config::SHIELD_RECHARGE_DELAY seconds
};
```
Hit direction (relative to player forward) determines which sector takes damage.
Depleted sector passes overflow damage to hull HP.
Visual: translucent hemisphere shader around ship, UV-animated pulse on hit.

**Radar:**
- Circular minimap, ego-centric (player always at centre, heading = up)
- Range: Config::RADAR_BASE_RANGE, extendable by Radar Booster friendly
- Blips: green (friendly), red (enemy), yellow (projectile)
- Blink rate proportional to proximity (faster = closer)
- Altitude strip on radar edge: shows targets above/below player altitude

**Full HUD layout:**
- Top-right: debug panel (already implemented, will expand)
- Bottom-left: hull health bar (already implemented)
- Bottom-right: radar circle + altitude strip
- Bottom-centre: weapon slot display (3 slots, active ammo/energy/cooldown)
- Top-centre (thin bar): wave number and enemy count remaining
- Directional shield display: small pie/sector diagram, colour = HP level

---

### Phase 5 — Polish

- **Particle system:** Pre-allocated pool (2000 particles). Emitters for engine exhaust,
  weapon muzzle flash, explosion burst, shield impact sparks, missile smoke trail.
  Billboard quads, sorted back-to-front for translucency.
- **Audio:** raylib miniaudio backend. Positional audio via manual pan/volume
  calculation (raylib doesn't do 3D audio natively). 4 concurrent weapon-fire channels.
  Distinct sound per weapon type.
- **Day/night cycle:** Sun direction animates over time. Sky colour lerps from dawn to
  noon to dusk to night. Terrain ambient/diffuse adjusts accordingly.
- **Weather:** Wind vector affects handling (drift). Storm = reduced visibility (fog
  near/far shrink). Visual: particle snow/rain overlay.
- **Post-FX (optional):** Scanline vignette, mild bloom on engine nozzles/explosions.
  raylib custom shader pass.

---

### Phase 6 — Extended Features

- Leaderboard / scoring persistence (simple binary file or JSON)
- Cockpit camera mode (inside the ship, looking forward, no external model visible)
- Replay recording (deterministic at 120 Hz — record inputs, replay inputs)
- New weapon types open for expansion (gravity well bomb, chain lightning, tractor beam)
- New enemy types open for expansion (cloaking drone, kamikaze, artillery)

---

## Difficulty System

Four presets, selected at game start:

| Preset | Flight Assist | Enemy Aggression | Shield Recharge | Pickup Rate |
|--------|---------------|------------------|-----------------|-------------|
| Veteran | Raw (0) | 1.3× | 0.6× | 0.5× |
| Pilot | Minimal (1) | 1.0× | 1.0× | 1.0× |
| Recruit | Standard (2) | 0.8× | 1.4× | 1.5× |
| Commander | Full (3) | 0.6× | 2.0× | 2.0× |

Flight assist coefficients (Config::ASSIST_LEVEL_COEFFS[4] = {0.0, 0.18, 0.42, 0.75}):
- Level 0 (Raw): No correction. Pure momentum physics.
- Level 1 (Minimal): Auto-levelling roll only.
- Level 2 (Standard): Auto-level + lateral velocity dampening.
- Level 3 (Full): Auto-level + dampen + predictive terrain avoidance look-ahead.

---

## Key Constants Reference (Config.hpp)

```cpp
// Timing
FIXED_DT            = 1/120s    // physics tick rate
MAX_FRAME_TIME      = 0.05s     // spiral-of-death guard

// Terrain
HEIGHTMAP_SIZE      = 1025      // must be 2^n+1
CHUNK_COUNT         = 16        // 16×16 chunk grid
TERRAIN_SCALE       = 4.0f      // world units per heightmap cell
TERRAIN_HEIGHT_MAX  = 55.0f     // max mountain height in world units
TERRAIN_ROUGHNESS   = 0.55f     // diamond-square roughness
SEA_LEVEL           = 0.20f     // fraction of HEIGHT_MAX
RIVER_COUNT         = 6
LAKE_COUNT          = 12

// Player
PLAYER_THRUST       = 28.0f
PLAYER_DRAG         = 0.92f     // per-tick multiplier (applied as powf per dt)
PLAYER_MAX_SPEED    = 40.0f
PLAYER_TURN_RATE    = 1.8f      // rad/s
PLAYER_MIN_ALTITUDE = 2.5f      // AGL minimum
PLAYER_BANK_RATE    = 3.5f

// Camera
CAM_HEIGHT          = 6.0f      // units above player
CAM_DISTANCE        = 14.0f     // units behind player
CAM_FOV             = 70.0f
CAM_FOLLOW_SPEED    = 6.0f

// Weapons
TURRET_RANGE        = 60.0f
MISSILE_NAV_N       = 4.0f      // proportional navigation constant
CANNON_FIRE_RATE    = 0.08s

// Shields
SHIELD_HP_MAX       = 100.0f
SHIELD_RECHARGE_RATE= 8.0f HP/s
SHIELD_RECHARGE_DELAY=3.0s

// Radar
RADAR_BASE_RANGE    = 300.0f
```

---

## Coding Standards

- **No magic numbers** — add to Config.hpp
- **No heap allocation in hot path** — use pre-allocated pools
- **No raylib draw calls in update()** — strict separation
- **Header guards** — `#pragma once` on all headers
- **Naming** — PascalCase classes, camelCase methods, m_ prefix for members,
  ALL_CAPS for Config constants
- **Comments** — explain *why*, not *what*
- **No exceptions in hot paths** — use return codes or optional
- **Platform paths** — use `std::filesystem::path` not string literals

---

## Next Immediate Tasks

The project was in the middle of Phase 2 when this handoff was created.
The player craft is flying and the camera is working. Start here:

1. **Verify the ship mesh looks correct** — engine pods should be flush against
   the hull wings, cockpit should sit on the hull surface (not float), red stripe
   should be a thin painted-on band not a floating plank.
   The last coordinate fix was: pods at ±[1.80, 2.60], cockpit at Y=[0.25, 0.58],
   stripe at Y=[0.24, 0.28].

2. **Complete Phase 2:**
   - Add gamepad support to `Player::handleInput()`
   - Add engine exhaust particle emitter
   - Add ship ground shadow

3. **Move to Phase 3** once Phase 2 feels solid:
   - `Entity.hpp` base struct (position, velocity, health, type, id, alive)
   - `EntityManager` with flat pools per type and spatial grid
   - First enemy type: Fighter (pursuit → attack → evade state machine)
   - `WeaponSystem` with Cannon as first weapon (player can fire)
   - Collision detection between projectiles and enemies
   - Wave manager (simple: spawn fighters, clear wave, repeat)

Do not skip ahead to HUD or shields until basic combat (player can shoot enemies,
enemies can shoot player, waves spawn) is working and feels good.

---

## Aesthetic Guidelines

The visual target is "modernised but stylistically similar" to the 1988 original:
- Flat-shaded polygons with no texture mapping
- Visible polygon faces — low poly is a feature, not a bug
- One directional sun light, ambient ~0.45, no specular
- Colour by height/type bands (no gradients within a face)
- Fog that matches sky colour at max draw distance
- Simple particle effects — no complex shaders until Phase 5

The game should look like it could have run in 1992 on very powerful hardware,
not like a modern game with a retro filter applied.
