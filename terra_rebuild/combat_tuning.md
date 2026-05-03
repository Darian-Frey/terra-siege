# terra-siege — Combat Tuning Document

## Purpose

This document is the authoritative reference for all combat-related numbers in
terra-siege. All HP values, damage values, TTK targets, and shield parameters
are derived from the design principles here — not guessed. When something feels
wrong during playtesting, identify which design principle is violated and adjust
the principle first, then re-derive the numbers. Never adjust individual constants
in isolation.

---

## Core Design Principles

1. **Every weapon has a purpose.** No weapon should be strictly better than another
   in all situations. Each weapon has a target type it excels against and situations
   where it underperforms. Players should feel the difference.

2. **Time-to-kill is the anchor.** All HP and damage values derive from TTK targets.
   The TTK targets are the real design decision — everything else is arithmetic.

3. **Both missiles can kill anything.** With round-start missile selection (no
   mid-wave swapping), neither missile can be useless in any situation. Each has
   a preferred target profile where it is faster and more ammo-efficient, but both
   are viable tools against any enemy type.

4. **Shields create tactical decisions, not just HP padding.** Shields should be
   strippable within the first 25–30% of a sustained engagement. If a player
   focuses fire and stays on target, shields should never fully regenerate during
   that engagement. Shield regen is a reward for breaking off, not a permanent defence.

5. **Enemy shield complexity scales with threat level.** Drones and Seeders have no
   shields. Fighters have a simple omnidirectional shield. Carriers have a four-sector
   directional shield requiring the player to focus fire on one quadrant. Complexity
   matches the importance of the target.

6. **The Beam Laser exists because of the Carrier.** The Carrier's fast shield regen
   makes it resistant to burst weapons — the shield recharges between attacks. The
   Beam Laser suppresses shield regeneration with continuous damage, making it
   mechanically necessary for efficient Carrier kills. This is not an incidental
   property — it is the reason the weapon exists.

7. **The Depth Charge exists because of Ground Turrets.** The Newtonian flight model
   makes horizontal cannon fire at ground targets awkward and dangerous. The Depth
   Charge exploits the vertical axis — drop from altitude, safe from return fire.
   Without it, Ground Turrets would feel unfair.

---

## Time-to-Kill Budget

TTK is measured in seconds of **sustained Cannon fire** under normal engagement
conditions. All other weapons scale relative to this baseline.

```
Cannon DPS reference:
  CANNON_DAMAGE     =  8.0 damage/shot
  CANNON_FIRE_RATE  =  0.08s between shots
  Cannon DPS        =  8.0 / 0.08 = 100 DPS
```

| Enemy | TTK Target | Total HP | Feel |
|-------|-----------|----------|------|
| Swarm Drone | 0.08s (1 shot) | 8 | Instant volume kills |
| Seeder | 0.5s | 50 | Trivial, keep moving |
| Fighter | 2.0s | 200 | Requires brief focused tracking |
| Bomber | 5.0s | 500 | Sustained engagement, needs commitment |
| Carrier | 25.0s | 2500 | Major objective, all weapons needed |
| Ground Turret | 4.0s | 400 | Priority target, destroy before it pins you |

**Derivation formula:**
```
total_HP    = TTK_seconds × Cannon_DPS
shield_HP   = total_HP × shield_fraction
hull_HP     = total_HP - shield_HP
```

---

## Enemy Hull and Shield Values

### Shield Fractions

| Enemy | Shield Fraction | Rationale |
|-------|----------------|-----------|
| Swarm Drone | 0% | Cannon fodder — one shot, done |
| Seeder | 0% | Slow and vulnerable by design |
| Fighter | 20% | Light shield — absorbs first burst |
| Bomber | 30% | Tougher shield — costs a missile to strip efficiently |
| Carrier | 40% | Shield IS the tactical challenge |
| Ground Turret | 0% | Stationary, compensated by position advantage |

### Derived HP Values

```
Swarm Drone:
  total HP  = 0.08 × 100 = 8
  shield HP = 0
  hull HP   = 8

Seeder:
  total HP  = 0.5 × 100 = 50
  shield HP = 0
  hull HP   = 50

Fighter:
  total HP  = 2.0 × 100 = 200
  shield HP = 200 × 0.20 = 40
  hull HP   = 160

Bomber:
  total HP  = 5.0 × 100 = 500
  shield HP = 500 × 0.30 = 150
  hull HP   = 350

Carrier:
  total HP  = 25.0 × 100 = 2500
  shield HP = 2500 × 0.40 = 1000  (four sectors × 250 each)
  hull HP   = 1500

Ground Turret:
  total HP  = 4.0 × 100 = 400
  shield HP = 0
  hull HP   = 400
```

### Shield Recharge Parameters

| Enemy | Recharge Delay | Recharge Rate | Notes |
|-------|---------------|---------------|-------|
| Fighter | 4.0s | 20 HP/s | Recharges between engagements, not during |
| Bomber | 5.0s | 25 HP/s | Long delay — committed fire strips it cleanly |
| Carrier | 2.0s | 80 HP/s | Fast regen — designed to require Beam Laser |

**Rule:** Recharge delay must be longer than a typical engagement burst duration.
A player who stays on target continuously should never see a shield recharge mid-burst.
A player who breaks off and returns after 6+ seconds will face a partially recharged shield.

---

## Config.hpp Values

```cpp
namespace Config {

// ----------------------------------------------------------------
// Combat tuning — TIME-TO-KILL BUDGET
// Change TTK targets first, then re-derive HP values using:
//   total_HP  = TTK_seconds * CANNON_DPS  (reference: 100 DPS)
//   shield_HP = total_HP * shield_fraction
//   hull_HP   = total_HP - shield_HP
// ----------------------------------------------------------------

// TTK targets (seconds of sustained cannon fire)
constexpr float TTK_DRONE       = 0.08f;
constexpr float TTK_SEEDER      = 0.50f;
constexpr float TTK_FIGHTER     = 2.00f;
constexpr float TTK_BOMBER      = 5.00f;
constexpr float TTK_CARRIER     = 25.0f;
constexpr float TTK_TURRET      = 4.00f;

// Enemy hull HP (derived — do not edit directly, adjust TTK above)
constexpr float HULL_DRONE      = 8.0f;
constexpr float HULL_SEEDER     = 50.0f;
constexpr float HULL_FIGHTER    = 160.0f;
constexpr float HULL_BOMBER     = 350.0f;
constexpr float HULL_CARRIER    = 1500.0f;
constexpr float HULL_TURRET     = 400.0f;

// Enemy shield HP (derived — do not edit directly, adjust TTK above)
constexpr float SHIELD_DRONE    = 0.0f;
constexpr float SHIELD_SEEDER   = 0.0f;
constexpr float SHIELD_FIGHTER  = 40.0f;
constexpr float SHIELD_BOMBER   = 150.0f;
constexpr float SHIELD_CARRIER  = 250.0f;  // per sector (×4 = 1000 total)
constexpr float SHIELD_TURRET   = 0.0f;

// Enemy shield recharge
constexpr float SHIELD_DELAY_FIGHTER   = 4.0f;   // seconds before recharge begins
constexpr float SHIELD_DELAY_BOMBER    = 5.0f;
constexpr float SHIELD_DELAY_CARRIER   = 2.0f;
constexpr float SHIELD_RATE_FIGHTER    = 20.0f;  // HP/sec
constexpr float SHIELD_RATE_BOMBER     = 25.0f;
constexpr float SHIELD_RATE_CARRIER    = 80.0f;  // fast — requires sustained pressure

// Player hull and shield
constexpr float PLAYER_HULL_HP         = 100.0f;
constexpr float PLAYER_SHIELD_HP       = 100.0f;  // per sector (×4 = 400 total)
constexpr float PLAYER_SHIELD_DELAY    = 3.0f;
constexpr float PLAYER_SHIELD_RATE     = 8.0f;

} // namespace Config
```

---

## Weapon System

### Primary Slot

#### Cannon
The default starting weapon. Unlimited ammo. Workhorse for early waves.

```cpp
constexpr float CANNON_DAMAGE       = 8.0f;
constexpr float CANNON_FIRE_RATE    = 0.08f;   // seconds between shots
constexpr float CANNON_SPEED        = 120.0f;  // world units/sec
constexpr float CANNON_RANGE        = 200.0f;
// DPS = 100 — the baseline everything is tuned against
```

**Preferred targets:** Drones, Seeders, Fighters
**Works against:** Bombers (slow but viable)
**Inefficient against:** Carrier shields (regen outpaces DPS if disengaging)

#### Plasma Cannon
Slower rate of fire, splash damage, limited ammo. Unlocked when Fighters start
appearing in numbers. Splash compensates for evasive target movement.

```cpp
constexpr float PLASMA_DAMAGE       = 35.0f;
constexpr float PLASMA_FIRE_RATE    = 0.28f;   // seconds between shots
constexpr float PLASMA_SPEED        = 75.0f;
constexpr float PLASMA_SPLASH       = 8.0f;    // world units radius
constexpr int   PLASMA_AMMO_MAX     = 120;
// DPS = 125 — slightly higher than Cannon but limited ammo
// Splash means near-misses on evasive Fighters still deal ~60% damage
```

**Preferred targets:** Evasive Fighters, clustered enemies
**Works against:** Bombers
**Inefficient against:** Single isolated targets (splash wasted)

#### Beam Laser
Continuous damage, no projectile travel time, drains energy cell. Unlocked when
Carriers first appear. The defining weapon for Carrier fights — continuous damage
actively suppresses shield regeneration.

```cpp
constexpr float BEAM_DAMAGE_PS      = 120.0f;  // damage per second
constexpr float BEAM_RANGE          = 180.0f;
constexpr float BEAM_ENERGY_MAX     = 100.0f;
constexpr float BEAM_DRAIN_PS       = 22.0f;   // energy drain per second firing
constexpr float BEAM_RECHARGE_PS    = 14.0f;   // energy recharge per second idle
// At full energy: 100/22 = 4.5 seconds continuous fire
// Recharge from empty: 100/14 = 7.1 seconds
// DPS = 120 (highest primary DPS — justified by limited energy and single-target only)
```

**Why this weapon exists:** Carrier shield recharges at 80 HP/s. Cannon DPS of 100
exceeds this, but only while firing continuously. The moment the player breaks off to
evade, the Carrier shield recharges. In practice the Cannon cannot sustain the 25s TTK
without breaks. The Beam Laser at 120 DPS gives more margin and — crucially — its
continuous nature means shield regen is suppressed the entire time it's on target.

**Preferred targets:** Carrier (shield suppression), stationary Bombers
**Works against:** Fighters (overkill but fast)
**Inefficient against:** Drones (overkill), Swarms (single target only)

---

### Secondary Slot — Missile Selection

**Round-start selection system.** Before each wave the player chooses which missile
type to carry for that round. The wave composition is shown on the radar report so
the choice is informed. No mid-wave swapping. No pickup dependency.

The missile selection UI appears at wave start:
```
╔════════════════════════════════════════╗
║  WAVE 4 INCOMING                       ║
║                                        ║
║  RADAR REPORT:                         ║
║   8 × Fighters                         ║
║   2 × Bombers                          ║
║   1 × Carrier                          ║
║                                        ║
║  SELECT MISSILE TYPE:                  ║
║  [ STANDARD  ×16 ]  [ CLUSTER  ×10 ]  ║
║                                        ║
║  Press FIRE to confirm                 ║
╚════════════════════════════════════════╝
```

#### Standard Missile — Patient Hunter

High single-hit damage, moderate homing, moderate manoeuvrability. Preferred for
high-HP targets that can't evade — Bombers and Carriers. Works against all targets
but is ammo-wasteful against Drones.

```cpp
constexpr float MISSILE_DAMAGE      = 55.0f;
constexpr float MISSILE_SPEED       = 70.0f;
constexpr float MISSILE_NAV_N       = 3.5f;   // proportional navigation constant
constexpr float MISSILE_TURN_RATE   = 2.8f;   // rad/s — evadable by sharp turns
constexpr float MISSILE_BLAST_RAD   = 4.0f;
constexpr int   MISSILE_AMMO        = 16;     // per round
```

**Proportional navigation at N=3.5:** A Fighter flying a perpendicular crossing course
can cause the missile to overshoot if the turn is sharp enough. This is intentional —
skilled enemy AI should be able to partially evade Standard Missiles, making the
engagement last longer and requiring more missiles. Bombers cannot execute the required
turn, so the missile connects reliably.

**Kill counts per target type (Standard Missile):**
```
Drone      (8 HP):   1 hit  — massive overkill, wastes missile
Seeder     (50 HP):  1 hit  — works, slight overkill
Fighter    (200 HP): 4 hits — viable, but may miss 1-2 due to evasion
Bomber     (500 HP): 10 hits — preferred target, reliable tracking
Carrier    (2500 HP): ~46 hits — impractical as primary weapon, use as supplement
```

**Verdict:** Choose Standard when facing Bombers and Carriers. Viable against Fighters
as suppression. Wasteful against Drones.

#### Cluster Missile — Aggressive Swarm Killer

Low parent damage, four fast-homing submunitions, high aggregate manoeuvrability.
Preferred for Drones, swarm waves, and Fighter escorts. Works against all targets
but is inefficient against high-HP single targets because damage is spread.

```cpp
// Parent missile (delivery system only)
constexpr float CLUSTER_PARENT_DAMAGE    = 8.0f;
constexpr float CLUSTER_PARENT_SPEED     = 65.0f;
constexpr float CLUSTER_PARENT_NAV_N     = 5.0f;
constexpr float CLUSTER_PARENT_TURN_RATE = 4.0f;
constexpr float CLUSTER_PROX_TRIGGER     = 20.0f;  // world units — split distance

// Submunitions (×4)
constexpr float CLUSTER_SUB_DAMAGE       = 18.0f;
constexpr float CLUSTER_SUB_SPEED        = 90.0f;  // faster than parent
constexpr float CLUSTER_SUB_NAV_N        = 6.0f;   // aggressive tracking
constexpr float CLUSTER_SUB_TURN_RATE    = 5.5f;   // rad/s — very hard to shake
constexpr float CLUSTER_SUB_BLAST_RAD    = 2.5f;
constexpr float CLUSTER_SUB_SEARCH_CONE  = 2.094f; // 120° in radians
constexpr int   CLUSTER_AMMO             = 10;     // per round
```

**Why N=6.0 for submunitions:** At N=3.5 (Standard) a perpendicular crossing course
causes overshoot. At N=6.0 the geometry closes too fast — the submunition curves
sharply enough to follow even a hard-turning Drone. The trade-off is low damage per
hit, so it can't punch through shields efficiently.

**Submunition split behaviour:**
1. Parent reaches proximity trigger distance from nearest enemy
2. Parent stops homing, maintains current velocity vector
3. Four submunitions spawn at parent position, offset in cross pattern
4. Each submunition independently queries SpatialGrid for nearest enemy
   within its 120° forward search cone
5. Each homes independently using SUB_NAV_N and SUB_TURN_RATE
6. If no target found in search cone: submunition flies ballistically until
   terrain impact or 4-second lifetime expiry

**The 120° search cone prevents all four submunitions targeting the same enemy**
when the swarm is loose — they spread naturally. Against a tight cluster the overlap
is irrelevant because there are enough targets for all four.

**Kill counts per target type (one Cluster volley, all 4 submunitions connecting):**
```
Total volley damage if all connect = 8 (parent) + 4×18 (subs) = 80 damage

Drone      (8 HP):    1 sub kills — 1 volley can kill up to 4 Drones
Seeder     (50 HP):   3 subs to kill 1 Seeder — efficient in loose groups
Fighter    (200 HP):  ~3 volleys assuming partial misses — viable but not ideal
Bomber     (500 HP):  ~7 volleys minimum — inefficient, use Standard
Carrier    (2500 HP): 32+ volleys — impractical, subs scatter on single target
```

**Verdict:** Choose Cluster when facing Drone waves, Swarms, or Fighter escort groups.
Viable against isolated Fighters. Wasteful against Bombers and Carriers.

**The wrong-choice scenario (design intent):**
Firing a Cluster at a lone Fighter: three submunitions find no target in their cone,
fly ballistically, miss. Only one submunition connects. 26 damage total from a 10-ammo
pool missile. This feels bad — and it should. The player learns Cluster is a situational
weapon, not an upgrade.

Firing a Standard at a Drone wave: 55 damage kills the first Drone, zero splash.
The other seven Drones continue. 7 more missiles needed. 8 missiles per Drone wave
vs 1-2 Cluster volleys. Also feels bad. The player learns Standard is wasteful on swarms.

Both "feel bad" scenarios are correct design — they communicate the weapon's purpose
without a tooltip.

---

**Missile Comparative Summary**

| Metric | Standard | Cluster |
|--------|---------|---------|
| Single-hit damage | 55 | 8 (parent) + up to 72 (subs) |
| Nav constant (N) | 3.5 | 6.0 (subs) |
| Turn rate | 2.8 rad/s | 5.5 rad/s (subs) |
| Evasion difficulty | Evadable by sharp turns | Very hard to shake |
| Ammo per round | 16 | 10 |
| Best against | Bombers, Carriers | Drones, Swarms, Fighter groups |
| Worst against | Drones (overkill) | Bombers/Carriers (damage spread) |

---

### Special Slot

#### Auto Turret
Independent targeting AI, fires without player input. The 180° blind spot handler —
critical during Carrier engagements when the player must maintain forward focus.

```cpp
constexpr float TURRET_DAMAGE       = 10.0f;
constexpr float TURRET_FIRE_RATE    = 0.35f;   // seconds between shots
constexpr float TURRET_RANGE        = 60.0f;
constexpr float TURRET_ROT_SPEED    = 3.5f;    // rad/s
constexpr float TURRET_FIRE_CONE    = 0.06f;   // radians — must be aimed before firing
// DPS = 28.6 — enough to handle a Drone in 0.3s, slowly chip a Fighter
```

**Tactical role:** The player cannot simultaneously face the Carrier and cover their
flanks. The Auto Turret handles Fighters that get behind you during Carrier fights.
Without it, a common death scenario is: player locked onto Carrier, Fighter sneaks
behind, destroys player before the Carrier dies.

#### Shield Booster
Doubles player shield recharge rate for 8 seconds. Does not add HP — repositions
the defensive curve.

```cpp
constexpr float SHIELD_BOOST_DURATION    = 8.0f;   // seconds
constexpr float SHIELD_BOOST_MULTIPLIER  = 2.0f;   // recharge rate multiplier
constexpr float SHIELD_BOOST_COOLDOWN    = 20.0f;  // seconds before reusable
```

**Design constraint:** Doubling recharge rate during a wave of Fighters is very
effective — sectors recover quickly between hits. Against a Carrier's 100+ DPS
broadside, doubling 8 HP/s to 16 HP/s is meaningless — the Carrier is still dealing
6× more damage than the shield can absorb. This is intentional. Shield Booster helps
with sustained attrition, not burst encounters.

#### EMP
Area stun within 50 world units. Does not damage — stuns enemy weapons and movement
for 4 seconds. High skill ceiling, devastating when timed correctly.

```cpp
constexpr float EMP_RADIUS          = 50.0f;
constexpr float EMP_STUN_DURATION   = 4.0f;   // seconds
constexpr float EMP_COOLDOWN        = 12.0f;  // seconds before reusable
```

**Optimal use:** Carrier fights with escort. Fire EMP when escorts are within 50
units — stuns escort weapons for 4 seconds while player focuses all fire on the
Carrier. Without the escort threat, the player can maintain Beam Laser lock on the
Carrier for the full 4 seconds without breaking off. This is the highest-skill play
in the game.

**Ineffective use:** Against the Carrier alone (the Carrier still fires back during
the stun — EMP does not stun the Carrier itself, only craft within radius smaller
than the Carrier). Against spread-out enemies where fewer than 2 fall within 50 units.

---

## Weapon / Target Matrix

Rate each combination: ★★★ optimal, ★★ viable, ★ inefficient, ✗ counterproductive

| | Drone | Seeder | Fighter | Bomber | Carrier | Turret |
|---|---|---|---|---|---|---|
| **Cannon** | ★★★ | ★★★ | ★★★ | ★★ | ★★ | ★★★ |
| **Plasma Cannon** | ★★ | ★★ | ★★★ | ★★ | ★★ | ★★ |
| **Beam Laser** | ★ | ★ | ★★ | ★★★ | ★★★ | ★★ |
| **Standard Missile** | ★ | ★★ | ★★ | ★★★ | ★★★ | ★★ |
| **Cluster Missile** | ★★★ | ★★★ | ★★ | ★ | ✗ | ★ |
| **Auto Turret** | ★★★ | ★★ | ★★ | ★ | ✗ | ✗ |
| **EMP** | ★★ | ★ | ★★★ | ★★ | ★ | ✗ |
| **Shield Booster** | ★★★ | ★★ | ★★★ | ★★ | ★ | ★★ |

---

## Player Combat Survivability

Player shields are directional (four sectors). Each sector has 100 HP.
Total shield capacity: 400 HP across all sectors.

```
Fighter attack DPS against player:   ~25 DPS
  → One Fighter depletes one sector in 4 seconds sustained fire
  → Player has time to evade or return fire before hull damage

Bomber attack DPS against player:    ~40 DPS
  → Bomber depletes one sector in 2.5 seconds — dangerous if ignored

Carrier attack DPS against player:   ~100 DPS
  → Carrier strips one sector in 1 second — immediate priority target
```

**Survivability design intent:** A player can survive a Fighter engagement with
damage to one or two shield sectors but hull intact. A Bomber attack demands
immediate response — sustained Bomber fire will reach hull within 5-7 seconds.
A Carrier hit must be avoided at all costs — no shield sector survives more than
one second of Carrier fire.

---

## Tuning Process for Playtesting

When Phase 3 combat is implemented, follow this process:

### Step 1 — Isolated enemy testing
Spawn one enemy type in DEV_MODE. Fight it. Time the kill.
Expected TTK should match the target table above within ±15%.
If not, adjust the TTK target for that enemy (not individual HP values), then
re-derive HP values using the formula.

### Step 2 — Weapon isolation testing
Fight one enemy type using only one weapon. Verify the ★★★ weapons feel clearly
superior to ★ weapons against that target. If a ★ weapon feels fine, the design
gap is not wide enough — either reduce the ★ weapon damage or increase the ★★★ weapon.

### Step 3 — Mixed wave testing
Run Wave 1 (Drones + Seeders), Wave 3 (Fighters), Wave 5 (Bombers), Wave 7 (Carrier).
Each wave should feel distinctly different in terms of what the player needs to do.
If Wave 5 feels like Wave 3 with more clicks, the Bomber is not differentiated enough.

### Step 4 — Missile selection testing
At the wave-start selection screen, make an intentionally wrong choice (Cluster vs
Bombers, Standard vs Drone wave). The wrong choice should feel noticeably harder but
not impossible. If it feels identical, the differentiation is insufficient. If it
feels unwinnable, the differentiation is too aggressive.

### Step 5 — Full playthrough balance
Play from Wave 1 to Wave 7 continuously. The difficulty curve should feel like:
  Waves 1–2: Learning the Newtonian controls, enemies are not a real threat
  Waves 3–4: Fighters create real pressure, evasive flying becomes necessary
  Waves 5–6: Bomber + Fighter mix, missile selection matters for the first time
  Wave 7+:   Carrier present, all weapons needed, EMP timing becomes decisive

### Adjustment heuristics
- "Everything dies too fast" → increase all TTK targets by 25%, re-derive
- "Everything is a slog" → decrease Bomber/Carrier TTK targets by 20%, re-derive
- "Missiles feel the same" → increase damage gap between Standard and sub damage
- "Cluster is never worth choosing" → increase CLUSTER_SUB_NAV_N or add 2 submunitions
- "Standard is always better" → reduce MISSILE_AMMO to 12, increase CLUSTER_AMMO to 14
- "Shield Booster is useless" → increase SHIELD_BOOST_MULTIPLIER to 3.0
- "Shield Booster is broken" → reduce SHIELD_BOOST_DURATION to 5.0s

---

## What Needs Implementing (Phase 3 Checklist)

- [ ] `Entity.hpp` — add `hullHP`, `shieldHP`, `shieldRechargeDelay`,
      `shieldRechargeRate`, `timeSinceHit` to base struct
- [ ] `Enemy.cpp` — initialise HP values from Config constants per enemy type
- [ ] `WeaponSystem.cpp` — implement all 9 weapon types with correct damage values
- [ ] `Projectile.hpp` — add `damage`, `blastRadius`, `isContinuous` fields
- [ ] `AutoTurret.cpp` — independent targeting loop using SpatialGrid
- [ ] `MissileGuidance.hpp` — proportional navigation with configurable N and turn rate
- [ ] Cluster submunition split logic — spawn 4 projectiles at proximity trigger
- [ ] Round-start missile selection UI — radar report display, FIRE to confirm
- [ ] `ShieldSystem.cpp` — directional sector damage routing for player
- [ ] Enemy shield sectors — simple omnidirectional (Fighter/Bomber), four-sector (Carrier)
- [ ] Beam Laser shield suppression — set `timeSinceHit` to 0 each tick while beam on target
- [ ] EMP area stun — flag all entities within radius as stunned for EMP_STUN_DURATION

