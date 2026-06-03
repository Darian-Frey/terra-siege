# terra-siege — Project Roadmap

> **⚠ Archived 2026-06-03.** Historical reference — Phase 2 flight-modes design (dual Classic + Arcade + five craft). Original path: `project-status/project-roadmap.md`.
> Active replacement: [`/ROADMAP.md`](../../ROADMAP.md). The dual flight model was collapsed into a single Newtonian model during the rebuild; this Phase 2 design is preserved for context.

This document extends CLAUDE.md with the revised flight model plan and updated phase breakdown.

---

## Flight Mode Architecture

terra-siege will ship with two distinct game modes, each with its own craft, physics, and camera. The main menu presents two buttons:

```
┌─────────────────────┐
│                     │
│       CLASSIC       │
│                     │
│       ARCADE        │
│                     │
└─────────────────────┘
```

The player commits to one mode per playthrough — **no mid-game switching**. This lets each mode be tuned as its own experience (difficulty curves, craft handling, enemy aggression, scoring) without compromise.

Both modes share: terrain, weapons, enemies, HUD layout, audio, wave manager. They differ in: physics, input handling, camera, and ship mesh.

### Mode 1 — Arcade (Air Combat 22 style) `FlightMode::Arcade`

The accessible, fast-paced mode. Bank-to-turn, auto-leveling, forgiving.

**Craft — Delta Wing Fighter:**
- Sleek swept-back triangular wings, single central fuselage
- Cockpit forward, twin engines at the rear close to centerline
- Low profile, aerodynamic silhouette (~5m wide × 7m long × 1m tall)
- Palette: olive/steel hull with red accent stripes (military jet feel)
- Built as procedural mesh in `Player::buildArcadeCraft()`

**Controls:**
- A/D = combined bank-and-turn (roll input + automatic proportional yaw)
- W/S = pitch (nose down / nose up)
- Shift = afterburner (max speed + slight FOV increase)
- Ctrl or Space = air brake (slow down for tighter turns)
- Q/E = not used (altitude is controlled entirely via pitch)

**Physics:**
- Velocity vector always locked to ship's forward transform — no lateral drift
- Aggressive rotation interpolation (Slerp) — the ship snaps to new headings
- Turning: `yawRate = bankAngle * ARCADE_YAW_COEFFICIENT`
- Auto-level: when no roll input, wings lerp back to horizon quickly
- Constant forward speed (adjustable via throttle/brake) — ship cannot hover or stop
- Minimum speed floor prevents stalling
- Terrain pull-up: automatic nose-up nudge when approaching terrain at steep angle
- Forgiving — hard to crash, generous altitude floor

**Camera — Chase Cam:**
- Third-person behind and above (current camera, refined)
- Moderate lag for cinematic feel during turns
- FOV: 70° base, widens slightly during afterburner
- Camera banks slightly with the ship during turns (subtle, not 1:1)
- Alternative: cockpit cam (Phase 6 stretch goal)

**Constants (new for Config.hpp):**
```
ARCADE_BANK_RATE         = 4.0     // rad/s roll response
ARCADE_MAX_BANK          = 0.78    // ~45° max bank angle
ARCADE_YAW_FROM_BANK     = 2.5    // yaw rad/s per rad of bank
ARCADE_PITCH_RATE        = 2.0    // rad/s pitch response
ARCADE_AUTO_LEVEL_SPEED  = 5.0    // roll return-to-zero rate
ARCADE_MIN_SPEED         = 8.0    // can't go slower than this
ARCADE_MAX_SPEED         = 50.0
ARCADE_AFTERBURNER_SPEED = 80.0
ARCADE_AFTERBURNER_FOV   = 78.0   // widened from 70°
ARCADE_BRAKE_SPEED       = 12.0   // speed while braking
ARCADE_PULLUP_AGL        = 8.0    // nudge nose up below this AGL
ARCADE_PULLUP_STRENGTH   = 3.0    // how hard the pull-up pushes
```

---

### Mode 2 — Classic (Virus 1988 style) `FlightMode::Classic`

The original Newtonian feel. Punishing, rewarding, authentic.

**Craft — Updated Virus Hovercraft:**
- Modernised version of the original Virus lander: chunky, geometric, purposeful
- Rectangular body with visible downward thrust nozzles at each corner
- No wings — it's a hover platform, not an aircraft (thrust is along local UP)
- Wider, shorter, taller than the delta (~4m × 4m × 2m)
- Palette: dark greys with yellow/orange accents — matches the 1988 original
- Built as procedural mesh in `Player::buildClassicCraft()`

**Controls:**
- A/D = roll (bank left/right — no automatic yaw)
- W/S = pitch (tilt nose down/up to redirect thrust vector)
- Shift = thrust (force along ship's local UP axis only)
- No thrust = coast on momentum + gravity pulls you down

**Physics:**
- Full Newtonian: `velocity += (thrustVector + gravity) * dt`
- Thrust is along ship's LOCAL UP axis, not forward — pitch to convert lift into forward speed
- Near-zero drag (Config: `CLASSIC_DRAG = 0.005`) — momentum carries indefinitely
- Gravity: constant -Y force (Config: `CLASSIC_GRAVITY = 9.81`)
- Hover equilibrium: `thrust * cos(tiltAngle) = gravity` — tilt too far and you lose altitude
- No auto-level — full manual attitude control
- No speed cap — limited only by drag equilibrium
- Ground collision: low angle + low speed = bounce; high angle or high speed = destroy

**Camera — External High (Virus-style, modernised):**
- Higher, further back than arcade mode — shows more terrain
- FOV: 85° (wider than arcade — leveraging modern hardware)
- Camera lerps smoothly, does not bank with ship
- Shows the ship's shadow on terrain clearly (critical for altitude judgement)
- The ship and its relationship to the ground is always visible

**Constants (new for Config.hpp):**
```
CLASSIC_GRAVITY          = 9.81
CLASSIC_THRUST_POWER     = 18.0   // force when thrust key held
CLASSIC_DRAG             = 0.005  // minimal linear damping per tick
CLASSIC_ROLL_RATE        = 3.0    // rad/s
CLASSIC_PITCH_RATE       = 2.5    // rad/s
CLASSIC_BOUNCE_MAX_VEL   = 8.0    // above this = crash on ground hit
CLASSIC_BOUNCE_MAX_ANGLE = 0.5    // rad from horizontal — steeper = crash
CLASSIC_CAM_HEIGHT       = 12.0   // higher than arcade
CLASSIC_CAM_DISTANCE     = 22.0   // further back than arcade
CLASSIC_CAM_FOV          = 85.0   // wider FOV
CLASSIC_CAM_LERP_SPEED   = 4.0    // smooth but not snappy
```

---

## Camera Modes Summary

| Flight Mode | Default Camera | Height | Distance | FOV | Banks with ship |
|-------------|---------------|--------|----------|-----|-----------------|
| Arcade | Chase (behind) | 6m | 14m | 70° (78° afterburner) | Subtle (30%) |
| Classic | External high | 12m | 22m | 85° | No |
| Dev (F1) | Free-roam | — | — | 70° | No |
| Cockpit (Phase 6) | Internal first-person | 0m | 0m | 90° | Full |

---

## Selectable Craft (Arcade Mode)

Arcade mode offers **four distinct craft**, each with its own mesh, flight characteristics, and identity. The player picks one on the main menu after choosing Arcade mode. Classic mode has its own single craft (the updated Virus hovercraft) — the roster below applies to Arcade only.

Each craft has its own **CraftProfile** struct that overrides the default `ARCADE_*` constants in `Config.hpp`. The same physics model (bank-to-turn, velocity-locked forward) runs for all of them — only the tuning numbers differ. This keeps the code path simple while giving each craft a distinct feel.

### Craft Roster

| | Delta Wing | Forward Swept | X-36 | YB-49 |
|---|---|---|---|---|
| **Role** | Balanced fighter | Experimental agility | Research prototype | Heavy bomber |
| **Silhouette** | Swept-back delta | Forward-swept + canards | Tailless lambda wing | Wide flying wing |
| **Min speed** | 12 | 10 | 10 | 20 |
| **Cruise speed** | 30 | 28 | 25 | 36 |
| **Max speed** | 45 | 42 | 38 | 52 |
| **Boost speed** | 65 | 58 | 52 | 70 |
| **Roll rate (rad/s)** | 2.5 | 3.5 | 4.0 | 1.2 |
| **Pitch rate (rad/s)** | 1.2 | 1.6 | 1.8 | 0.7 |
| **Turn coefficient** | 1.6 | 2.2 | 2.5 | 0.9 |
| **Max bank angle** | 75° | 80° | 85° | 55° |
| **Max pitch angle** | 70° | 75° | 75° | 55° |
| **Energy trade factor** | 0.40 | 0.45 | 0.45 | 0.60 |
| **Throttle response** | 1.2 | 1.4 | 1.5 | 0.8 |
| **Hull health** | 100 | 85 | 75 | 140 |
| **Pullup AGL base** | 8 m | 8 m | 7 m | 14 m |
| **Pullup speed factor** | 0.35 | 0.35 | 0.30 | 0.50 |

### Craft Profiles

**Delta Wing — "Balanced Fighter"** — `CraftType::DeltaWing`
- The baseline craft. All-round, forgiving, the "default experience".
- No strong weaknesses, no extreme strengths.
- Recommended for new players.
- Closest to a modern multirole fighter (F-16, Mirage-style handling).

**Forward Swept — "Experimental Agility"** — `CraftType::ForwardSwept`
- High roll and pitch rate, tighter turns at the cost of top speed and hull.
- Inherently less stable feel — rewards precision, punishes sloppiness.
- Faster throttle response (`ACCEL_K = 1.4`) makes it feel twitchier on the stick.
- Slightly lower hull (85) — glass cannon of the roster.
- Inspired by the Su-47 Berkut / Grumman X-29.

**X-36 — "Research Prototype"** — `CraftType::X36`
- **Tightest turn radius of the roster** — canards give exceptional pitch authority.
- Highest bank and pitch limits (85° / 75°) — can pull maneuvers the others can't.
- Lowest top speed — not a fast craft, but unmatched in a dogfight.
- Pullup zone is tighter (base 7m, factor 0.30) — small, nimble, can hug terrain.
- Thinnest hull (75) — pay for the agility with fragility.
- Inspired by the NASA/Boeing X-36 tailless demonstrator.

**YB-49 — "Heavy Bomber"** — `CraftType::YB49`
- **Highest top speed** (boost 70) — big engines, good aerodynamics.
- **Heaviest hull by far** (140) — can absorb damage.
- **Slowest roll, slowest pitch, wide turns** — feels like turning a barge.
- Higher cruise speed (36) and minimum speed (20) — cannot slow down for tight fights.
- Higher energy trade (0.60) — climbs really cost speed.
- Larger pullup zone (14m base, 0.50 factor) — needs more altitude to recover.
- More stable though — big wing area = resistant to stall, hard to flip.
- Inspired by the Northrop YB-49 Flying Wing.

### Implementation Notes

**Struct layout** (to be added to `Config.hpp` or a new `craft/CraftProfile.hpp`):

```cpp
struct CraftProfile {
    float minSpeed, cruiseSpeed, maxSpeed, boostSpeed;
    float rollRate, pitchRate, turnCoeff;
    float bankMax, pitchMax;
    float energyTrade, accelK;
    float hullMax;
    float pullupBase, pullupSpeedFactor;
};

constexpr CraftProfile CRAFT_DELTA      = { 12, 30, 45, 65, 2.5, 1.2, 1.6, 1.31, 1.22, 0.40, 1.2, 100, 8.0, 0.35 };
constexpr CraftProfile CRAFT_FWD_SWEPT  = { 10, 28, 42, 58, 3.5, 1.6, 2.2, 1.40, 1.31, 0.45, 1.4,  85, 8.0, 0.35 };
constexpr CraftProfile CRAFT_X36        = { 10, 25, 38, 52, 4.0, 1.8, 2.5, 1.48, 1.31, 0.45, 1.5,  75, 7.0, 0.30 };
constexpr CraftProfile CRAFT_YB49       = { 20, 36, 52, 70, 1.2, 0.7, 0.9, 0.96, 0.96, 0.60, 0.8, 140, 14.0, 0.50 };
```

**Loading:** `Player::init()` or `Player::setCraft()` looks up the profile for the chosen `CraftType` and caches a copy as `m_profile`. Physics code reads from `m_profile.*` instead of `Config::ARCADE_*`.

**Balancing philosophy:**
- No craft is strictly better than another — every stat gain comes with a cost
- Delta Wing is the balanced reference; others tilt in one direction each
- Players earn a right to fly the specialist craft by getting comfortable with the delta first

### Design Open Questions

- **Separate craft for Classic mode?** Classic mode is the Virus homage and should probably stay **one craft** — the updated Virus hovercraft. Its character is the Newtonian physics, not the ship variety. Reconsider only if Classic gets its own campaign structure.
- **Unlockables?** A fresh playthrough could start with only the Delta Wing, unlocking the others through score/progression. This is a Phase 6 decision — don't build it until core combat is working.
- **Per-craft hull weighting?** The table lists hull HP per craft but damage, shield, and impact tuning aren't locked in yet. Hull values are starting guesses and will move during Phase 4.

---

## Revised Phase Plan

### Phase 2 — Player Craft (current)

**2a. Fix and polish current arcade controls** ✅ In progress
- Matrix order fix for raylib row-vector convention ✅
- Backface culling fix ✅
- Camera follow with fast lerp ✅
- Turn direction and banking ✅

**2b. Refactor into FlightMode system**
- Add `FlightMode` enum (`Arcade`, `Classic`) to GameState
- Flight mode is set once at game start and locked for the playthrough
- Extract physics into mode-specific update paths in Player
- Ship mesh is built at init based on selected mode (`buildArcadeCraft` or `buildClassicCraft`)
- No runtime mode switching — not even in DEV_MODE
- The main menu (Phase 2i) provides the selection UI

**2c. Implement Arcade mode (Air Combat 22 style)**
- Bank-to-turn: A/D control roll, yaw derived from bank angle
- W/S pitch control
- Auto-level on zero input
- Afterburner (Shift) with FOV kick
- Air brake (Ctrl/Space) for tight turns
- Terrain pull-up safety
- Velocity locked to forward vector
- Tune constants for snappy, fun feel

**2d. Implement Classic mode (Virus style)**
- Newtonian thrust-vs-gravity physics
- Thrust along local UP axis only
- Roll + pitch for manual attitude control
- Near-zero drag, momentum-based movement
- Ground collision with bounce/destroy logic
- No auto-level, no safety nets

**2e. Camera system per flight mode**
- Arcade: chase cam (current, refined) — moderate lag, subtle bank
- Classic: external high cam — higher, further, wider FOV, no bank
- Shared: terrain clamping, dev free-roam (F1)

**2f. Ship shadow**
- Dark translucent ellipse on terrain at `heightAt(ship.x, ship.z)`
- Critical for Classic mode altitude judgement
- Useful for Arcade mode too

**2g. Engine exhaust particles**
- CPU particle emitter at rear engine pods
- Orange/yellow billboard quads, 0.3s lifetime
- Pre-allocated pool (2000 particles)

**2h. Gamepad support**
- Map left stick to roll/pitch (both modes)
- Triggers for thrust/brake
- raylib `IsGamepadAvailable()` + `GetGamepadAxisMovement()`

**2j. Mouse control**
- Mouse X → yaw / roll input (bank-to-turn in Arcade, direct roll in Classic)
- Mouse Y → pitch (Arcade) or altitude (current basic mode)
- Sensitivity constants in Config.hpp:
  - `MOUSE_YAW_SENSITIVITY` — pixels-to-rad scaling for X axis
  - `MOUSE_PITCH_SENSITIVITY` — pixels-to-rad scaling for Y axis
- Mouse already captured by `DisableCursor()` in main.cpp — only used by free-roam camera currently
- Mouse input is **additive** to keyboard — both work simultaneously
- Free-roam camera (F1) keeps its existing mouse-look behavior

**2i. Main menu — Classic / Arcade selection**
- Simple two-button menu: `CLASSIC` and `ARCADE`
- Selection determines flight mode, craft mesh, and camera for the whole playthrough
- No ship picker, no mode picker, no settings tree — just the two buttons
- Brief one-line description under each button explaining the feel
  (e.g. "CLASSIC — Newtonian hovercraft. Unforgiving. Authentic."
  "ARCADE — Bank-and-turn fighter. Fast. Forgiving.")
- Selection transitions directly into gameplay
- Transitions back to menu only via game over / victory / pause → quit

### Phase 3 — Enemies, Combat, Wave Manager
*(unchanged from CLAUDE.md)*

### Phase 3.5 — Terrain Objects
*(unchanged from CLAUDE.md)*

### Phase 4 — Shields, HUD, Radar
*(unchanged from CLAUDE.md)*

### Phase 5 — Polish
*(unchanged from CLAUDE.md)*

### Phase 6 — Extended Features
*(unchanged from CLAUDE.md, plus cockpit cam for Arcade mode)*

---

## Implementation Order (recommended)

Within Phase 2, the suggested build order:

1. **Ship shadow** (2f) — quick win, helps with altitude perception in both modes
2. **FlightMode refactor** (2b) — create the architecture for two modes + two craft
3. **Arcade mode + delta craft** (2c) — the accessible mode most players will use
4. **Classic mode + hovercraft** (2d) — the authentic Virus experience
5. **Camera per mode** (2e) — tune each camera to its flight style
6. **Main menu** (2i) — the Classic/Arcade selection screen
7. **Exhaust particles** (2g) — visual polish
8. **Gamepad** (2h) — input expansion

This order lets us get both flight modes working in DEV_MODE first (with a temporary startup flag or hardcoded default), then add the menu last once both feel right. Main menu goes near the end because we need something to put on the menu before we build it.
