# terra-siege — Rebuild Design Documents Guide

## Purpose of This File

This document explains what each design document in the repo root is, what it
covers, how the documents relate to each other, and in what order to read and
implement them. Read this file first before opening any of the others.

---

## The Design Document Set

The following documents were written during a design session to plan a significant
rebuild of terra-siege before Phase 3 combat is implemented. They supersede or
extend the information in CLAUDE.md where there is overlap. CLAUDE.md remains the
primary project context document — these rebuild documents are the detailed
implementation specs for specific systems.

```
readme_rebuild.md          ← You are here. Read this first.
CLAUDE.md                  ← Primary project context. Read second.
flight_mode_rebuild.md     ← Flight physics rebuild spec.
terrain_rebuild.md         ← Terrain generation rebuild spec.
combat_tuning.md           ← Combat balance framework and weapon specs.
camera_system.md           ← Five-view camera system spec.
radar_system.md            ← Radar display system spec.
```

---

## Document Summaries

### flight_mode_rebuild.md
**What it covers:** Complete replacement of the current dual flight model system
(Classic + Arcade with five craft types) with a single well-tuned Newtonian
physics model faithful to the original Virus/Zarch.

**Key decisions documented:**
- Everything being removed (FlightMode enum, CraftType enum, all Arcade/Classic
  constants, dual input handlers, dual physics pipelines, F5/F6 dev keys)
- The Newtonian physics model — thrust on local UP axis, constant gravity, near-zero
  drag, mouse pitch/yaw control. The "tilt and burn" feel of the original.
- All new Config::NEWTON_* constants with values and rationale
- Landing and crash system (impact speed thresholds, attitude check on landing)
- Flight ceiling (above NEWTON_FLIGHT_CEILING thrust cuts out)
- Flight assist at four levels layered on top of the physics (not an alternative model)
- Fuel system (NEWTON_FUEL_MAX, NEWTON_FUEL_BURN_RATE)
- Simplified Player.hpp shape after the rebuild
- One hovercraft mesh (the existing Saucer geometry is the starting point)
- Simplified HUD — fuel gauge added, mode/craft labels removed
- Simplified dev keys — F5 (craft) and F6 (mode) removed
- Implementation order: gut Player.hpp → rewrite Player.cpp → strip Config.hpp →
  simplify GameState.cpp

**When to implement:** This is the first rebuild task. Do it before terrain,
combat, or cameras. Everything else depends on having a working flight model.

**What it does NOT cover:** Enemy flight behaviour (covered in combat_tuning.md),
camera behaviour (covered in camera_system.md).

---

### terrain_rebuild.md
**What it covers:** Replacement of the Diamond-Square heightmap generator with
Fourier synthesis (sum of sine waves) inspired by and improved upon David Braben's
original Zarch/Virus terrain system.

**Key decisions documented:**
- Why sine wave generation: infinite tiling, no artificial ocean border, naturally
  smooth (no smoothing passes needed), faster to generate than D-S + 12 smooth passes
- The three improvements over Braben's original:
  1. Seed-driven phase offsets (different terrain every game — original was fixed)
  2. Multi-octave structure (Continental + Regional + Local = 16 terms vs Braben's 6)
  3. Irrational frequency ratios using φ, √2, √3, √5, √7, √11 — eliminates visible
     tiling within any playable world size
  4. Domain warping — organic coastlines and ridgelines
  5. Rivers and lakes preserved on top of sine terrain
- Complete code for `SineWaveTerm` struct, `buildSineTerms()`, `sineWaveGenerate()`
- Updated `generate()` pipeline (what's removed, what stays)
- Updated `sample()` for toroidal wrapping (modular coordinates)
- Updated `Planet::heightAt()` for toroidal wrapping
- `Player::wrapPosition()` — call at end of `applyPhysics()` each tick
- All new Config constants with values
- Updated terrain colour bands (remapped for sine height distribution, no falloff)
- Performance notes (~60-90ms generation vs ~200ms for D-S + 12 smooth passes)
- Implementation order: Config → buildSineTerms/sineWaveGenerate → remove old
  methods → update generate() → update sample() → update heightAt() → wrap player →
  tune SEA_LEVEL and colours

**When to implement:** Second rebuild task, after flight model. The terrain
generation is independent of the flight model but the wrapping system requires
knowing the world size, which is set in Config.

**What it does NOT cover:** Terrain objects (Phase 3.5), terrain rendering
(TerrainChunk is unchanged), river/lake implementation (unchanged — same code
runs on sine terrain).

**Critical note:** `smooth()` and `applyRadialFalloff()` are REMOVED entirely.
Do not retain them. The sine generator produces smooth terrain natively and has
no edges to taper.

---

### combat_tuning.md
**What it covers:** The complete combat balance framework — TTK targets, HP
derivation formulas, all nine weapon specifications, missile system design,
enemy shield parameters, and the playtesting process.

**Key decisions documented:**
- **Time-to-kill budget** as the anchor for all HP values. TTK targets are the
  design decision; HP values are derived arithmetic. Never adjust HP values
  directly — adjust TTK targets and re-derive.
- Enemy HP values derived from TTK × Cannon DPS (100 DPS reference):
  Drone=8HP, Seeder=50HP, Fighter=200HP, Bomber=500HP, Carrier=2500HP, Turret=400HP
- Shield fractions per enemy type and recharge parameters
- All nine weapon specs with damage, fire rate, DPS, ammo counts, and tactical purpose
- **Missile system — Option B (round-start selection):** Player chooses Standard or
  Cluster at wave start via selection UI. No mid-wave swapping. No pickup dependency.
  The wave composition (radar report) is shown so the choice is informed.
- Standard Missile: N=3.5, 2.8 rad/s turn — evadable by sharp turns, best vs Bombers
- Cluster Missile: N=6.0, 5.5 rad/s submunition turn — very hard to shake, best vs
  Drones and Fighter groups. Parent splits into 4 submunitions at proximity trigger.
  Each submunition independently homes within a 120° search cone.
- Weapon/target matrix (★★★ optimal to ✗ counterproductive for all 8 weapons vs 6
  enemy types)
- Why the Beam Laser exists: suppresses Carrier shield regeneration via continuous
  damage (setting timeSinceHit=0 each tick while beam is on target)
- Why the Depth Charge exists: only reliable weapon against Ground Turrets from
  altitude given the Newtonian flight model
- Player survivability numbers — how long each enemy takes to strip one shield sector
- Five-step playtesting process with specific adjustment heuristics
- Phase 3 implementation checklist

**When to implement:** Read during Phase 3 planning. Implement alongside enemy
entities and the weapon system. The checklist at the bottom of the document is
the Phase 3 task list.

**What it does NOT cover:** Enemy AI behaviour (that is in the AI system, TBD),
radar integration (covered in radar_system.md).

**Critical note:** The missile round-start selection UI must show the wave
composition before the player selects. This requires WaveManager to expose the
upcoming wave's enemy count and types before spawning begins.

---

### camera_system.md
**What it covers:** Five distinct camera views selectable with keys 1–5, replacing
the current single follow-camera system.

**Key decisions documented:**
- Five views and their tactical purposes:
  1. Chase (key 1) — follows ship nose, default combat view
  2. Velocity (key 2) — follows velocity vector, terrain avoidance
  3. Tactical (key 3) — fixed overhead, north always up, battlefield awareness
  4. Threat-lock (key 4) — rotates to keep nearest enemy in frame
  5. Classic (key 5) — fixed diagonal-down, original Virus perspective
- Complete implementation code for all five `updateXxxCamera(dt)` methods
- `CameraView` enum (NOT `CamMode` — that already exists for Follow/FreeRoam dev toggle)
- View switching input handler and dispatcher
- 2-second fade-out label on view switch
- Velocity camera degenerate case: blends to chase cam when speed < 5 units/sec
- Threat-lock camera: hysteresis (20% score gap to switch target), 90°/sec max
  rotation rate, threat score formula (damage multiplier / distance)
- Tactical camera: `camera.up = {0, 0, 1}` — CRITICAL, not `{0, 1, 0}`. Wrong
  value causes the overhead view to rotate randomly.
- Classic camera: world-fixed offset (not nose-relative), CLASSIC_CAM_OFFSET_X/Z
  and CLASSIC_CAM_ALTITUDE need tuning against actual world scale
- Ship direction arrow overlay for Tactical view (nose direction indicator)
- Compass rose overlay for Classic view
- All new Config constants
- Radar integration notes: Tactical view hides disc and shows altitude strip only;
  Classic view scales disc up 20%
- Implementation checklist

**When to implement:** After flight model rebuild. Camera system depends on having
correct player yaw, pitch, roll, velocity, and position accessors on Player.
Threat-lock (view 4) is a stub until Phase 3 enemies exist — it falls back to
Chase behaviour.

**What it does NOT cover:** HUD layout beyond camera-specific overlays (that is
in the HUD system, TBD), radar disc rendering (covered in radar_system.md).

**Critical naming note:** The enum is `CameraView`, not `CameraMode` (raylib
typedef collision) and not `CamMode` (already exists for dev toggle). All three
names would compile but only `CameraView` is correct.

---

### radar_system.md
**What it covers:** The full tactical radar display system in three implementation
tiers across Phases 4 and 5.

**Key decisions documented:**
- Physical layout: 120px disc bottom-right, 16px altitude strip beside it
- Three implementation tiers:
  - **Tier 1 (Phase 4 baseline):** disc, IFF colours, blip shapes by enemy type,
    proximity pulse, altitude strip, range rings. Mandatory before combat ships.
  - **Tier 2 (Phase 4 complete):** threat vector arrows, inbound missile warning
    ring, zone overlay (bases/pads), Carrier drone count label, screen-edge
    missile direction indicator
  - **Tier 3 (Phase 5 polish):** ghost blips for lost contacts, radar jamming
    near Carriers, radar boost visual, camera-view-aware sizing
- Complete `Radar` class declaration (`src/hud/Radar.hpp`)
- `RadarContact` and `GhostBlip` structs
- `worldToRadar()` coordinate transform — rotates by -playerYaw so player heading
  is always up on the radar disc
- All draw helper methods with complete raylib implementation code
- IFF colour table, blip shape table, altitude tinting formula
- Ghost blip pool: 32 pre-allocated slots (no heap allocation)
- Jamming jitter: `sin(id * constant + time)` for smooth deterministic interference
- Radar boost: disc border brightens, disc briefly expands 15%, BOOST label
- Integration with camera views: Tactical view shows altitude strip only (disc is
  redundant when looking straight down); Classic view scales disc up 20%
- Screen-edge missile direction indicator (companion to warning ring)
- Phase 4 and Phase 5 implementation checklists

**When to implement:** Phase 4. The Radar class replaces the empty stub in
`src/hud/Radar.hpp/cpp`. Tier 1 should be implemented before any combat wave
testing — without altitude information the game is genuinely unfair to the player.

**What it does NOT cover:** HUD layout (shield display, weapon slots, health bar —
those are separate HUD components), audio cues for radar events (Phase 5).

---

## How the Documents Relate to Each Other

```
flight_mode_rebuild.md
    │
    │ depends on nothing
    │ implement first
    ↓
terrain_rebuild.md
    │
    │ depends on world size from Config (set in flight rebuild)
    │ implement second
    ↓
camera_system.md
    │
    │ depends on Player accessors (yaw, pitch, roll, velocity, position)
    │ Threat-lock (view 4) is a stub until Phase 3
    │ implement after flight model
    ↓
combat_tuning.md
    │
    │ Phase 3 task list — enemies, weapons, wave manager
    │ read during Phase 3 planning
    │ implement third
    ↓
radar_system.md
    │
    │ Phase 4 — depends on EntityManager (Phase 3)
    │ Tier 1 must be done before wave testing begins
    └ implement fourth
```

---

## Implementation Priority Order

Given the current state of the project (Phase 2 partial, no enemies, camera
rebuild needed), the recommended order is:

### Step 1 — Flight Model Rebuild (do this first)
Follow `flight_mode_rebuild.md`. Gut the dual model system and replace with
the single Newtonian model. Verify feel at all four assist levels before
proceeding. The rest of the game is built on this.

### Step 2 — Terrain Rebuild (do this second)
Follow `terrain_rebuild.md`. Replace Diamond-Square with sine wave generation.
Tune SEA_LEVEL and colours until terrain looks right. Verify world wrapping
works by flying off the map edge. Rivers and lakes should work unchanged.

### Step 3 — Camera System (do alongside or after terrain)
Follow `camera_system.md`. Implement all five views. Views 1, 2, 3, 5 can be
fully tested immediately. View 4 (Threat-lock) stubs to Chase until enemies exist.

### Step 4 — Phase 2 Completion
With flight model and terrain rebuilt and cameras working:
- Gamepad support in Player::handleInput()
- Engine exhaust particle emitter
- Ship ground shadow
- Commit Phase 2 complete

### Step 5 — Phase 3 Combat (read combat_tuning.md before starting)
- Entity.hpp base struct, EntityManager flat pools, SpatialGrid
- Fighter enemy, Cannon weapon, basic collision, WaveManager
- Expand to full enemy roster and weapon set using combat_tuning.md as reference
- Missile round-start selection UI

### Step 6 — Phase 4 HUD and Radar (read radar_system.md before starting)
- Radar Tier 1 before any wave testing
- Shield system (directional sectors)
- Full HUD layout (weapon slots, shield display)
- Radar Tier 2

### Step 7 — Phase 5 Polish
- Radar Tier 3
- Particle system
- Audio
- Day/night, weather

---

## What CLAUDE.md Still Covers

CLAUDE.md remains the authoritative source for:
- Project overview and GitHub URL
- Technology stack and build commands
- Full project file structure
- Core architecture rules (fixed timestep, no heap alloc in hot path,
  separation of update/render, procedural geometry only)
- Coordinate system (+Z = north, +X = east, +Y = up)
- Current build state (what is implemented, what is stub)
- Coding standards
- The CamMode enum (Follow/FreeRoam dev toggle — distinct from CameraView)
- Camera terrain clamping (do not remove this)

The rebuild documents extend CLAUDE.md — they do not replace it. When there is
a conflict between CLAUDE.md and a rebuild document, **the rebuild document takes
precedence** as it represents more recent design decisions.

---

## Naming Collisions to Watch For

These are known naming hazards in the codebase:

| Name | Problem | Correct Name |
|------|---------|-------------|
| `CameraMode` | raylib typedef — compile error | `CamMode` or `CameraView` |
| `CamMode` | Already exists for Follow/FreeRoam dev toggle | Use for that only |
| `CameraView` | New enum for the five player views | Use this |
| `TERRAIN_ROUGHNESS` | Removed in terrain rebuild | No replacement — sine has no roughness |
| `smooth()` | Removed in terrain rebuild | Do not re-add |
| `applyRadialFalloff()` | Removed in terrain rebuild | Do not re-add |
| `FlightMode` | Removed in flight rebuild | No replacement |
| `CraftType` | Removed in flight rebuild | No replacement |

---

## Notes for Claude Code

- Read `readme_rebuild.md` (this file) and `CLAUDE.md` before opening any other file.
- The five design documents are implementation specs, not suggestions. Follow them.
- When a document says "tuning needed" (e.g. CLASSIC_CAM_OFFSET values), implement
  the stated starting values and flag them with a `// TUNE` comment for playtesting.
- The Phase 3 checklist in `combat_tuning.md` and the implementation checklists in
  `camera_system.md` and `radar_system.md` are the task lists for their respective
  phases. Work through them in order.
- Do not implement Phase 4 radar until Phase 3 EntityManager exists — the Radar
  class depends on entity pool queries.
- Do not skip Radar Tier 1 before wave testing — without the altitude strip the
  game is genuinely unfair.
- The Beam Laser shield suppression (`timeSinceHit = 0` while beam on target) is
  a small detail with major balance implications. Do not omit it.
- The Cluster Missile 120° search cone on submunitions prevents all four targeting
  the same enemy. Do not change this to full-sphere search — it breaks the weapon's
  swarm-killing identity.

