# VIRUS REMAKE — Architecture Plan

> **⚠ Archived 2026-06-03.** Historical reference — the original architectural rationale, predating any code. Original path: `project-status/VIRUS_REMAKE_ARCHITECTURE.md`.
> Active replacement: [`/CLAUDE.md`](../../CLAUDE.md) for the current architecture rules. This doc remains valuable as the *why* behind those rules (raylib choice, flat pools over ECS, fixed timestep, directional shields, etc.).

**Modern C++ / raylib reimagining of Virus (Argonaut/Firebird, 1988)**

---

## 1. Technology Decisions

### Primary Library: raylib (not SDL2)
SDL2 is excellent but primarily 2D-oriented — adding a full 3D renderer on top of it means writing or integrating a significant rendering layer yourself. raylib provides:
- A clean, immediate-mode 3D API (cameras, meshes, shaders, models)
- Built-in audio (miniaudio backend)
- Window, input, and timing management
- A genuine flat-shaded / retro aesthetic that suits the stylistic target

**Recommendation:** Use raylib as the sole external dependency for now. Add SDL2 only if you later need its controller rumble API or platform quirks that raylib doesn't cover.

### Language Standard: C++17
- `std::variant` for weapon slot types
- `std::optional` for nullable game state
- Structured bindings, `if constexpr`, filesystem library for asset loading
- No exceptions in hot paths — use error codes or `std::expected` (C++23) if needed

### Build System: CMake + FetchContent
```cmake
FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib GIT_TAG 5.0)
FetchContent_MakeAvailable(raylib)
```
Single `CMakeLists.txt` at root, no submodule headaches.

---

## 2. Project Structure

```
virus-remake/
├── CMakeLists.txt
├── assets/
│   ├── shaders/
│   │   ├── terrain_flat.vs
│   │   ├── terrain_flat.fs
│   │   ├── shield_pulse.fs
│   │   └── exhaust_trail.fs
│   ├── sounds/
│   └── fonts/
├── src/
│   ├── main.cpp                  # Entry point, game loop
│   ├── core/
│   │   ├── GameState.hpp/cpp     # Top-level state machine
│   │   ├── Clock.hpp             # Fixed timestep / accumulator
│   │   └── Config.hpp            # Tuning constants, no magic numbers
│   ├── world/
│   │   ├── Planet.hpp/cpp        # Spherical terrain mesh generation
│   │   ├── Heightmap.hpp/cpp     # Fractal noise, diamond-square
│   │   ├── TerrainChunk.hpp/cpp  # LOD, chunk culling
│   │   └── SkyDome.hpp/cpp       # Gradient sky, colour by time-of-day
│   ├── entity/
│   │   ├── Entity.hpp            # Base class: position, velocity, health
│   │   ├── Player.hpp/cpp        # Craft physics, input
│   │   ├── Enemy.hpp/cpp         # Enemy variants + factory
│   │   ├── Friendly.hpp/cpp      # Ground units to protect
│   │   ├── Projectile.hpp/cpp    # Bullet, missile, beam
│   │   └── EntityManager.hpp/cpp # Flat pool + spatial grid
│   ├── weapon/
│   │   ├── WeaponSystem.hpp/cpp  # Player weapon slots, firing logic
│   │   ├── WeaponTypes.hpp       # Enum + stats table
│   │   ├── AutoTurret.hpp/cpp    # Independent targeting subsystem
│   │   ├── MissileGuidance.hpp   # Proportional navigation
│   │   └── UpgradeManager.hpp    # Pickup processing, slot assignment
│   ├── shield/
│   │   ├── ShieldSystem.hpp/cpp  # Directional sectors, recharge
│   │   └── ShieldRenderer.hpp    # Pulse shader, hit flash
│   ├── ai/
│   │   ├── AIController.hpp/cpp  # State machine base
│   │   ├── EnemyAI.hpp/cpp       # Fighter, bomber, swarm variants
│   │   └── SpatialGrid.hpp/cpp   # Broad-phase queries for AI
│   ├── renderer/
│   │   ├── SceneRenderer.hpp/cpp # Orchestrates all 3D draw calls
│   │   ├── ParticleSystem.hpp    # Explosion, exhaust, shield sparks
│   │   └── PostFX.hpp            # Scanline vignette, bloom (optional)
│   ├── hud/
│   │   ├── HUD.hpp/cpp           # Orchestrates all 2D overlay elements
│   │   ├── Radar.hpp/cpp         # Minimap — friend/foe/projectile
│   │   ├── WeaponDisplay.hpp     # Active slot, ammo, cooldown bars
│   │   └── ShieldDisplay.hpp     # Directional shield sectors (pie)
│   ├── audio/
│   │   └── AudioManager.hpp/cpp  # Pooled sound channels, positional audio
│   └── wave/
│       ├── WaveManager.hpp/cpp   # Spawn scheduling, escalation
│       └── WaveDef.hpp           # Data-driven wave definitions
└── tests/                        # Catch2 unit tests for non-rendering systems
```

---

## 3. Core Game Loop

Fixed-timestep physics with variable rendering — the standard pattern:

```
┌─────────────────────────────────────────────────────────┐
│  GameLoop                                               │
│                                                         │
│  accumulator += frame_time                              │
│  while accumulator >= FIXED_DT (1/120s):                │
│      Input::poll()                                      │
│      Player::update(FIXED_DT)                           │
│      EntityManager::update(FIXED_DT)   ← AI, physics   │
│      WeaponSystem::update(FIXED_DT)    ← fire, cooldown │
│      AutoTurret::update(FIXED_DT)      ← independent   │
│      ShieldSystem::update(FIXED_DT)    ← recharge       │
│      WaveManager::update(FIXED_DT)     ← spawning       │
│      CollisionSystem::resolve()                         │
│      accumulator -= FIXED_DT                            │
│                                                         │
│  alpha = accumulator / FIXED_DT       ← interpolation  │
│  SceneRenderer::draw(alpha)                             │
│  HUD::draw()                                            │
└─────────────────────────────────────────────────────────┘
```

Physics at 120 Hz decoupled from render rate gives smooth motion on 60 or 144 Hz displays without duplicating logic.

---

## 4. World / Planet System

### Terrain Generation
The original used a fractal height field. Modern approach:

- **Diamond-Square** algorithm for the base heightmap — fast, deterministic with a seed, produces the right "alien landscape" character
- Stored as a `float[]` grid, baked into a flat-shaded mesh
- Vertex colours assigned by height band (ocean blue → sand → rock → snow), with slight random variation per face to break uniformity
- **Flat shading** is enforced by duplicating vertices per triangle (no shared normals) — gives the original's polygonal look without a custom shader being strictly necessary, though a face-normal shader is cleaner

### Planet Curvature
Rather than a true sphere (complex LOD, seams), use a **large planar grid that visually curves**:
- Camera sits at a fixed altitude above a reference point on the grid
- Far vertices are gradually depressed along a cosine curve relative to camera distance
- Gives the illusion of flying over a curved world without spherical geometry complexity
- Draw distance can be dramatically increased over the original (the original was fog-limited)

### LOD / Chunking
- Divide the heightmap into NxN chunks (e.g. 16x16 tiles of 32x32 quads each)
- Chunks at distance > threshold use a coarser mesh (every-other-vertex)
- Frustum cull chunks before upload

---

## 5. Entity System

### Design: Flat Pool with Type Tags (not ECS, not deep inheritance)
A full ECS is overkill for this game's entity count (<500 live entities). Use a **typed flat pool**:

```cpp
// Entity.hpp
struct Entity {
    Vector3     position;
    Vector3     velocity;
    Quaternion  orientation;
    float       health;
    float       maxHealth;
    EntityType  type;      // PLAYER, ENEMY_FIGHTER, ENEMY_BOMBER, FRIENDLY, PROJECTILE, TURRET
    uint32_t    id;
    bool        alive;
};
```

`EntityManager` holds `std::vector<Entity>` pools per type for cache-friendliness, with a spatial grid for O(1) neighbour queries (used heavily by AI and collision).

### Player Craft
- Hovercraft-style physics: thrust along forward vector, drag applied each tick
- **Banking**: roll angle driven by lateral velocity (cosmetic, affects feel enormously)
- Altitude: player maintains a minimum height above terrain via a downward raycast each tick
- Input: keyboard + gamepad (raylib handles both)

### Enemy Variants
| Type | Behaviour | Threat |
|---|---|---|
| Fighter | Pursuit → strafe → evade | Player craft |
| Bomber | Low-altitude run toward friendly units | Ground units |
| Swarm Drone | Simple flocking toward player | Player craft (numbers) |
| Carrier | Hangs at altitude, spawns drones | Spawn source |
| Ground Turret | Stationary, rotates to track player | Player craft |

### Friendly Units
- Move slowly across terrain between fixed waypoints
- Emit a distress signal (HUD indicator) when under attack
- Destroying all friendly units ends the game (same as original)
- Types: Collector (scores points), Repair Station (restores player health on proximity), Radar Booster (extends radar range while alive)

---

## 6. Weapon System

### Weapon Slots
Player has **three weapon slots**: Primary, Secondary, Special.

```cpp
enum class WeaponType {
    // Primary
    Cannon,         // Rapid-fire, unlimited ammo, low damage
    PlasmaCannon,   // Slower, splash damage, medium ammo
    BeamLaser,      // Continuous ray, drains energy cell
    // Secondary
    Missile,        // Single homing, limited ammo
    ClusterMissile, // Splits into 4 on proximity
    DepthCharge,    // Drops downward, area effect vs ground
    // Special
    AutoTurret,     // Attached to hull, independent AI targeting
    ShieldBooster,  // Temporary absorb (consumes slot action)
    EMP,            // Area stun, no kill — stops enemy weapons temporarily
};
```

Upgrades are picked up from destroyed enemies (random drop) or from Collector friendly units reaching a base. Each pickup either unlocks a new weapon type or levels up the current slot (e.g. Cannon → Plasma Cannon → Beam Laser progression).

### Auto-Turret Subsystem
This runs as an **independent update** in the game loop, separate from the player's firing input:

```
AutoTurret::update(dt):
    if cooldown > 0: cooldown -= dt; return
    target = SpatialGrid::nearest_enemy(turret_world_pos, MAX_TURRET_RANGE)
    if target:
        aim turret toward target (max rotation rate per second)
        if angle_to_target < FIRE_CONE:
            spawn projectile
            cooldown = TURRET_FIRE_RATE
```

The turret is parented to the player's transform (moves with the ship) but has its own orientation. Visual: a small rotating cannon model attached to the ship's dorsal surface.

### Missile Guidance
**Proportional Navigation** — realistic, simple to implement:
```
closing_velocity = dot(relative_velocity, LOS_unit_vector)
LOS_rate = cross(relative_pos, relative_vel) / |relative_pos|²
acceleration = N * closing_velocity * LOS_rate   // N ≈ 3–5
```
Gives convincing homing that can be evaded by sharp turns at close range.

---

## 7. Shield System

### Directional Sectors
The shield is divided into **4 quadrants**: Front, Rear, Left, Right — each with its own HP pool and recharge state.

```cpp
struct ShieldSystem {
    float sectorHP[4];      // indexed by ShieldSector enum
    float sectorMax;        // same for all sectors (upgradeable)
    float rechargeRate;     // HP/sec (upgradeable)
    float rechargeDelay;    // seconds after last hit before recharge begins
    float timeSinceHit[4];  // per-sector
};
```

On collision, the hit direction relative to the player's forward vector determines which sector absorbs it. A fully depleted sector passes damage through to hull HP.

### Visual
- raylib shader: a translucent hemisphere around the ship, UV-animated pulse ripple on hit
- HUD element: a small pie/sector diagram in the corner showing each quadrant's fill level, colour-coded (green → yellow → red)

---

## 8. Radar System

The radar is one of the most important feel improvements over the original:

```
┌─────────────────┐
│    RADAR        │
│                 │
│   ·  (green)    │  ← Friendly unit
│         ●       │  ← Player (centre)
│    ○  ○         │  ← Enemies (red)
│  ·              │  ← Projectile (yellow)
│                 │
│  [altitude bar] │  ← Vertical strip showing targets above/below
└─────────────────┘
```

- Range is a tunable constant, extendable by the Radar Booster friendly unit surviving
- Enemies blink faster the closer they are
- An **altitude strip** on the side of the radar circle shows whether threats are above or below the player's current altitude — critical for the game's 3D combat feel
- Direction of radar north is always the player's current heading (ego-centric), not world-north

---

## 9. AI System

### State Machine Per Enemy
```
IDLE ──(player enters range)──► PURSUE
PURSUE ──(within attack range)──► ATTACK
ATTACK ──(player evades / low health)──► EVADE
EVADE ──(sufficient distance)──► PURSUE
ATTACK ──(target is friendly)──► STRAFE_FRIENDLY   [Bomber type]
```

### Flocking (Swarm Drones)
Three standard rules: **separation, alignment, cohesion** — with player as an additional attraction vector. Very cheap, very effective for producing the swarming feel.

### Spatial Grid
A 2D grid overlaid on the terrain (ignoring altitude) with cell size = `MAX_ENEMY_ATTACK_RANGE`. Each `EntityManager::update()` tick rebuilds the grid (cheap at entity counts < 500). Used by:
- AI for nearest-player / nearest-friendly queries
- AutoTurret for nearest-enemy
- Collision broad phase

---

## 10. Renderer

### Draw Order
```
1. SkyDome (depth write off)
2. Planet terrain chunks (opaque, front-to-back)
3. Entities — opaque meshes
4. Projectiles — additive blended quads / cylinders
5. Particle effects (explosion, exhaust) — sorted back-to-front
6. Shield meshes — translucent, additive
7. [Post-FX pass — optional: bloom, scanline vignette]
8. HUD — 2D overlay, depth off
```

### Flat Shading
The "modernised but stylistically similar" target is best hit with:
- Flat-shaded polygon terrain (per-face colours, no interpolation)
- **One directional light** (the "sun") with no specular — purely diffuse
- A subtle **ambient term** (0.15–0.25) so shadow faces aren't black
- Mild fog that matches sky colour at max draw distance — hides LOD transitions cleanly

This is achievable in a raylib custom shader with ~40 lines of GLSL.

### Particle System
Simple CPU-side particle pool (pre-allocated, ~2000 particles):
```cpp
struct Particle {
    Vector3 pos, vel;
    Color   colour;
    float   life, maxLife, size;
    bool    alive;
};
```
Rendered as camera-facing quads. Emitters for: engine exhaust, weapon muzzle flash, explosion burst, shield impact sparks, missile smoke trail.

---

## 11. Wave Manager

Data-driven wave definitions (could be JSON or a simple header-defined table):

```cpp
struct WaveDef {
    int          waveNumber;
    int          enemyCount;
    EnemyType    types[MAX_TYPES];   // mix ratios
    float        spawnInterval;      // seconds between spawns
    bool         hasCarrier;
    bool         hasGroundTurrets;
    float        aggressionMultiplier;
};
```

Escalation: every N waves, a "boss wave" spawns a Carrier plus a full fighter escort. Between waves, a short grace period allows shield recharge and rewards inspecting the radar for remaining threats.

---

## 12. Audio

raylib's built-in audio (miniaudio) is sufficient. Key design points:
- **Positional audio** via manual pan/volume calculation based on world-space distance and angle to listener — raylib doesn't do 3D audio natively, but the calculation is trivial
- Pre-load all sounds at startup into a sound pool — no runtime loading
- 4 simultaneous channels for weapon fire (prevents cutoff on rapid fire)
- Distinct audio signatures per weapon type — critical for feel

---

## 13. Configuration & Tuning

All gameplay constants live in `Config.hpp` as `constexpr` values — no magic numbers in logic code. This makes tuning fast without recompilation being the primary loop (you can add a live-reload JSON layer later).

Key tuning constants:
```cpp
constexpr float PLAYER_THRUST          = 28.0f;
constexpr float PLAYER_DRAG            = 0.92f;
constexpr float PLAYER_MAX_SPEED       = 40.0f;
constexpr float PLAYER_BANK_RATE       = 3.5f;
constexpr float SHIELD_RECHARGE_RATE   = 8.0f;   // HP/sec
constexpr float SHIELD_RECHARGE_DELAY  = 3.0f;   // sec
constexpr float TURRET_RANGE           = 60.0f;
constexpr float TURRET_FIRE_RATE       = 0.4f;   // sec
constexpr float MISSILE_NAV_CONSTANT   = 4.0f;
constexpr float RADAR_BASE_RANGE       = 300.0f;
constexpr float TERRAIN_CURVATURE      = 0.00015f;
```

---

## 14. Build & Development Phasing

### Phase 1 — Foundation (no gameplay yet)
- CMake setup, raylib linked
- Planet terrain generates and renders (flat shaded)
- Player craft moves over it with correct banking and altitude clamping
- Camera follows with lag

### Phase 2 — Core Combat
- Enemy fighters spawn and pursue
- Cannon fires, collision resolves, enemies die
- Friendly units on ground, wave manager triggers next wave on clear

### Phase 3 — Weapons & Upgrades
- Missile guidance
- Weapon slot system, drop pickups
- Auto-turret as separate update

### Phase 4 — Shield & HUD
- Directional shield with visual
- Full radar with altitude strip
- Weapon display, health bar

### Phase 5 — Polish
- All enemy variants (bomber, swarm, carrier, ground turret)
- Particle effects pass
- Audio pass
- Config tuning

### Phase 6 — Extended Features
- Day/night cycle
- Weather (wind affecting handling, reduced visibility)
- Leaderboard / scoring persistence
- Optional: cockpit camera mode

---

## 15. Key Design Principles

- **No heap allocation in the hot path** — entity pools, particle pools, projectile pools are all pre-allocated at startup
- **Data-oriented where it matters** — entity update loop iterates flat arrays, not pointer-chasing polymorphism
- **Separation of update and render** — nothing in the physics tick touches a raylib draw call; nothing in the render pass mutates game state
- **Determinism** — the physics tick at fixed 120 Hz with no `rand()` in logic paths (use a seeded `std::mt19937`) enables reproducible bug reports and future replay recording
