# terra-siege — Game Modes and New Features Design

## Document Purpose

This document specifies all new game mode and feature additions planned for
terra-siege. All design questions have been resolved. Remaining tuning values
are marked `// TUNE` for playtesting adjustment.

Read alongside `readme_rebuild.md`, `combat_tuning.md`, and `CLAUDE.md`.
Implement after the rebuild phases (flight, terrain, camera) are complete
and Phase 3 wave mode combat is stable.

---

## Resolved Design Decisions Summary

| Question | Decision |
|----------|----------|
| Player bases | Multiple per side, same rules as enemy bases |
| Base counts | Normal: 20v20, Easy: 25v15, Hard: 15v25 |
| Base placement | Random, player cluster near centre, enemy on outskirts |
| Base building | Defend and capture only (build new bases deferred to later) |
| Infected base behaviour | Immediately active — collects, defends, produces |
| Re-infection | Once turned, a unit cannot be turned again |
| Reboot period | 2-4 seconds confusion before allegiance switch (using 3.0s) |
| Player ship infection | Cannot be infected — only damaged |
| Resource types | Two: Metal and Bio-matter |
| Drone type | All drones identical, 60s cooldown or resource-gated, expendable |
| Drone calling | One at a time from nearest base with resources |
| Primary weapons | Four weapons, shared energy pool, player switches with key |
| Wreckage sources | Carriers and Bombers only |
| Wreckage decay | Disappears after time, collectible by either faction |
| Game over | Losing all friendly bases = game over |
| Score | Friendly base count vs enemy base count, live ratio |
| Production | Fully autonomous except drones (callable) |
| Resource sharing | Auto when below 20% resources AND below 30% hull AND no nearby enemies |
| Reinforcements | Both factions call reinforcements from nearest base when attacked |
| Reinforce range | To be trialled — starting value 600 units |
| Primary weapon switch | Single key (KEY_R) cycles all four primaries |
| Energy pool | Shared, 120 units, 15/sec recharge |
| Drone travel | 90 units/sec — faster than player to catch up |
| Wreckage decay | 5 minutes (300s) |
| Builder scope | Build radar rings + repair base hull |
| Infected ship AI | Brief reboot then independently attacks former allies |
| Friendly fire | Reboot period = confusion period, no targeting during it |
| Map layout | World split in half along Z — friendly south, enemy north (two fronts) |

---

## Part 1 — Game Modes

### 1.1 Mode and Difficulty Selection

At game start the player selects mode and difficulty. Both affect base counts
in base mode. Wave mode difficulty uses the existing aggression/recharge
multipliers from `combat_tuning.md`.

```cpp
enum class GameMode {
    WaveMode,    // Original — defend against scripted waves
    BaseMode,    // Strategic — destroy or infect all enemy bases
};

enum class Difficulty {
    Easy,    // 25 friendly bases, 15 enemy bases
    Normal,  // 20 friendly bases, 20 enemy bases
    Hard,    // 15 friendly bases, 25 enemy bases
};
```

### 1.2 Wave Mode

Unchanged from `combat_tuning.md`. No modifications.

**Win:** Survive all waves.
**Loss:** All friendly units destroyed or player destroyed.

### 1.3 Base Mode

**Win condition:** All enemy bases destroyed or infected.
**Loss condition:** All friendly bases destroyed OR player ship destroyed.

**Score:** Live ratio displayed as `[Friendly bases] vs [Enemy bases]`.
Updated instantly when any base changes state. No cumulative score —
only the current ratio matters.

---

## Part 2 — Base System

> **⚠ Superseded (2026-06-02).** Part 2 below describes the original symmetric 20v20 Base Mode design. The current Slice C plan replaces this with an asymmetric defender-vs-invader shape — see [base_mode_v2.md](base_mode_v2.md) for the active design. The content below is kept as historical reference; the resource system (Part 4), infection rules for ships (Part 5), orbit drones (Part 6), weapons (Part 7), helicopter mode (Part 8), and damage visuals (Part 9) all still apply unchanged.

### 2.1 Base Properties

Both factions share the same base structure. The only differences are
faction colour, initial placement, and starting allegiance.

```cpp
struct Base {
    Vector3      position;
    Faction      faction;           // Friendly, Enemy, or Neutral (post-infection)
    float        hullHP;
    float        maxHullHP;         // Config::BASE_HULL_HP
    float        shieldHP[4];       // Four-sector directional, same system as Carrier
    float        maxShieldHP;       // Config::BASE_SHIELD_HP per sector

    // Resource pool
    float        metal;
    float        bioMatter;
    float        metalCapacity;     // Config::BASE_METAL_CAPACITY
    float        bioCapacity;       // Config::BASE_BIO_CAPACITY

    // State
    bool         infected;          // true = recently flipped, recovering
    float        recoverTimer;      // counts to Config::BASE_INFECTION_RECOVER
    bool         underAttack;       // true = enemy within BASE_THREAT_RADIUS
    float        threatTimer;       // time since last damage — for resource sharing
    bool         requestedReinforce;

    // Radar ring progress
    int          innerRadarsBuilt;  // 0..8
    int          outerRadarsBuilt;  // 0..BASE_RADAR_OUTER_COUNT
    bool         innerRingComplete;
    bool         outerRingComplete;

    // Production queues (autonomous)
    float        droneProductionTimer;
    float        fighterProductionTimer;
    float        collectorProductionTimer;
    float        builderProductionTimer;
    int          dronesAvailable;   // waiting at base for player to call
};
```

```cpp
namespace Config {
    // Base HP
    constexpr float BASE_HULL_HP        = 800.0f;   // TUNE
    constexpr float BASE_SHIELD_HP      = 300.0f;   // per sector, TUNE
    constexpr float BASE_SHIELD_REGEN   = 15.0f;    // HP/sec
    constexpr float BASE_SHIELD_DELAY   = 5.0f;     // sec after hit

    // Base resources
    constexpr float BASE_METAL_CAPACITY   = 400.0f;
    constexpr float BASE_BIO_CAPACITY     = 300.0f;

    // Base geometry
    constexpr float BASE_RADIUS          = 30.0f;   // world units footprint
    constexpr float BASE_MIN_SPACING     = 500.0f;  // min distance between any two bases
    constexpr float BASE_FRONT_BUFFER    = 400.0f;  // no-man's land at each front, TUNE
    constexpr float BASE_THREAT_RADIUS   = 250.0f;  // enemy within = underAttack
    constexpr float BASE_REINFORCE_RANGE = 600.0f;  // TUNE — trial different base distances
    constexpr int   BASE_REINFORCE_COUNT = 3;       // fighters sent per call, TUNE
} // namespace Config
```

### 2.2 Base Placement Algorithm

```cpp
void BaseMode::placeBases(uint32_t seed, Difficulty diff, Planet& planet)
{
    int friendlyCount = 20, enemyCount = 20;
    if      (diff == Difficulty::Easy) { friendlyCount = 25; enemyCount = 15; }
    else if (diff == Difficulty::Hard) { friendlyCount = 15; enemyCount = 25; }

    float ws     = planet.worldSize();
    float buffer = Config::BASE_FRONT_BUFFER;   // no-man's land at each front

    // --- Split the world into two halves along the Z axis ---
    // Friendly bases occupy the south half: z in [buffer, ws/2 - buffer]
    // Enemy bases occupy the north half:    z in [ws/2 + buffer, ws - buffer]
    // X spans the full world width for both factions.
    //
    // NOTE: the world is toroidal, so there are TWO front lines:
    //   1. The middle front at z = ws/2
    //   2. The wrap-seam front where z=0 meets z=ws
    // Both have a 2*buffer no-man's land. This creates two natural battle fronts.

    placeInBand(friendlyCount,
                /*xMin*/ buffer,        /*xMax*/ ws - buffer,
                /*zMin*/ buffer,        /*zMax*/ ws * 0.5f - buffer,
                Faction::Friendly, seed);

    placeInBand(enemyCount,
                /*xMin*/ buffer,        /*xMax*/ ws - buffer,
                /*zMin*/ ws * 0.5f + buffer, /*zMax*/ ws - buffer,
                Faction::Enemy, seed ^ 0xDEAD);
}

// placeInBand scatters `count` bases within the rectangular band, enforcing
// BASE_MIN_SPACING between any two bases, on grassland terrain only, using
// seeded RNG so the same seed reproduces the same layout. Rejection-samples
// candidate positions until count is placed or max attempts exceeded.
```

Both placement functions enforce:
- `BASE_MIN_SPACING` between any two bases
- Terrain height in grassland band (0.25–0.50 normalised)
- Not within 100 units of a resource node (nodes cluster near bases naturally)
- Uses seeded RNG so the same seed always produces the same layout

### 2.3 Base Production (Autonomous)

All base production is autonomous. The base updates its timers each tick and
spawns units when resources are available and timer is ready.

```cpp
void Base::updateProduction(float dt, Planet& planet)
{
    // Fighter production
    fighterProductionTimer += dt;
    if (fighterProductionTimer >= Config::BASE_FIGHTER_PERIOD
        && bioMatter >= Config::FIGHTER_BIO_COST)
    {
        spawnFighter();
        bioMatter -= Config::FIGHTER_BIO_COST;
        fighterProductionTimer = 0.0f;
    }

    // Collector production
    collectorProductionTimer += dt;
    if (collectorProductionTimer >= Config::BASE_COLLECTOR_PERIOD
        && bioMatter >= Config::COLLECTOR_BIO_COST
        && activeCollectors() < Config::BASE_MAX_COLLECTORS)
    {
        spawnCollector();
        bioMatter -= Config::COLLECTOR_BIO_COST;
        collectorProductionTimer = 0.0f;
    }

    // Builder production
    if (!hasActiveBuilder() && metal >= Config::BUILDER_METAL_COST)
    {
        spawnBuilder();
        metal -= Config::BUILDER_METAL_COST;
    }
    // Note: radar stations cost RADAR_STATION_METAL_COST, deducted when builder
    // completes each station (not at builder spawn).

    // Drone production (stockpile — not deployed automatically)
    droneProductionTimer += dt;
    if (droneProductionTimer >= Config::DRONE_PRODUCTION_PERIOD
        && dronesAvailable < Config::DRONE_MAX_AT_BASE
        && metal >= Config::DRONE_METAL_COST
        && bioMatter >= Config::DRONE_BIO_COST)
    {
        dronesAvailable++;
        metal    -= Config::DRONE_METAL_COST;
        bioMatter -= Config::DRONE_BIO_COST;
        droneProductionTimer = 0.0f;
    }
}
```

```cpp
namespace Config {
    // Production timers (seconds between spawns)
    constexpr float BASE_FIGHTER_PERIOD    = 45.0f;   // TUNE
    constexpr float BASE_COLLECTOR_PERIOD  = 60.0f;   // TUNE
    constexpr float DRONE_PRODUCTION_PERIOD = 60.0f;  // or resource-gated
    constexpr int   BASE_MAX_COLLECTORS    = 3;
    constexpr int   DRONE_MAX_AT_BASE      = 12;

    // Production resource costs
    // Bio-matter powers ship production; Metal powers construction/repair;
    // drones cost both (metal frame + bio systems).
    constexpr float FIGHTER_BIO_COST       = 50.0f;   // ships are produced from bio
    constexpr float BOMBER_BIO_COST        = 90.0f;   // larger ship
    constexpr float COLLECTOR_BIO_COST     = 30.0f;   // cheap support unit
    constexpr float BUILDER_METAL_COST     = 70.0f;   // construction vehicle = metal
    constexpr float RADAR_STATION_METAL_COST = 35.0f; // built by builder
    constexpr float DRONE_METAL_COST       = 25.0f;   // frame
    constexpr float DRONE_BIO_COST         = 15.0f;   // systems
    constexpr float REPAIR_METAL_PER_10HP  = 1.0f;    // hull repair cost
} // namespace Config
```

### 2.4 Inter-Base Resource Sharing

When a base is under attack (enemy within BASE_THREAT_RADIUS), has taken
damage, and has no enemy ships within its radar coverage, it broadcasts
a resource request to all friendly bases within BASE_REINFORCE_RANGE.

Neighbour bases with surplus (above 50% capacity) send a collector carrying
their excess to the requesting base. This is entirely automatic — no player
input required.

```cpp
void Base::updateResourceSharing(float dt, std::vector<Base>& allBases)
{
    // Request only when: low on resources AND hull damaged AND safe to receive
    // Thresholds: below 20% resources, below 30% hull, no nearby enemies
    bool lowResources = (metal < metalCapacity * 0.20f ||
                         bioMatter < bioCapacity * 0.20f);
    bool hullDamaged  = (hullHP < maxHullHP * 0.30f);
    bool safeToReceive = !underAttack && threatTimer > 10.0f;
    bool needsResources = lowResources && hullDamaged;

    if (!needsResources || !safeToReceive) return;

    for (auto& neighbour : allBases)
    {
        if (neighbour.faction != faction) continue;
        if (Vector3Distance(position, neighbour.position)
            > Config::BASE_REINFORCE_RANGE) continue;

        // Neighbour shares half its surplus
        if (neighbour.metal > neighbour.metalCapacity * 0.5f)
        {
            float share = (neighbour.metal - neighbour.metalCapacity * 0.5f) * 0.5f;
            // Spawn a collector carrying the share from neighbour to this base
            neighbour.spawnResourceCollector(position, share, 0.0f);
            neighbour.metal -= share;
        }
        // Same for bioMatter
    }
}
```

### 2.5 Reinforcement Calls

When a base takes damage from an enemy attack, it calls for reinforcements
from nearby friendly bases:

```cpp
void Base::onDamaged(float damage, Vector3 attackerPos,
                     std::vector<Base>& allBases)
{
    hullHP -= damage;
    underAttack   = true;
    threatTimer   = 0.0f;

    if (!requestedReinforce)
    {
        requestedReinforce = true;
        // Find nearest friendly base within range
        for (auto& b : allBases)
        {
            if (b.faction != faction) continue;
            if (&b == this) continue;
            float d = Vector3Distance(position, b.position);
            if (d < Config::BASE_REINFORCE_RANGE)
            {
                // Send fighters if available
                int toSend = std::min(b.availableFighters(),
                                      Config::BASE_REINFORCE_COUNT);
                b.dispatchFighters(toSend, position);
            }
        }
    }
}
```

Reinforcement cooldown: after sending, the responding base cannot send
reinforcements again for 90 seconds. `// TUNE`

### 2.6 Base Infection

When an infectious missile hits a base with all four shield sectors depleted:

```cpp
void Base::infect()
{
    faction       = (faction == Faction::Enemy) ? Faction::Friendly
                                                : Faction::Enemy;
    infected      = true;
    recoverTimer  = 0.0f;

    // Immediately begin recovery — shields and hull restore over time
    // Builder continues working (radar stations change colour, stay functional)
    // All ships already spawned by this base retain their old faction
    // New ships produced after infection are the new faction
    requestedReinforce = false;
}

void Base::updateInfection(float dt)
{
    if (!infected) return;
    recoverTimer += dt;

    // Shield and hull recovery during infection period
    float progress = recoverTimer / Config::BASE_INFECTION_RECOVER;
    for (int i = 0; i < 4; ++i)
        shieldHP[i] = std::min(shieldHP[i] + Config::BASE_SHIELD_REGEN * dt,
                               maxShieldHP);

    hullHP = std::min(hullHP + Config::BASE_HULL_REGEN * dt, maxHullHP);

    if (recoverTimer >= Config::BASE_INFECTION_RECOVER)
        infected = false;   // fully recovered
}
```

```cpp
namespace Config {
    constexpr float BASE_INFECTION_RECOVER = 60.0f;  // seconds to full recovery
    constexpr float BASE_HULL_REGEN        = 8.0f;   // HP/sec during recovery, TUNE
} // namespace Config
```

**Score update:** Infection immediately updates the score ratio on the HUD.
The base count shifts the moment `infect()` is called.

### 2.7 Base Destruction

```cpp
void Base::onDestroyed()
{
    // Large explosion particle effect
    // Spawn wreckage node if base was a Carrier-scale structure (always true)
    spawnWreckageNode(position, Config::BASE_WRECKAGE_METAL_YIELD);

    // All spawned ships from this base become leaderless:
    // - Fighters: continue attacking player (aggression state)
    // - Bombers: disengage, fly to nearest friendly base
    // - Collectors: stop, become harvestable wreckage after 30s
    // - Builders: stop, become terrain debris

    // Score updates immediately
}
```

```cpp
namespace Config {
    constexpr float BASE_WRECKAGE_METAL_YIELD = 120.0f;  // TUNE
    constexpr float BASE_WRECKAGE_DECAY       = 300.0f;  // 5 minutes
} // namespace Config
```

---

## Part 3 — Builder Units and Radar Rings

### 3.1 Builder Behaviour

Each base has one active builder at a time. Builder scope:
- Constructs radar stations in inner ring (8 stations) then outer ring
- Repairs base hull damage when no radar slots remain unbuilt
  (builder drives around the base structure, hull HP restores at 5 HP/sec)
- If builder is destroyed, base produces a replacement when metal allows

```cpp
enum class BuilderState {
    Idle,
    DrivingToRadarSlot,
    BuildingRadar,
    DrivingToBase,
    RepairingBase,
};
```

Builder speed: `Config::BASE_BUILDER_SPEED = 8.0f` world units/sec.
Build time per radar: `Config::BASE_BUILD_TIME = 8.0f` seconds.
Repair rate when no radar slots remain: `Config::BUILDER_REPAIR_RATE = 5.0f` HP/sec.

### 3.2 Radar Ring Construction

```cpp
// Pre-calculate all radar slot positions on base spawn
void Base::initRadarSlots()
{
    // Inner ring — 8 stations
    for (int i = 0; i < Config::BASE_RADAR_INNER_COUNT; ++i)
    {
        float angle = i * (2.0f * PI / Config::BASE_RADAR_INNER_COUNT);
        radarSlots[i] = {
            position.x + sinf(angle) * Config::BASE_RADAR_INNER_RADIUS,
            position.y,
            position.z + cosf(angle) * Config::BASE_RADAR_INNER_RADIUS
        };
    }
    // Outer ring — additional stations
    for (int i = 0; i < Config::BASE_RADAR_OUTER_COUNT; ++i)
    {
        float angle = i * (2.0f * PI / Config::BASE_RADAR_OUTER_COUNT);
        radarSlots[Config::BASE_RADAR_INNER_COUNT + i] = {
            position.x + sinf(angle) * Config::BASE_RADAR_OUTER_RADIUS,
            position.y,
            position.z + cosf(angle) * Config::BASE_RADAR_OUTER_RADIUS
        };
    }
}
```

Radar station detection ranges:
```
No radars:          spawn/detect range = 150 units
Inner ring done:    spawn/detect range = 350 units
Both rings done:    spawn/detect range = 600 units
```

Infected/destroyed radar stations: builder replaces destroyed ones.
Infected stations show in player's radar range extension (green).

```cpp
namespace Config {
    constexpr float BASE_RADAR_INNER_RADIUS  = 120.0f;
    constexpr float BASE_RADAR_OUTER_RADIUS  = 220.0f;
    constexpr int   BASE_RADAR_INNER_COUNT   = 8;
    constexpr int   BASE_RADAR_OUTER_COUNT   = 12;      // TUNE
    constexpr float BASE_BUILDER_SPEED       = 8.0f;
    constexpr float BASE_BUILD_TIME          = 8.0f;    // seconds per radar
    constexpr float BUILDER_REPAIR_RATE      = 5.0f;    // hull HP/sec
} // namespace Config
```

---

## Part 4 — Resource System

### 4.1 Resource Types

Two resource types. Simple — no refinement required.

| Resource | Harvested From | Powers |
|----------|---------------|--------|
| **Metal** | Rock formations, wreckage | Ship repair, base construction, builder units, orbit drones |
| **Bio-matter** | Tree clusters | Ship production, drone production |

Both types are tracked separately at each base and for the player in base mode.

### 4.2 Resource Nodes

```cpp
enum class ResourceNodeType {
    RockFormation,  // Metal, yield 40, respawn 120s
    TreeCluster,    // Bio-matter, yield 20, respawn 60s
    CrystalDeposit, // Metal (high grade), yield 80, respawn 180s  ← TUNE type if needed
    Wreckage,       // Metal, yield varies, no respawn (decays instead)
};

struct ResourceNode {
    Vector3          position;
    ResourceNodeType type;
    float            metalRemaining;
    float            bioRemaining;
    float            maxMetal;
    float            maxBio;
    float            respawnTimer;   // counts up when depleted
    float            decayTimer;     // for wreckage only
    bool             depleted;
    bool             isWreckage;
};
```

**Wreckage nodes** (Carriers, Bombers, destroyed bases):
- Spawn at death position
- Yield: Carrier = 100 Metal, Bomber = 50 Metal, Base = 120 Metal
- Decay timer: disappear after `Config::WRECKAGE_DECAY_TIME = 180.0f` seconds
- Collectible by either faction's collectors — first to arrive gets it
- No respawn

```cpp
namespace Config {
    // Resource node yields
    constexpr float ROCK_METAL_YIELD       = 40.0f;
    constexpr float ROCK_RESPAWN           = 120.0f;  // seconds
    constexpr float TREE_BIO_YIELD         = 20.0f;
    constexpr float TREE_RESPAWN           = 60.0f;
    constexpr float CRYSTAL_METAL_YIELD    = 80.0f;
    constexpr float CRYSTAL_RESPAWN        = 180.0f;

    // Wreckage
    constexpr float WRECKAGE_CARRIER_YIELD = 100.0f;
    constexpr float WRECKAGE_BOMBER_YIELD  = 50.0f;
    constexpr float WRECKAGE_BASE_YIELD    = 120.0f;
    constexpr float WRECKAGE_DECAY_TIME    = 300.0f;  // 5 minutes

    // Collector
    constexpr float COLLECTOR_CARGO_METAL_MAX = 50.0f;
    constexpr float COLLECTOR_CARGO_BIO_MAX   = 50.0f;
    constexpr float COLLECTOR_HARVEST_TIME    = 3.0f;  // seconds at node
    constexpr int   BASE_MAX_COLLECTORS       = 3;
} // namespace Config
```

### 4.3 Player Resource Pool (Base Mode Only)

In base mode the player maintains their own small resource pool.
In wave mode resources are not tracked — simpler ammo/energy system applies.

```cpp
namespace Config {
    constexpr float PLAYER_METAL_START      = 100.0f;
    constexpr float PLAYER_BIO_START        = 80.0f;
    constexpr float PLAYER_METAL_MAX        = 200.0f;
    constexpr float PLAYER_BIO_MAX          = 150.0f;
    constexpr float PLAYER_HARVEST_METAL    = 20.0f;  // fly-through harvest
    constexpr float PLAYER_HARVEST_BIO      = 15.0f;
    constexpr float BASE_SUPPLY_RANGE       = 80.0f;  // world units from base
    constexpr float BASE_SUPPLY_RATE_METAL  = 5.0f;   // units/sec when in range
    constexpr float BASE_SUPPLY_RATE_BIO    = 3.0f;

    // Resource costs for special weapons (base mode only)
    constexpr float INFECT_MISSILE_METAL_COST  = 30.0f;
    constexpr float SHIELD_MISSILE_METAL_COST  = 15.0f;
} // namespace Config
```

**Fly-through harvest:** When the player flies within 5 world units of a
resource node, they automatically harvest a fixed amount. No stopping required.
The node's remaining yield decreases accordingly.

**Base supply:** When the player is within `BASE_SUPPLY_RANGE` of a friendly
base with resources available, metal and bio-matter trickle into the player's
pool automatically. Rate scales down if base is low.

---

## Part 5 — Infection System

### 5.1 Infection Rules

- Any entity can be infected **once**. After infection it cannot be turned again.
- Infectious missile only works on ships/units with shields fully depleted.
- The player's ship cannot be infected — only damaged.
- Bases can be infected when all four shield sectors are at zero.
- Radar stations can be infected at any time (no shields).

```cpp
struct InfectionState {
    bool  canBeInfected;    // false after first infection — permanent
    bool  isRebooting;      // true during confusion period
    float rebootTimer;      // counts down from INFECT_REBOOT_DURATION
    bool  infected;         // true = allegiance has flipped
};
```

### 5.2 Reboot Period

When an entity is infected, it enters a reboot state for
`Config::INFECT_REBOOT_DURATION = 3.0f` seconds. During reboot:
- Entity stops all movement
- Entity does not fire
- Entity does not respond to damage (immune during reboot — represents
  the moment of transition)
- Visual: brief flicker between faction colours
- Radar blip: alternates between red and green rapidly

After reboot completes:
- `isRebooting = false`
- `infected = true`
- Entity immediately begins targeting former allies
- `canBeInfected = false` — permanent, cannot be flipped back

```cpp
namespace Config {
    constexpr float INFECT_REBOOT_DURATION = 3.0f;  // seconds — trial range 2.0-4.0
} // namespace Config
```

### 5.3 Infected Ship Behaviour

Infected enemy ships (now friendly): fly independently, attack nearest
enemy. They do NOT join player formations or fly to bases. They fight
where they are until destroyed. They are not replaced when destroyed.

Infected enemy ships have permanent penalties:
- Speed: 80% of normal (`Config::INFECT_SPEED_PENALTY = 0.80f`)
- No shields (shields destroyed by the stripping process before infection)
- Hull HP: unchanged from moment of infection

Infected friendly ships (turned enemy by hypothetical future mechanic —
currently blocked since once infected cannot be re-infected, but structure
supports it if rules change).

### 5.4 Infected Base Production

An infected enemy base:
- Immediately becomes `Faction::Friendly`
- Score ratio updates immediately
- Begins 60-second recovery (hull and shields restore over this period)
- Builder continues building — radar stations switch to friendly colour
- All ships/units that were already spawned retain their original faction
- All new ships produced after infection are friendly
- Resources already in pool are retained and used for friendly production

---

## Part 6 — Orbit Drone System

### 6.1 Drone Properties

All drones are identical. No variants.

```cpp
struct OrbitDrone {
    Vector3  position;
    float    orbitAngle;       // current angle in orbit
    float    hullHP;           // Config::DRONE_HP = 25.0f
    float    fireCooldown;     // time until next shot
    bool     alive;
    bool     travelling;       // true when flying from base to player
    Vector3  travelTarget;     // player position at time of dispatch
};
```

### 6.2 Drone Calling

Player calls a drone with a dedicated key (suggest: `KEY_TAB` or `KEY_X`).

```
On call:
1. Check nearest friendly base with dronesAvailable > 0
2. Check if cooldown timer has expired (Config::DRONE_CALL_COOLDOWN = 60s)
   OR if base has enough resources (metal >= Config::DRONE_METAL_COST)
3. If both conditions met: dispatch one drone from that base
4. Drone flies toward player at Config::DRONE_TRAVEL_SPEED
5. On arrival: enters orbit at next available slot
6. Cooldown timer resets
```

Up to 4 drones orbit the player simultaneously. Slots are evenly distributed:
- 1 drone: 1 orbit position (offset 0°)
- 2 drones: 2 positions (offset 180° each)
- 3 drones: 3 positions (offset 120° each)
- 4 drones: 4 positions (offset 90° each)

Drones are expendable — when destroyed they are not replaced automatically.
The player must call another from a base.

```cpp
namespace Config {
    constexpr float DRONE_CALL_COOLDOWN    = 60.0f;   // seconds
    constexpr float DRONE_TRAVEL_SPEED     = 90.0f;   // faster than player max (70) to catch up
    constexpr float DRONE_ORBIT_RADIUS     = 12.0f;
    constexpr float DRONE_ORBIT_HEIGHT     = 2.0f;    // above player ship
    constexpr float DRONE_ORBIT_SPEED      = 1.2f;    // rad/sec
    constexpr float DRONE_HP               = 25.0f;
    constexpr float DRONE_FIRE_RATE        = 1.5f;    // seconds between shots
    constexpr float DRONE_LASER_RANGE      = 40.0f;
    constexpr float DRONE_LASER_DAMAGE     = 8.0f;
    constexpr float DRONE_LASER_DURATION   = 0.12f;   // visual flash seconds
    constexpr int   DRONE_MAX_ORBITING     = 4;       // max around player
    constexpr int   DRONE_MAX_AT_BASE      = 12;      // max stockpile at base
} // namespace Config
```

### 6.3 Drone Orbit Mechanics

```cpp
void OrbitDrone::update(float dt, Vector3 playerPos, EntityManager& entities)
{
    if (travelling)
    {
        // Fly toward player
        Vector3 toPlayer = Vector3Subtract(playerPos, position);
        float dist = Vector3Length(toPlayer);
        if (dist < 2.0f)
        {
            travelling = false;  // arrived — enter orbit
        }
        else
        {
            Vector3 dir = Vector3Scale(toPlayer, 1.0f / dist);
            position = Vector3Add(position, Vector3Scale(dir,
                       Config::DRONE_TRAVEL_SPEED * dt));
        }
        return;
    }

    // Orbit update
    orbitAngle += Config::DRONE_ORBIT_SPEED * dt;
    position = {
        playerPos.x + cosf(orbitAngle) * Config::DRONE_ORBIT_RADIUS,
        playerPos.y + Config::DRONE_ORBIT_HEIGHT,
        playerPos.z + sinf(orbitAngle) * Config::DRONE_ORBIT_RADIUS
    };

    // Fire at nearest enemy
    fireCooldown -= dt;
    if (fireCooldown <= 0.0f)
    {
        Entity* target = entities.nearestEnemy(position, Config::DRONE_LASER_RANGE);
        if (target)
        {
            fireLaser(target);
            fireCooldown = Config::DRONE_FIRE_RATE;
        }
    }
}
```

---

## Part 7 — New Weapons

### 7.1 Primary Weapon Slot — Four Weapons

Player cycles between all four primaries with a single key: `KEY_R`.
Each press advances Cannon → Plasma → Beam Laser → Shield Laser → Cannon.
The active weapon is shown on the HUD. All four share a single energy pool.

```cpp
enum class PrimaryWeapon {
    Cannon,       // kinetic — no energy cost
    PlasmaCannon, // energy cost per shot
    BeamLaser,    // continuous energy drain, suppresses shield regen
    ShieldLaser,  // continuous energy drain, drains target shields
};
```

**Shared energy pool:**
```cpp
namespace Config {
    constexpr float PRIMARY_ENERGY_MAX      = 120.0f;  // one substantial action, not two
    constexpr float PRIMARY_ENERGY_RECHARGE = 15.0f;  // units/sec when idle
    // At 120 pool: Beam ~5.5s continuous; Shield Laser strips a Carrier sector
    // (~75 energy) leaving little for Beam — forces strip-OR-suppress choice.
    // Recharge from empty: 120/15 = 8s.

    // Energy costs
    constexpr float PLASMA_ENERGY_PER_SHOT  = 8.0f;
    constexpr float BEAM_ENERGY_DRAIN_PS    = 22.0f;
    constexpr float SHIELD_LASER_ENERGY_PS  = 18.0f;
    // Cannon: 0 energy cost
} // namespace Config
```

Energy recharges only when no energy-consuming weapon is firing. The player
manages energy across all three energy weapons — draining it fully on Beam
Laser means no Plasma shots or Shield Laser until it recharges.

### 7.2 Shield Laser (4th Primary)

Drains target shield HP rapidly with minimal hull damage. The setup weapon
for the infection combo — strip shields with Shield Laser, then infect.

```cpp
namespace Config {
    constexpr float SHIELD_LASER_SHIELD_DRAIN_PS = 60.0f;  // TUNE
    constexpr float SHIELD_LASER_HULL_DRAIN_PS   = 3.0f;
    constexpr float SHIELD_LASER_RANGE           = 160.0f;
} // namespace Config
```

**Tactical purpose:**
- Strip Fighter shields in ~0.7s (40HP / 60 drain = 0.67s)
- Strip Bomber shield sector in ~2.5s per sector
- Strip Carrier sector in ~4.2s per sector
- Does not suppress shield regen (that is the Beam Laser's role)
- Combine with Beam Laser: Shield Laser strips, Beam Laser suppresses regen
  during hull damage phase

### 7.3 Shield Missile (Secondary)

```cpp
namespace Config {
    constexpr float SHIELD_MISSILE_SPEED      = 75.0f;
    constexpr float SHIELD_MISSILE_NAV_N      = 4.0f;
    constexpr float SHIELD_MISSILE_TURN_RATE  = 3.0f;
    constexpr float SHIELD_MISSILE_SHIELD_DMG = 80.0f;
    constexpr float SHIELD_MISSILE_HULL_DMG   = 4.0f;
    constexpr int   SHIELD_MISSILE_AMMO       = 12;
} // namespace Config
```

### 7.4 Infectious Missile (Secondary)

Only works on targets with no active shields. Triggers reboot + faction flip.

```cpp
namespace Config {
    constexpr float INFECT_MISSILE_SPEED     = 60.0f;
    constexpr float INFECT_MISSILE_NAV_N     = 4.5f;
    constexpr float INFECT_MISSILE_TURN_RATE = 3.5f;
    constexpr int   INFECT_MISSILE_AMMO      = 8;
} // namespace Config
```

**Round-start missile selection UI (four options):**
```
[ STANDARD ×16 ]  [ CLUSTER ×10 ]  [ SHIELD ×12 ]  [ INFECTIOUS ×8 ]
```

**Infection combo flowchart:**
```
Fighter with shields
    → Fire Shield Laser until shields at zero
    → Fire Infectious Missile
    → Fighter enters 3s reboot
    → Fighter attacks former allies
```

---

## Part 8 — Helicopter Flight Mode

### 8.1 Overview

Second selectable flight model. Hovering, directional movement, no gravity
management. Accessible to players who find Newtonian physics difficult.
Selected at game start alongside game mode and difficulty.

```cpp
enum class FlightModel {
    Newtonian,   // Thrust on local UP, gravity, tilt to steer
    Helicopter,  // Auto-hover, directional controls, no gravity
};
```

### 8.2 Controls

| Input | Action |
|-------|--------|
| Mouse left/right | Turn left / right (yaw) |
| Mouse forward/back | Visual pitch (cosmetic only — ship tilts to show direction) |
| W | Ascend |
| S | Descend |
| Q | Strafe left |
| E | Strafe right |
| Arrow Up / Gamepad Up | Move forward |
| Arrow Down / Gamepad Down | Move backward |
| No input | Auto-hover — velocity damps to zero |

### 8.3 Helicopter Physics

```cpp
void Player::applyHelicopterPhysics(float dt, const Planet& planet)
{
    // Horizontal movement (strafe + forward/back)
    float yaw = m_yaw;
    Vector3 fwd   = { sinf(yaw), 0.0f, cosf(yaw) };
    Vector3 right = { cosf(yaw), 0.0f, -sinf(yaw) };

    if (IsKeyDown(KEY_UP))
        m_vel = Vector3Add(m_vel, Vector3Scale(fwd, Config::HELI_ACCEL * dt));
    if (IsKeyDown(KEY_DOWN))
        m_vel = Vector3Add(m_vel, Vector3Scale(fwd, -Config::HELI_ACCEL * dt));
    if (IsKeyDown(KEY_E))
        m_vel = Vector3Add(m_vel, Vector3Scale(right, Config::HELI_ACCEL * dt));
    if (IsKeyDown(KEY_Q))
        m_vel = Vector3Add(m_vel, Vector3Scale(right, -Config::HELI_ACCEL * dt));

    // Auto-hover — strong horizontal drag when no directional input
    bool anyHoriz = IsKeyDown(KEY_UP) || IsKeyDown(KEY_DOWN)
                 || IsKeyDown(KEY_Q)  || IsKeyDown(KEY_E);
    if (!anyHoriz)
    {
        float drag = powf(Config::HELI_HOVER_DRAG, dt * 120.0f);
        m_vel.x *= drag;
        m_vel.z *= drag;
    }

    // Speed cap (horizontal only)
    float hSpd = sqrtf(m_vel.x*m_vel.x + m_vel.z*m_vel.z);
    if (hSpd > Config::HELI_MAX_SPEED)
    {
        float scale = Config::HELI_MAX_SPEED / hSpd;
        m_vel.x *= scale;
        m_vel.z *= scale;
    }

    // Vertical — W ascends, S descends, else stabilise at current AGL
    if (IsKeyDown(KEY_W))
        m_pos.y += Config::HELI_ASCEND_RATE * dt;
    else if (IsKeyDown(KEY_S))
        m_pos.y -= Config::HELI_DESCEND_RATE * dt;
    else
    {
        // Vertical stabilisation — maintain current altitude
        float groundH   = planet.heightAt(m_pos.x, m_pos.z);
        float floorH    = groundH + Config::PLAYER_MIN_ALTITUDE;
        if (m_pos.y < floorH) m_pos.y = floorH;
        m_vel.y *= powf(0.85f, dt * 120.0f);  // damp vertical drift
    }

    // Yaw from mouse
    Vector2 mouse = GetMouseDelta();
    m_yaw += mouse.x * Config::HELI_MOUSE_TURN_SENS;

    // Integrate position
    m_pos = Vector3Add(m_pos, Vector3Scale(m_vel, dt));

    // Toroidal wrap
    wrapPosition(planet.worldSize());
}
```

```cpp
namespace Config {
    constexpr float HELI_MAX_SPEED          = 35.0f;
    constexpr float HELI_ACCEL              = 20.0f;
    constexpr float HELI_HOVER_DRAG         = 0.80f;
    constexpr float HELI_TURN_RATE          = 1.6f;
    constexpr float HELI_MOUSE_TURN_SENS    = 0.003f;
    constexpr float HELI_ASCEND_RATE        = 15.0f;
    constexpr float HELI_DESCEND_RATE       = 12.0f;
    constexpr float HELI_VISUAL_PITCH       = 0.20f;   // cosmetic tilt magnitude
} // namespace Config
```

---

## Part 9 — Damaged Ship Visuals

### 9.1 Smoke Trail

Enemy ships below 50% hull HP emit a smoke trail. Intensity scales with damage.

```cpp
namespace Config {
    constexpr float SMOKE_HP_THRESHOLD     = 0.50f;  // fraction — smoke starts
    constexpr float RETREAT_HP_THRESHOLD   = 0.40f;  // fraction — retreat starts
    constexpr float SMOKE_EMIT_RATE_MIN    = 3.0f;   // particles/sec at 50% HP
    constexpr float SMOKE_EMIT_RATE_MAX    = 15.0f;  // particles/sec at 0% HP
    constexpr float SMOKE_LIFETIME         = 1.2f;
} // namespace Config
```

Smoke particle colour: dark brownish `{120, 100, 80, 200}` fading to transparent.
Emitted from ship centre position (not engine nozzles).

Radar blip for damaged ships: flickers at double normal blink rate, slightly
dimmer, to communicate state without line of sight.

### 9.2 Retreat Behaviour

```cpp
// In EnemyAI::update():
float hpFrac = e.hullHP / e.maxHullHP;
if (hpFrac < Config::RETREAT_HP_THRESHOLD && e.state != AIState::Retreating
    && e.homeBase != nullptr)
{
    e.state      = AIState::Retreating;
    e.retreatTarget = e.homeBase->position;
}

// Retreat speed penalty
if (e.state == AIState::Retreating)
{
    e.currentSpeed = e.maxSpeed * (hpFrac / Config::RETREAT_HP_THRESHOLD) * 0.7f;
}
```

On arrival at base: hull restores at 15 HP/sec, shields at 20 HP/sec.
Base pays Metal for hull repair at 1 Metal per 10 HP restored.

---

## Part 10 — Score System

### 10.1 Live Ratio Display

The score HUD element shows the live base count ratio.

```
[ ▲ 18 ] vs [ ▼ 14 ]
```

Green number = friendly base count. Red number = enemy base count.
Arrow direction indicates which side is winning.

Updates immediately when any base changes state (destroyed or infected).

### 10.2 Session Result Screen

On game end (all enemy bases gone = win, all friendly bases gone = loss):

```
╔══════════════════════════════════════╗
║          SECTOR SECURED              ║  ← or SECTOR LOST
║                                      ║
║  Final base count:                   ║
║    Friendly: 12      Enemy: 0        ║
║                                      ║
║  Bases infected:     8               ║
║  Bases destroyed:    12              ║
║  Ships destroyed:    147             ║
║  Ships infected:     23              ║
║  Resources collected: 4,820          ║
║                                      ║
║  Time: 47:23                         ║
╚══════════════════════════════════════╝
```

No persistent leaderboard — session result only. Stats tracked in GameState
during play, displayed at end.

---

## Part 11 — Implementation Order

```
During Phase 3 (combat):
  [ ] Smoke trail emitter on enemy ships < 50% HP
  [ ] Enemy retreat AI state (HP < 40%)
  [ ] Shield Missile + Infectious Missile
  [ ] Shield Laser (4th primary, shared energy pool)
  [ ] Primary weapon cycling key
  [ ] Infection system — reboot period, faction flip, canBeInfected flag
  [ ] Round-start missile UI — four options
  [ ] Orbit drone — orbit physics, laser fire, travel from base
  [ ] Drone call system (key, cooldown, base dispatch)

During Phase 3.5 (terrain objects):
  [ ] Rock Formation resource node
  [ ] Tree Cluster resource node
  [ ] Crystal Deposit resource node
  [ ] Wreckage node (spawns on Carrier/Bomber/Base death)
  [ ] ResourceNode struct and ResourceSystem::placeNodes()
  [ ] Collector resource harvesting loop
  [ ] Resource pool on Entity base struct

Base Mode (new phase after wave mode stable):
  [ ] GameMode and Difficulty enums + selection UI
  [ ] Base struct with all fields above
  [ ] BaseMode::placeBases() — cluster + annular placement
  [ ] Base radar ring slot pre-calculation
  [ ] Builder unit — drive-build-return loop
  [ ] Base autonomous production (fighters, collectors, builders, drones)
  [ ] Inter-base resource sharing (automatic when damaged)
  [ ] Reinforcement call system (both factions)
  [ ] Base infection flow (shield-stripped → infect → reboot → recover)
  [ ] Base destruction + wreckage spawn
  [ ] Enemy ship heal-at-base (retreat + dock + heal)
  [ ] Score ratio HUD element (live base counts)
  [ ] Session result screen
  [ ] Player resource pool (base mode only)
  [ ] Player fly-through harvest
  [ ] Base supply replenishment (player near friendly base)

Helicopter Mode (Phase 2 completion):
  [ ] FlightModel enum + mode selection UI
  [ ] applyHelicopterPhysics() — auto-hover, W/S altitude, Q/E strafe
  [ ] handleHelicopterInput()
  [ ] Config::HELI_* constants
```

---

## Remaining Tuning Items (flag with // TUNE in code)

These need playtesting to get right — implement with the values below
as starting points:

| Constant | Starting Value | Notes |
|----------|---------------|-------|
| BASE_HULL_HP | 800 | May need to be higher to survive early raids |
| BASE_SHIELD_HP | 300 per sector | Scales with Carrier — test against mixed attacks |
| BASE_FIGHTER_PERIOD | 45s | May be too fast — watch base economy |
| BASE_REINFORCE_COUNT | 3 fighters | May need to scale with difficulty |
| BASE_REINFORCE_RANGE | 600 units | Trial different base distances |
| BASE_FRONT_BUFFER | 400 units | No-man's land width at each front |
| INFECT_REBOOT_DURATION | 3.0s | Confirmed range 2-4s — fine-tune within it |
| DRONE_CALL_COOLDOWN | 60s | Confirmed — or resource-gated if base low |
| PRIMARY_ENERGY_MAX | 120 | Confirmed — test strip-vs-suppress tension |
| PRIMARY_ENERGY_RECHARGE | 15 units/sec | Confirmed — 8s full recharge from empty |
| WRECKAGE_DECAY_TIME | 300s | 5 minutes — long enough to feel harvestable |
| BASE_INFECTION_RECOVER | 60s | Should feel like a real reward for infection |
