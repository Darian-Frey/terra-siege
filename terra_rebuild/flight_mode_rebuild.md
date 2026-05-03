# terra-siege — Flight Mode Rebuild

## Overview

The current Player system has two physics models (Classic and Arcade), five craft
types, mouse-sensitivity tuning, input smoothing, a flight recorder, and several
layers of abstraction built on top of each other. The plan is to strip all of this
back to a single, well-tuned Newtonian physics model that captures the feel of the
original Virus/Zarch — the "tilt and burn" system — and then layer flight assist
on top as a difficulty modifier rather than as an alternate physics model.

---

## What Gets Removed

- `FlightMode` enum (`Classic` / `Arcade` / `_Count`)
- `CraftType` enum (all five variants)
- `handleArcadeInput()` and `handleClassicInput()` — replaced by one `handleInput()`
- `applyArcadePhysics()` and `applyClassicPhysics()` — replaced by one `applyPhysics()`
- All Arcade constants from `Config.hpp`:
  `ARCADE_MIN_SPEED`, `ARCADE_CRUISE_SPEED`, `ARCADE_MAX_SPEED`, `ARCADE_BOOST_SPEED`,
  `ARCADE_THROTTLE_RATE`, `ARCADE_ACCEL_K`, `ARCADE_PITCH_RATE`, `ARCADE_PITCH_MAX`,
  `ARCADE_ROLL_RATE`, `ARCADE_BANK_MAX`, `ARCADE_TURN_COEFF`, `ARCADE_BANK_RESPONSE`,
  `ARCADE_ENERGY_TRADE`, `ARCADE_GRAVITY`, `ARCADE_MOUSE_PITCH_SENS`,
  `ARCADE_MOUSE_ROLL_SENS`, `ARCADE_MOUSE_SMOOTH`, `ARCADE_RATE_SMOOTH`,
  `ARCADE_PULLUP_BASE`, `ARCADE_PULLUP_SPEED_FACTOR`, `ARCADE_PULLUP_STRENGTH`,
  `ARCADE_CAM_DISTANCE_MIN`, `ARCADE_CAM_DISTANCE_MAX`, `ARCADE_CAM_HEIGHT_MIN`,
  `ARCADE_CAM_HEIGHT_MAX`, `ARCADE_CAM_FOV_MIN`, `ARCADE_CAM_FOV_MAX`
- All Classic constants from `Config.hpp`:
  `CLASSIC_GRAVITY`, `CLASSIC_THRUST`, `CLASSIC_THRUST_IDLE`, `CLASSIC_DRAG`,
  `CLASSIC_PITCH_RATE`, `CLASSIC_ROLL_RATE`, `CLASSIC_YAW_RATE`, `CLASSIC_PITCH_MAX`,
  `CLASSIC_ROLL_MAX`, `CLASSIC_MAX_SPEED`, `CLASSIC_GROUND_IMPACT`,
  `CLASSIC_GROUND_BOUNCE`, `CLASSIC_MOUSE_PITCH_SENS`, `CLASSIC_MOUSE_ROLL_SENS`,
  `CLASSIC_INPUT_SMOOTH`, `CLASSIC_CAM_HEIGHT`, `CLASSIC_CAM_DISTANCE`,
  `CLASSIC_CAM_FOV`, `CLASSIC_CAM_LERP`
- Multiple craft mesh variants — replaced by one hovercraft mesh (the existing
  Saucer-style geometry is the right starting point)
- `setCraft()`, `setFlightMode()`, `flightModeName()`, `craftName()` methods
- All associated `F5` and `F6` dev key bindings
- `targetSpeed()`, `boosting()` accessors (Arcade concepts)
- The speed-dependent camera distance/FOV system (Arcade concept)

---

## What the New System Is

### The Original Virus Feel

From the Zarch/Virus source code documentation and gameplay analysis:

> "The lander has a single thruster pointing directly downwards beneath it. Firing
> the thruster causes the lander to fly straight upwards. To fly in any direction
> requires the lander to be tilted in that direction. The lander can only pitch and
> yaw; it cannot roll."

The original flight model is essentially **Lunar Lander in 3D**:
- Thrust is always along the ship's **local UP axis** (pointing out of the bottom)
- Gravity is constant and always pulls **world DOWN**
- The player steers by tilting the ship — changing where the thrust vector points
- Near-zero drag — momentum is conserved almost perfectly
- At full tilt the ship accelerates horizontally; tilting back upright fights the gravity

This is the feel we want. It is genuinely different from both the old Classic and
Arcade models. The old Classic had configurable roll (±180°) which the original
did not have — Braben's ship pitches and yaws only.

### Departure from the Original

We add **roll** as an optional control axis. The original omitted it to reduce
complexity on a 1987 mouse. We include it but limit it to ±45° maximum lean so it
acts as a banking aid rather than enabling full aerobatics. Flight assist (at levels
2+) auto-returns roll to zero when the player releases input.

---

## The Physics Model

### Core Equations (per physics tick, dt = 1/120s)

```
// 1. Resolve input into attitude rates
pitchRate  = mouseY  * NEWTON_MOUSE_PITCH_SENS   // rad/tick
yawRate    = mouseX  * NEWTON_MOUSE_YAW_SENS     // rad/tick

// 2. Integrate attitude (Euler angles — sufficient for this game)
pitch += pitchRate
yaw   += yawRate
pitch  = clamp(pitch, -NEWTON_PITCH_MAX, NEWTON_PITCH_MAX)

// 3. Build thrust vector in world space from current orientation
//    Ship local UP = rotate(world_UP, pitch, yaw, roll)
thrustDir = rotateVector({0, 1, 0}, pitch, yaw, roll)

// 4. Apply thrust (left mouse button held, or W key)
if (thrusting)
    vel += thrustDir * NEWTON_THRUST * dt

// 5. Apply gravity (always world -Y)
vel.y -= NEWTON_GRAVITY * dt

// 6. Apply drag (near-zero — just enough to prevent runaway)
vel *= (1.0f - NEWTON_DRAG * dt)

// 7. Integrate position
pos += vel * dt

// 8. Terrain collision
groundH = planet.heightAt(pos.x, pos.z)
if pos.y < groundH + PLAYER_MIN_ALTITUDE:
    handle landing / crash (see below)
```

### Constants (new, replacing all Arcade and Classic blocks)

```cpp
namespace Config {
    // Newtonian flight model
    constexpr float NEWTON_GRAVITY         = 9.8f;    // m/s² world-down
    constexpr float NEWTON_THRUST          = 24.0f;   // m/s² along local UP
    constexpr float NEWTON_DRAG            = 0.02f;   // near-zero linear damping
    constexpr float NEWTON_PITCH_MAX       = 1.30f;   // ~74° — steep but not inverted
    constexpr float NEWTON_ROLL_MAX        = 0.78f;   // ±45° banking limit
    constexpr float NEWTON_MAX_SPEED       = 70.0f;   // hard clamp
    constexpr float NEWTON_MOUSE_PITCH_SENS = 0.0025f; // rad per pixel
    constexpr float NEWTON_MOUSE_YAW_SENS  = 0.003f;  // rad per pixel
    constexpr float NEWTON_MOUSE_ROLL_SENS = 0.002f;  // rad per pixel (optional axis)
    constexpr float NEWTON_INPUT_SMOOTH    = 12.0f;   // lowpass per second on mouse
    constexpr float NEWTON_CRASH_SPEED     = 12.0f;   // vertical impact that kills
    constexpr float NEWTON_LAND_SPEED      = 3.0f;    // max safe landing velocity
    constexpr float NEWTON_FUEL_MAX        = 100.0f;  // fuel units
    constexpr float NEWTON_FUEL_BURN_RATE  = 3.5f;    // units/sec while thrusting
    constexpr float NEWTON_FLIGHT_CEILING  = 250.0f;  // AGL above which thrust cuts
}
```

### Landing and Crashing

The original Zarch had a crash system that the Atari ST port simplified. We implement
the proper version:

```
on ground contact:
    impactSpeed = |vel.y|
    if impactSpeed > NEWTON_CRASH_SPEED:
        CRASH — destroy ship, game over
    elif impactSpeed > NEWTON_LAND_SPEED:
        HARD LANDING — damage proportional to excess speed
    elif ship attitude (pitch, roll) not within ±8° of level:
        CRASH — not level enough to land
    else:
        SUCCESSFUL LANDING — refuel if at launch pad
```

### Flight Ceiling

The original had a hard ceiling above which thrust would not fire:

```
if (pos.y - groundH > NEWTON_FLIGHT_CEILING):
    thrustEnabled = false
```

This gives a natural upper boundary without a hard wall — the ship drifts up into
the ceiling and gravity pulls it back if thrust cuts out.

---

## Controls

### Primary (Mouse)
| Input | Action |
|-------|--------|
| Mouse forward/back | Pitch nose down/up |
| Mouse left/right | Yaw left/right |
| Left mouse button | Thrust (engine on) |
| Right mouse button | Fire primary weapon |

### Keyboard alternatives and extensions
| Key | Action |
|-----|--------|
| W | Thrust (same as left mouse button) |
| A / D | Yaw left / right |
| Q / E | Roll left / right (optional banking) |
| Space | Fire primary weapon |
| Shift | Fire secondary weapon |

### Why mouse for pitch/yaw, not keys?

The original Virus used mouse exclusively. The sensitivity creates the characteristic
"twitchy but precise" feel — small mouse movements tilt the ship slightly for gentle
manoeuvres; larger sweeps create aggressive tilts for fast travel. This is the game's
defining mechanical feeling and we must preserve it.

---

## Flight Assist System (unchanged in concept, rebuilt on new physics)

Flight assist sits on top of the Newtonian model and reduces its unforgiving nature.
It does NOT change the physics — it applies corrective inputs automatically.

| Level | Name | What it does |
|-------|------|-------------|
| 0 | Raw | No correction. Pure Newtonian. Crash if not careful. |
| 1 | Minimal | Auto-levels roll back to 0 when input released |
| 2 | Standard | Auto-level roll + auto-reduce pitch toward level when no input |
| 3 | Full | Auto-level + auto-reduce pitch + terrain avoidance thrust |

Flight assist is the primary difficulty axis. "Raw" is authentic Virus difficulty —
extremely punishing, very satisfying when mastered. "Full" is accessible to anyone.

### Level 3 Terrain Avoidance

At assist level 3, a look-ahead raycast fires along the velocity vector:
```
lookAheadTime = 0.4s
aheadPos = pos + vel * lookAheadTime
aheadGround = planet.heightAt(aheadPos.x, aheadPos.z)
dangerAGL = aheadGround + PLAYER_MIN_ALTITUDE * 4.0f

if pos.y < dangerAGL:
    // Apply upward corrective thrust proportional to how deep we are
    correction = (dangerAGL - pos.y) * ASSIST_PULLUP_STRENGTH
    vel.y += correction * dt
```

---

## Camera

The camera becomes simpler with one physics model:
- Fixed `CAM_DISTANCE = 18.0f` behind, `CAM_HEIGHT = 8.0f` above (slightly higher
  than current to give better view of the terrain ahead when pitching forward)
- No speed-dependent zoom — that was an Arcade concept
- Camera uses horizontal yaw only — does not follow the ship's pitch
- The terrain clamping logic stays exactly as it is (it works well)

```cpp
namespace Config {
    constexpr float CAM_HEIGHT    = 8.0f;
    constexpr float CAM_DISTANCE  = 18.0f;
    constexpr float CAM_FOV       = 75.0f;
    constexpr float CAM_LERP      = 8.0f;  // follow lag per second
}
```

---

## Player.hpp Shape After Rebuild

```cpp
class Player {
public:
    void init(Vector3 startPos, int flightAssistLevel = 2);
    void update(float dt, const Planet& planet);
    void render() const;
    void unload();

    // Accessors
    Vector3 position()   const;
    Vector3 velocity()   const;
    Vector3 forward()    const;   // world-space nose direction
    Vector3 up()         const;   // world-space local UP (thrust direction)
    float   yaw()        const;
    float   pitch()      const;
    float   roll()       const;
    float   speed()      const;
    float   fuel()       const;
    float   health()     const;
    bool    isAlive()    const;
    bool    isThrusting()const;
    bool    isLanded()   const;
    void    setFlightAssist(int level);
    int     flightAssist()  const;
    void    applyDamage(float amount);

private:
    void handleInput(float dt);
    void applyFlightAssist(float dt);
    void applyPhysics(float dt, const Planet& planet);
    void buildMesh();

    Vector3 m_pos     = {};
    Vector3 m_vel     = {};
    float   m_yaw     = 0.0f;
    float   m_pitch   = 0.0f;
    float   m_roll    = 0.0f;
    float   m_fuel    = Config::NEWTON_FUEL_MAX;
    float   m_health  = 100.0f;
    bool    m_thrusting = false;
    bool    m_landed    = false;
    int     m_assistLevel = 2;

    // Input smoothing
    Vector2 m_smoothMouse = {};

    // Mesh
    Mesh    m_mesh  = {};
    Model   m_model = {};
    bool    m_built = false;
};
```

---

## Mesh

One mesh. The existing Saucer/hovercraft geometry is the right shape — a flat,
wide body with two engine nacelles. Trim it to look like the original Virus lander:
a slightly squashed, elongated diamond shape viewed from above with a visible
thrust exhaust port on the underside. Keep it simple — the original was extremely
simple geometry. Twelve to twenty faces total is appropriate.

The mesh should make the thrust direction visually obvious — the player needs to
intuitively understand which way the ship will go when they tilt it.

---

## HUD Changes

Remove: craft name display, flight mode display, target speed, boost indicator.
Add: fuel gauge (alongside hull health bar), landed indicator.

```
Bottom left:
  HULL  [========          ]   (health bar, existing)
  FUEL  [===============   ]   (new fuel bar)
  LANDED                       (shown when safely on ground)
```

---

## Dev Mode Keys (simplified)

| Key | Action |
|-----|--------|
| F1 | Toggle follow/free camera |
| F2 | Cycle flight assist level (0–3) |
| F3 | God mode (invincible, infinite fuel) |
| F4 | Flight recorder |

F5 (craft) and F6 (flight mode) are removed.

---

## Implementation Order

1. Gut `Player.hpp` — remove FlightMode, CraftType, all dual-model members
2. Rewrite `Player.cpp` — single `handleInput()`, `applyPhysics()`, `buildMesh()`
3. Strip `Config.hpp` — remove all ARCADE_ and CLASSIC_ blocks, add NEWTON_ block
4. Simplify `GameState.cpp` — remove F5/F6 bindings, craft/mode HUD elements,
   speed-dependent camera, simplify follow camera
5. Test feel at all four assist levels before proceeding to Phase 3

