# terra-siege — Rebuild Roadmap

This document is the **execution plan** for the five rebuild design specs in this
folder. The specs (`flight_mode_rebuild.md`, `terrain_rebuild.md`, `camera_system.md`,
`combat_tuning.md`, `radar_system.md`) describe the target systems. This file
sequences the work, captures dependencies, lists what gets deleted, defines
acceptance criteria, and flags the recurring traps we've hit during development.

Read this **after** `readme_rebuild.md` and `CLAUDE.md`. Use it as the running
checklist — every step here maps to one or more checkboxes that must be ticked
before moving on.

---

## Current State Snapshot

**Branch / commits:** `main` at `249e105` ("Newtonian Classic flight mode + Saucer
craft"). Uncommitted work in tree: bigger terrain, loading screen, Q/E swap,
helicopter physics fix, FlightMode/CraftType plumbing for the dual model.

**What exists today (post-session, uncommitted):**
- Diamond-Square terrain at 2049² (32×32 chunks, 8192×8192 world)
- Loading-screen progress bar (Planet::generate takes ProgressCb)
- Far clip extended to 3000 via rlFrustum override in render
- Player has dual physics (Classic + Arcade), 5 craft (Delta, Forward Swept, X-36, YB-49, Saucer)
- F1/F2/F4 dev keys; F5 (craft) and F6 (mode) added this session — both go away
- Flight recorder (F4) writing to `tests/logs/flight-recording.csv`

**Decision:** before touching the rebuild, **commit the current session's work
to a checkpoint commit** so we have a clean rollback point. Then start the
rebuild work on a feature branch (`rebuild/flight` is a sensible first one).

---

## Sequencing Overview

```
┌─────────────────┐
│ Step 0          │  Prep: commit current work, branch, decide preserve/delete
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 1          │  Flight rebuild — single Newtonian model, one ship mesh
│ flight_mode...  │  REMOVES: FlightMode, CraftType, ARCADE_*, CLASSIC_*, F5/F6
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 2          │  Terrain rebuild — sine waves, toroidal wrap, no falloff
│ terrain_rebuild │  REMOVES: diamondSquare, smooth, applyRadialFalloff
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 3          │  Five camera views (1–5), threat-lock stubs to chase
│ camera_system   │  ADDS: CameraView enum (NOT CameraMode, NOT CamMode)
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 4          │  Phase 2 completion: gamepad, exhaust, ground shadow
└────────┬────────┘
         │  ───── Phase 2 complete; commit, tag, party ─────
┌────────▼────────┐
│ Step 5          │  Phase 3 combat: entities, weapons, waves, missile selection
│ combat_tuning   │  HP is TTK-derived — never edit HP constants directly
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 6          │  Phase 4 HUD: Radar Tier 1 → Tier 2, shields, weapon display
│ radar_system    │  Tier 1 is MANDATORY before any wave testing
└────────┬────────┘
         │  ───── Phase 4 complete ─────
┌────────▼────────┐
│ Step 7          │  Phase 5 polish: Radar Tier 3, particles, audio, weather
└────────┬────────┘
         │
┌────────▼────────┐
│ Step 8          │  Phase 6 extended: leaderboard, cockpit cam, replay
└─────────────────┘
```

---

## Step 0 — Pre-flight (do this first, takes 10 minutes)

- [ ] Commit current uncommitted work as a checkpoint (`chore: pre-rebuild
      checkpoint`). This preserves the "best of both worlds" version we just
      built so we can compare feel against the rebuild as we go.
- [ ] Track `terra_rebuild/` in git (currently `??` in status).
- [ ] Create branch `rebuild/flight` for Step 1.
- [ ] Read these three files in order: `CLAUDE.md` (the rebuild copy in
      `terra_rebuild/CLAUDE.md`), `readme_rebuild.md`, `flight_mode_rebuild.md`.
      Do not skim — the rebuild specs override CLAUDE.md where they conflict.

---

## Step 1 — Flight Rebuild

**Spec:** `flight_mode_rebuild.md`
**Branch:** `rebuild/flight`
**Estimated effort:** 1 dedicated session.

### Removal Manifest

Delete from `Player.hpp`:
- `enum class FlightMode`, `enum class CraftType`, `flightModeName()`, `craftName()`
- `setCraft()`, `setFlightMode()`, `flightMode()`, `craft()`
- `targetSpeed()`, `boosting()`, `m_targetSpeed`, `m_currentSpeed`, `m_boosting`,
  `m_craftType`, `m_flightMode`, `m_rollRate`, `m_pitchRate`
- All `handleArcadeInput`, `handleClassicInput`, `applyArcadePhysics`,
  `applyClassicPhysics` declarations

Delete from `Player.cpp`:
- All four input/physics implementations (replaced by one each)
- All five mesh builders except the Saucer (rename to `buildHovercraftMesh`)
- The dispatcher block in `update()` that selects between Arcade and Classic

Delete from `Config.hpp`:
- Every `ARCADE_*` constant (~26 of them)
- Every `CLASSIC_*` constant (~20 of them)
- Speed-dependent camera constants (`ARCADE_CAM_*`)

Delete from `GameState.cpp`:
- F5 (craft cycle) and F6 (mode toggle) input handling
- `MODE:` and `CRAFT:` HUD lines
- Mode-aware speed/distance camera lerp logic

### Add

- New `NEWTON_*` constants block in `Config.hpp` (values per spec)
- Single `Player::handleInput()`, `Player::applyPhysics()`, `Player::buildMesh()`
- Fuel system: `m_fuel`, `fuel()`, fuel burn while thrusting, fuel-empty cuts thrust
- Landing/crash logic per spec (impact-speed and attitude checks)
- Flight ceiling: thrust cuts above `NEWTON_FLIGHT_CEILING` AGL
- `Player::wrapPosition()` declaration (called by terrain step — leave the
  call in for now, it's a no-op until Step 2)
- Flight assist as **post-physics correction layer**, not as alternate physics
- Fuel gauge in HUD next to hull bar; LANDED indicator

### Acceptance Criteria

- [ ] Build is clean (no unused warnings, no dead `// removed` comments)
- [ ] At assist Raw (0): tilt-and-burn feels authentic — easy to crash on landing
- [ ] At assist Full (3): terrain pull-up engages and saves the player
- [ ] Fuel runs out in ~30s of continuous thrust at default `NEWTON_FUEL_BURN_RATE`
- [ ] Soft landing on flat ground at <3 m/s vertical velocity refuels (if at pad
      — pad system will be Phase 3, so for now simply detect "landed = true")
- [ ] Hard landing at >12 m/s destroys ship
- [ ] Above flight ceiling, thrust audibly cuts (TraceLog for now, audio in Phase 5)
- [ ] Flight recorder still works (F4) — verify CSV columns reflect new state
- [ ] Loading screen still functions (we keep it from current work)

### Risk / Watch For

- **Matrix order:** raylib `MatrixMultiply(A, B)` returns `B*A` in standard math.
  We had this bite us repeatedly. The render uses
  `Mul(Mul(Mul(Rz, Rx), Ry), T)` which evaluates to the standard `T·Ry·Rx·Rz`.
  To compute the local-UP axis in world space, **apply Rx with `-m_pitch`**
  (matching `m_pitchVis`) — not `+m_pitch`. Helicopter convention: nose down →
  thrust tilts forward → fly forward.
- **Pitch input direction:** Classic/helicopter is **stick-forward = nose-down =
  fly-forward**. Mouse-up and W decrement `m_pitch`, not increment. Inverted
  vs flight-sim convention — the spec is explicit about this.
- **Q/E yaw direction:** Q = left (yaw decreases), E = right.
- **Don't reintroduce the dual model accidentally** when copying logic from the
  current Classic implementation. The spec is intentionally simpler — single
  physics path, no smoothing twin-state.

---

## Step 2 — Terrain Rebuild

**Spec:** `terrain_rebuild.md`
**Branch:** `rebuild/terrain` (off `rebuild/flight` after merge)
**Estimated effort:** 1 dedicated session.

### Removal Manifest

Delete from `Heightmap.cpp` / `Heightmap.hpp`:
- `diamondSquare()` — entire function
- `smooth()` — entire function
- `applyRadialFalloff()` — entire function
- The 12-pass smoothing loop in `generate()`

Delete from `Config.hpp`:
- `TERRAIN_ROUGHNESS` (sine has no roughness param)

### Add

- `SineWaveTerm` struct
- `buildSineTerms(seed, &terms)`
- `sineWaveGenerate(seed)` — fills `m_data[]` via Fourier synthesis
- All `SINE_*` constants per spec
- Bump `HEIGHTMAP_SIZE` 2049 → **1025** (and `CHUNK_COUNT` 32 → 16,
  `TERRAIN_SCALE` 4 → 8 to keep ~8192² world). Sine is fast — this is a 4×
  generation speed-up.
- Toroidal wrapping in `Heightmap::sample()` and `Planet::heightAt()`
- `Player::wrapPosition()` body (called from `applyPhysics()`)
- Updated colour bands in `TerrainChunk::landColor()` for sine height distribution

### Keep

- `classifyOcean()`, `carveRivers()`, `floodLakes()` — same code, runs better on
  smooth terrain
- The progress callback in `Planet::generate()` — startup is faster but the
  loading screen still wants its updates
- `TerrainChunk` mesh building, vertex colouring, lighting — unchanged
- The rlFrustum far-clip override at 3000 — still needed

### Acceptance Criteria

- [ ] Generation time logs at <100 ms (was 4.5 s) — sine is fast
- [ ] Two different seeds produce distinctly different landscapes
- [ ] Player can fly off the map edge and reappear on the opposite side with
      no visible seam in the terrain mesh or colouring
- [ ] Rivers and lakes still appear naturally
- [ ] No artificial ocean ring around the playfield
- [ ] `heightAt()` queries with negative or large coordinates return correct
      wrapped values (test with assertion harness or a couple of TraceLog dumps)

### Risk / Watch For

- **Toroidal wrap is everywhere or nowhere** — `Heightmap::sample`,
  `Planet::heightAt`, `Player::wrapPosition`, and any future `terrainObject`
  placement query must all use modular coordinates. One missed callsite shows
  up as a heightmap-edge cliff.
- **Don't keep `smooth()` "just in case".** The spec says delete it. Sine
  terrain is C∞ smooth natively. Keeping the function around invites someone
  (us, future) to call it and crush the high-frequency local octave detail.
- **Frequency-base values are scale-dependent.** If you change `HEIGHTMAP_SIZE`,
  the `SINE_*_BASE_FREQ` values need to scale inversely so spatial periods stay
  in cell-count units, not world-unit units.
- **`fmodf` returns negative for negative inputs.** The wrap helper must add
  `worldSize` after `fmodf` if the result is < 0. Spec has this right; don't
  shortcut it.

---

## Step 3 — Camera System

**Spec:** `camera_system.md`
**Branch:** `rebuild/camera`
**Estimated effort:** 1 dedicated session, plus playtest tuning.

### Add

- `enum class CameraView { Chase=0, Velocity=1, Tactical=2, ThreatLock=3, Classic=4 }`
- `m_cameraView`, `m_viewLabelTimer`, `m_threatCamYaw`, `m_threatLockTarget`
  members on `GameState`
- Five `update<View>Camera(dt)` methods (specs verbatim)
- `handleCameraViewKeys()` — keys 1–5 + 2-second fade label
- `drawCameraViewLabel()` in HUD
- `drawTacticalShipArrow()` overlay (only active in Tactical view)
- Compass rose overlay for Classic view (spec calls this out)
- New camera-related Config: `CAM_DISTANCE`, `CAM_HEIGHT`, `CAM_FOV`, `CAM_LERP`,
  `VELOCITY_CAM_MIN_SPD`, `TACTICAL_CAM_ALTITUDE`, `TACTICAL_CAM_FOV`,
  `THREAT_CAM_MAX_ROT`, `THREAT_CAM_HYSTERESIS`, `CLASSIC_CAM_OFFSET_X/Z`,
  `CLASSIC_CAM_ALTITUDE`, `CLASSIC_CAM_FOV`, `CLASSIC_CAM_LERP`

### Threat-Lock Stub

`updateThreatLockCamera()` falls back to the chase camera path when there is
no `EntityManager` yet (Phase 3). Wire `findHighestThreat()` as a stub
returning `nullptr` until enemies exist.

### Acceptance Criteria

- [ ] Keys 1–5 swap views instantly; 2-second label appears top-centre and fades
- [ ] Tactical view (3): world-north is up; ship arrow indicator visible; camera
      `up = {0, 0, 1}` not `{0, 1, 0}`
- [ ] Velocity view (2): identical to chase at <5 u/s; rotates to velocity
      direction at higher speeds; smoothly blends between
- [ ] Classic view (5): camera lags slowly, never rotates around ship; player
      visible roughly 1/3 from screen bottom
- [ ] Threat-lock view (4): identical to chase with no enemies (Phase 3 stub)
- [ ] All views: terrain-clamping prevents underground camera (preserve the
      existing logic — don't lose this fix)

### Risk / Watch For

- **`CameraMode` is reserved by raylib.** Use `CameraView` for the new enum.
  `CamMode` already exists for the dev Follow/FreeRoam toggle — leave that
  alone. Three names look identical in a hurry; only `CameraView` is correct.
- **Tactical view's `up` vector** — `{0, 0, 1}`, not `{0, 1, 0}`. Wrong value
  causes the overhead view to spin randomly because looking straight down
  with `up = world_up` gives a degenerate cross product. Spec is explicit.
- **Classic view tuning** — the spec gives starting offsets but flags them
  for playtesting against actual world scale. Mark with `// TUNE` and
  iterate after the rest of the system works.

---

## Step 4 — Phase 2 Completion

**Spec:** the existing project-roadmap covers these.
**Branch:** `phase2/finish`

### Tasks

- [ ] **Gamepad** — add `IsGamepadAvailable()` + `GetGamepadAxisMovement()`
      to `Player::handleInput()`. Map left stick to mouse-pitch/yaw, right
      trigger to thrust, A button to primary fire (slot reserved for Phase 3).
- [ ] **Engine exhaust particles** — pre-allocated CPU pool (size 2000),
      orange/yellow billboard quads, 0.3 s lifetime, emitted from rear nacelle
      while thrusting. Scales with thrust intensity.
- [ ] **Ship ground shadow** — dark translucent ellipse on terrain at
      `heightAt(ship.x, ship.z)`. Fades with AGL (transparent above
      `SHADOW_FADE_AGL`). Important for altitude readability — the original
      Virus relied heavily on the shadow.
- [ ] Tag `phase2-complete` and merge to main.

---

## Step 5 — Phase 3 Combat

**Spec:** `combat_tuning.md` (read top to bottom before starting)
**Branch:** `phase3/combat`

### Sequence Within Phase 3

1. **Entity foundation:**
   - `Entity.hpp` base struct (`pos`, `vel`, `hullHP`, `shieldHP`,
     `shieldRechargeDelay`, `shieldRechargeRate`, `timeSinceHit`, `type`,
     `id`, `alive`)
   - `EntityManager` — flat pools per type (`std::vector<Entity>`),
     spatial grid (cell size = `Config::SPATIAL_CELL_SIZE` = 60)
2. **First enemy + first weapon:**
   - Fighter enemy with state machine (IDLE→PURSUE→ATTACK→EVADE)
   - Cannon weapon (100 DPS reference; all other weapons are calibrated to this)
   - Projectile struct, AABB collision
   - `WaveManager` minimum: spawn N fighters, clear, repeat
3. **Full enemy roster** (in `combat_tuning.md` order):
   Drone (8 HP), Seeder (50), Fighter (160 + 40 shield), Bomber (350 + 150),
   Carrier (1500 + 4×250 directional), Ground Turret (400)
4. **Full weapon roster:**
   - Primary tier: Cannon → Plasma Cannon → Beam Laser
   - Secondary: Standard Missile or Cluster Missile
   - Special: Auto Turret / Shield Booster / EMP
5. **Round-start missile selection UI** — radar report shows wave composition,
   player picks Standard or Cluster, FIRE confirms. Locked for the wave.

### Critical Details (don't omit these — spec calls them out explicitly)

- **TTK-derived HP:** all hull HP is computed from TTK targets × Cannon DPS.
  Never edit HP values directly. Adjust TTK in spec, recompute, update Config.
- **Beam Laser shield suppression:** while beam is on a target, set
  `target.timeSinceHit = 0` each tick. This stops Carrier shield regen and
  is the primary Carrier-killing mechanic.
- **Cluster missile 120° submunition cone:** when the parent splits into 4,
  each submunition searches in a 120° forward cone. Do not widen to full
  sphere — full-sphere causes all 4 to home on the same target and breaks
  the swarm-killer identity.
- **Round-start selection, not pickup:** missile type is chosen between waves,
  not collected from drops. Keep it that way.
- **Friendly units on ground:** Collector / Repair Station / Radar Booster.
  All three destroyed = game over. Wire this into game-state transitions.

### Acceptance Criteria

- [ ] All six enemy types implemented with correct HP, shield config, and AI
- [ ] All nine weapons implemented; weapon/target matrix from spec is
      observably true (Cannon shreds Drones, Beam suppresses Carrier shields,
      Depth Charge kills ground turrets, etc.)
- [ ] Round-start missile selection shows accurate wave composition and locks
      after FIRE
- [ ] Auto-turret targets independently of the player's aim
- [ ] Player has 4×100 HP directional shields, recharging after 3 s of no hit
- [ ] Wave manager escalates difficulty across at least 5 sample waves
- [ ] Ground turrets cannot be cheesed from altitude with Cannon — only
      Depth Charge / Plasma splash works (this is the design intent)

---

## Step 6 — Phase 4 HUD + Radar

**Spec:** `radar_system.md`
**Branch:** `phase4/hud-radar`

### Tier 1 Radar — MANDATORY before any wave testing

- [ ] 120 px disc bottom-right; 16 px altitude strip beside
- [ ] IFF colours (green=friendly, red=enemy, yellow=projectile, blue=pickup)
- [ ] Blip shapes by type (Drone=dot, Seeder=dot+, Fighter=triangle,
      Bomber=square, Carrier=diamond, Turret=cross, Friendly=circle, Pickup=star)
- [ ] Proximity pulse blink (faster blink as enemy nears)
- [ ] Inner/outer range rings on disc
- [ ] `worldToRadar()` rotates by `-playerYaw` so player heading is always up
- [ ] Pre-allocated 32-slot ghost blip pool (Tier 3 uses it; allocate now)

### Tier 2 — Phase 4 complete

- [ ] Threat vector arrows
- [ ] Inbound missile warning ring + screen-edge direction indicator
- [ ] Zone overlay (friendly bases / pads on radar)
- [ ] Carrier drone count label

### Other Phase 4 HUD pieces

- [ ] Directional shield display (4-sector pie diagram, per-sector colour)
- [ ] Weapon slot display (Primary/Secondary/Special — ammo, cooldown, active)
- [ ] Wave number + remaining enemies (top-centre thin bar)

### Acceptance Criteria

- [ ] Player can play through a wave using only the radar for situational
      awareness (close eyes during the test if you need to be sure)
- [ ] Inbound missile warning fires before the missile becomes visible
      on-screen, with at least 0.5 s for the player to react
- [ ] Radar correctly shows altitude relative to player on the strip
- [ ] All Tier 1 work is in place **before** the first multi-enemy wave is
      tested. Without altitude info the game is unfair; this is non-negotiable.

### Risk / Watch For

- **Don't allocate ghost blips on the heap.** Pre-allocated 32-slot pool.
- **Jamming jitter formula:** `sin(id * constant + time)` — deterministic per
  enemy, smooth, no random per frame.
- **Camera-view-aware sizing** is Tier 3 (Phase 5). Don't gate Phase 4 on it.

---

## Step 7 — Phase 5 Polish

**Branch:** `phase5/polish`

- [ ] **Radar Tier 3:** ghost blips, jamming near Carriers, boost visual,
      view-aware sizing
- [ ] **Particle system:** explosions, weapon impact, shield sparks, missile
      smoke trails. Builds on the Phase 2 exhaust pool.
- [ ] **Audio:** miniaudio backend, positional pan/volume, 4 weapon channels,
      distinct sound per weapon type, radar warning blips, engine loop
- [ ] **Day/night cycle:** sun direction animates over time; sky / ambient
      lerp from dawn → noon → dusk → night
- [ ] **Weather:** wind vector affects flight (drift); storm reduces visibility
      via tighter fog; particle overlay (rain/snow)
- [ ] **Post-FX (optional):** scanline vignette, mild bloom on engines + explosions

---

## Step 8 — Phase 6 Extended

- [ ] Leaderboard / score persistence (binary or JSON file)
- [ ] Cockpit camera mode (interior view, no external ship visible)
- [ ] Replay recording (deterministic at 120 Hz — record inputs, replay them)
- [ ] Optional: new weapon types (gravity well bomb, chain lightning, tractor beam)
- [ ] Optional: new enemy types (cloaking drone, kamikaze, artillery)

---

## Naming-Hazard Reference Card

These have bitten us. Keep this card open while editing.

| Forbidden / wrong | Correct | Why |
|---|---|---|
| `CameraMode` (new code) | `CameraView` | raylib typedef collision |
| `CamMode` (camera views) | `CameraView` | `CamMode` already exists for dev toggle |
| Re-adding `FlightMode` | (deleted) | Step 1 removes it permanently |
| Re-adding `CraftType` | (deleted) | Step 1 removes it permanently |
| Re-adding `smooth()` | (deleted) | Sine is C∞ smooth |
| Re-adding `applyRadialFalloff()` | (deleted) | Toroidal wrap, no edges |
| `TERRAIN_ROUGHNESS` | (deleted) | Sine has no roughness |
| `MatrixMultiply(A,B)` mental model = `A*B` | actually `B*A` | Bit us 3+ times |
| `pitchInput += 1` for W in Classic | `pitchInput -= 1` | Helicopter: stick-forward = nose-down = fly-forward |

---

## Recurring-Trap Reference Card

Things we've hit and have to remember each time:

1. **raylib's far clip plane is 1000 by default.** Our world is 8192². Keep the
   `rlFrustum(0.05, 3000)` override after `BeginMode3D` in `GameState::render`.
2. **Backface culling.** Mesh winding bugs hide ships at certain camera angles.
   Keep `rlDisableBackfaceCulling()` around `DrawMesh` for the player ship if
   the new mesh has any inconsistent windings.
3. **Camera terrain clamping.** Without
   `desiredPos.y = max(desiredPos.y, heightAt(...) + 3.5f)` the camera dives
   underground when turning toward hills. Apply to all five camera views.
4. **Loading screen during long ops.** Once terrain generation drops below ~1 s
   we can drop the progress bar. While >2 s, keep it.
5. **`m_pitchVis = -m_pitch`.** This sign relationship between physics pitch
   and render pitch has caused multiple regressions. Search for it before
   editing render or physics independently.
6. **Bug clangd flags as errors are usually include-path issues** — the cmake
   build is the source of truth for whether something compiles.

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Newtonian feel doesn't land at all four assist levels | Medium | Step 1 redo | Tune `NEWTON_*` constants per assist coefficient before Phase 3 |
| Terrain seam visible at world wrap | Low | Need to refactor `sample()` | Test fly-around explicitly; spec wraps both sample and heightAt |
| Camera Tactical view spins randomly | Low | Easy fix | `up = {0, 0, 1}` — caught in spec, watch for it |
| Phase 3 enemy AI feels stiff | High | Iterate | Plan for 2–3 tuning passes after wave 1 plays end-to-end |
| Cluster missile becomes single-target | Medium | Breaks weapon identity | Spec says 120° cone — don't widen |
| Beam laser doesn't kill Carriers | Medium | Combat balance broken | `timeSinceHit = 0` each tick — easy to forget |
| Radar Tier 1 skipped to "save time" | Medium | Combat unfair | Don't. Make the altitude strip mandatory. |
| Build time grows unbounded as features land | Low | Slow iteration | Re-evaluate progress bar threshold each phase |

---

## Implementation Etiquette (carried over from CLAUDE.md)

- **Single source of truth for constants:** `Config.hpp`. No magic numbers.
- **No heap allocation in hot paths.** Pools, pre-allocated.
- **`update()` never calls raylib draw functions. `render()` never mutates state.**
- **Procedural geometry only.** No `.obj` / `.glb`.
- **Coordinate system:** +Z = north, +X = east, +Y = up. Yaw=0 faces +Z; yaw
  increases clockwise.
- **Comments explain WHY.** A line that's obvious from the code doesn't need one.
- **Mark playtest tunables with `// TUNE`.** Easy to grep for during balance passes.

---

## When the Roadmap Ends

After Step 8, terra-siege is feature-complete relative to the original Virus
plus the modern combat layer (shields, upgrades, radar, multiple weapons,
multiple cameras). At that point the next-pass questions are:

- Multiplayer? (Out of scope for current rewrite.)
- Procedural mission/wave generation beyond the hand-crafted progression?
- Mod support / data-driven weapon and enemy definitions?
- Console / Windows ports?

These belong in a separate roadmap, written after Step 8 ships.
