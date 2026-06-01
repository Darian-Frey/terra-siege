#pragma once

// ====================================================================
// terra-siege — Central configuration
// All gameplay and engine constants live here. No magic numbers
// anywhere else in the codebase.
// ====================================================================

namespace Config {
// ----------------------------------------------------------------
// Display
// ----------------------------------------------------------------
constexpr int SCREEN_WIDTH = 1280;
constexpr int SCREEN_HEIGHT = 720;
constexpr int TARGET_FPS = 0; // 0 = uncapped (vsync driven)

// ----------------------------------------------------------------
// Timing
// ----------------------------------------------------------------
constexpr float FIXED_DT = 1.0f / 120.0f; // 120 Hz physics
constexpr float MAX_FRAME_TIME = 0.05f;   // spiral-of-death guard

// ----------------------------------------------------------------
// Newtonian flight model — single physics path, Virus/Zarch style.
// Thrust along the ship's LOCAL UP axis. Constant world gravity.
// Near-zero drag. Player tilts the ship to redirect the thrust
// vector — "tilt and burn".
// ----------------------------------------------------------------
constexpr float PLAYER_MIN_ALTITUDE = 5.0f;   // hard floor AGL
constexpr float PLAYER_MAX_ALTITUDE = 500.0f; // hard ceiling AGL

constexpr float NEWTON_GRAVITY = 9.8f;    // m/s² world-down
constexpr float NEWTON_THRUST = 24.0f;    // m/s² along local UP
constexpr float NEWTON_DRAG = 0.15f;      // linear damping per second
                                          // ~14% velocity loss/sec at hover;
                                          // ship coasts but eventually stops
constexpr float NEWTON_PITCH_MAX = 1.30f; // ~74° — steep but not inverted
constexpr float NEWTON_ROLL_MAX = 0.78f;  // ±45° banking limit
constexpr float NEWTON_MAX_SPEED = 70.0f; // hard clamp to prevent runaway
constexpr float NEWTON_MOUSE_PITCH_SENS = 0.0025f; // rad per pixel
constexpr float NEWTON_MOUSE_YAW_SENS = 0.003f;    // rad per pixel
constexpr float NEWTON_MOUSE_ROLL_SENS = 0.002f;   // rad per pixel
constexpr float NEWTON_INPUT_SMOOTH = 12.0f; // lowpass per second on mouse
constexpr float NEWTON_PITCH_RATE = 1.8f;    // keyboard pitch rad/s
constexpr float NEWTON_YAW_RATE = 1.4f;      // keyboard yaw rad/s
constexpr float NEWTON_ROLL_RATE = 2.0f;     // keyboard roll rad/s
constexpr float NEWTON_CRASH_SPEED = 12.0f;  // |vel.y| above this = crash
constexpr float NEWTON_LAND_SPEED = 3.0f;    // safe landing speed
constexpr float NEWTON_LAND_ATTITUDE =
    0.14f; // ~8° pitch+roll tolerance for soft landing
constexpr float NEWTON_THRUST_CHARGE_MAX = 100.0f; // thrust charge units
constexpr float NEWTON_THRUST_DRAIN_RATE = 18.0f;  // units/sec while thrusting
constexpr float NEWTON_THRUST_RECHARGE_RATE =
    14.0f;                                      // units/sec while not thrusting
constexpr float NEWTON_FLIGHT_CEILING = 250.0f; // AGL above which thrust cuts

// Flight-assist Level 3 terrain look-ahead
constexpr float ASSIST_PULLUP_LOOKAHEAD = 0.4f; // seconds ahead
constexpr float ASSIST_PULLUP_STRENGTH = 12.0f; // upward correction strength

// ----------------------------------------------------------------
// Flight assist (0 = raw, 3 = full) — corrective layer on top of Newtonian
// ----------------------------------------------------------------
constexpr int FLIGHT_ASSIST_DEFAULT = 2; // Standard / Recruit
constexpr float ASSIST_LEVEL_COEFFS[4] = {0.0f, 0.18f, 0.42f, 0.75f};

// ----------------------------------------------------------------
// Camera — five-view system (Chase / Velocity / Tactical / ThreatLock /
// Classic). Keys 1–5 select; 2-second fade-out label confirms switch.
// See terra_rebuild/camera_system.md for the design and tactical notes.
// ----------------------------------------------------------------

// Shared
constexpr float CAM_FOV = 75.0f; // degrees
constexpr float CAM_LERP = 8.0f; // follow position lerp per second

// Chase (View 1) and Velocity (View 2)
constexpr float CAM_HEIGHT = 8.0f;           // units above player
constexpr float CAM_DISTANCE = 18.0f;        // units behind player
constexpr float VELOCITY_CAM_MIN_SPD = 5.0f; // blend to chase below this speed

// Tactical overhead (View 3) — note: camera.up MUST be {0,0,1}, not {0,1,0}
constexpr float TACTICAL_CAM_ALTITUDE = 130.0f; // world units above player
constexpr float TACTICAL_CAM_FOV = 55.0f; // narrower FOV for less distortion

// Threat-lock (View 4)
constexpr float THREAT_CAM_MAX_ROT = 1.5708f;  // rad/s — 90°/sec rotation cap
constexpr float THREAT_CAM_HYSTERESIS = 0.20f; // 20% score gap to switch target

// Classic / original-Virus view (View 5) — fixed world-space offset,
// camera never rotates with the ship; world-north stays at top of screen.
constexpr float CLASSIC_CAM_OFFSET_X = -15.0f; // east-west offset
constexpr float CLASSIC_CAM_OFFSET_Z = -35.0f; // north of player (-Z = north)
constexpr float CLASSIC_CAM_ALTITUDE = 42.0f;  // above player
constexpr float CLASSIC_CAM_FOV = 65.0f;       // slightly narrower than chase
constexpr float CLASSIC_CAM_LERP = 5.0f;       // slower = more detached feel

// View-switch label fade (2 second display, last 0.5s fades to transparent)
constexpr float CAM_VIEW_LABEL_DURATION = 2.0f;
constexpr float CAM_VIEW_LABEL_FADE = 0.5f;

// ----------------------------------------------------------------
// Terrain / planet — Fourier (sine) synthesis, toroidal world
// ----------------------------------------------------------------
constexpr int HEIGHTMAP_SIZE = 1025; // must be 2^n + 1
constexpr int CHUNK_COUNT = 16;      // 1024/16 = 64 cells per chunk
constexpr int CHUNK_VERTS = 32;      // quads per chunk edge
constexpr float TERRAIN_SCALE =
    8.0f; // world units per heightmap cell (world ~8192×8192)
constexpr float TERRAIN_HEIGHT_MAX = 220.0f;  // peak elevation, world units
constexpr float TERRAIN_CURVATURE = 0.00015f; // curvature cosine coefficient
constexpr float FOG_NEAR = 700.0f;
constexpr float FOG_FAR = 2000.0f;

// ----------------------------------------------------------------
// Weapons — primary
// ----------------------------------------------------------------
constexpr float CANNON_FIRE_RATE = 0.08f; // sec between shots
constexpr float CANNON_SPEED = 120.0f;
constexpr float CANNON_DAMAGE = 8.0f;
constexpr float CANNON_RANGE = 200.0f;

constexpr float PLASMA_FIRE_RATE = 0.25f;
constexpr float PLASMA_SPEED = 80.0f;
constexpr float PLASMA_DAMAGE = 30.0f;
constexpr float PLASMA_SPLASH = 6.0f;
constexpr int PLASMA_AMMO_MAX = 120;

constexpr float BEAM_DAMAGE_PS = 45.0f; // damage per second
constexpr float BEAM_RANGE = 160.0f;
constexpr float BEAM_ENERGY_MAX = 100.0f;
constexpr float BEAM_DRAIN_PS = 20.0f;
constexpr float BEAM_RECHARGE_PS = 12.0f;

// ----------------------------------------------------------------
// Weapons — secondary
// ----------------------------------------------------------------
constexpr float MISSILE_FIRE_RATE = 0.6f;
constexpr float MISSILE_SPEED = 70.0f;
constexpr float MISSILE_DAMAGE = 60.0f;
constexpr float MISSILE_NAV_N = 4.0f;     // proportional nav constant
constexpr float MISSILE_TURN_RATE = 4.5f; // rad/s
constexpr float MISSILE_RANGE = 600.0f;   // hard distance cutoff
constexpr float MISSILE_LOCK_CONE = 0.6f; // ~34° half-angle, fire-time lock
constexpr int MISSILE_AMMO_MAX = 20;

constexpr int CLUSTER_SUBMUNITIONS = 4;
constexpr float CLUSTER_SPREAD =
    12.0f; // degrees (perpendicular spread at split)
constexpr int CLUSTER_AMMO_MAX = 10;
// Distance to target at which the carrier splits into submunitions.
// Wider split (45m) so the 4 children have time to spread across a
// loose swarm and reacquire individually rather than all converging
// on the same target the carrier was locked on.
constexpr float CLUSTER_SPLIT_DISTANCE = 45.0f;
// Sub-missile reacquisition: when a child missile's target dies, it
// scans every alive enemy within REACQUIRE_RANGE for a new lock. If
// nothing's in range, it goes ballistic — fly straight with a small
// downward acceleration so it eventually lands somewhere.
constexpr float MISSILE_REACQUIRE_RANGE = 300.0f;
constexpr float MISSILE_BALLISTIC_DIP = 6.0f; // m/s² downward when ballistic

constexpr float DEPTH_CHARGE_DAMAGE = 80.0f;
constexpr float DEPTH_CHARGE_RADIUS = 14.0f;
constexpr int DEPTH_CHARGE_MAX = 8;

// ----------------------------------------------------------------
// Weapons — special
// ----------------------------------------------------------------
constexpr float TURRET_RANGE = 60.0f;
constexpr float TURRET_FIRE_RATE = 0.4f;
constexpr float TURRET_DAMAGE = 6.0f;
constexpr float TURRET_ROT_SPEED = 3.0f;  // rad/s
constexpr float TURRET_FIRE_CONE = 0.08f; // radians

constexpr float EMP_RADIUS = 50.0f;
constexpr float EMP_STUN_DURATION = 4.0f;
constexpr float EMP_COOLDOWN = 12.0f;

// ----------------------------------------------------------------
// Shields
// ----------------------------------------------------------------
constexpr float SHIELD_HP_MAX = 100.0f;
constexpr float SHIELD_RECHARGE_RATE = 8.0f;  // HP/sec
constexpr float SHIELD_RECHARGE_DELAY = 3.0f; // sec after last hit

// ----------------------------------------------------------------
// Radar — Tier 1 baseline (radar_system.md). Tier 2+3 reserve unused
// constants so adding the missile warning ring and ghost-blip pool
// later doesn't require Config changes.
// ----------------------------------------------------------------
constexpr float RADAR_BASE_RANGE = 300.0f;       // world units (default)
constexpr float RADAR_BOOST_RANGE = 500.0f;      // with Radar Booster alive
constexpr float RADAR_ALT_STRIP_RANGE = 150.0f;  // ±units shown on alt strip
constexpr float RADAR_BLINK_NEAR = 80.0f;        // distance for fast blink
constexpr float RADAR_BLINK_FAR = 250.0f;        // distance for slow blink
constexpr float RADAR_BLINK_FAST = 0.12f;        // sec/cycle (near)
constexpr float RADAR_BLINK_SLOW = 0.60f;        // sec/cycle (far)
constexpr float RADAR_GHOST_LIFETIME = 8.0f;     // ghost blip persistence
constexpr float RADAR_VECTOR_MAX_LEN = 18.0f;    // px at max world speed
constexpr float RADAR_JAM_MAX_OFFSET = 12.0f;    // px jitter near Carrier
constexpr float RADAR_JAM_RANGE = 250.0f;        // Carrier dist that starts jamming
constexpr float RADAR_MISSILE_WARN_MIN = 120.0f; // dist to start warning ring
constexpr float RADAR_MISSILE_WARN_TTI = 3.5f;   // sec time-to-impact threshold
constexpr float RADAR_VECTOR_LOOKAHEAD = 1.5f;   // sec ahead for velocity arrow
constexpr float RADAR_DISC_RADIUS_PX = 60.0f;    // half of 120px disc
constexpr float RADAR_INNER_RING_FRAC = 0.35f;   // cannon range ring
constexpr float RADAR_OUTER_RING_FRAC = 0.75f;   // missile range ring
constexpr float RADAR_CLASSIC_VIEW_SCALE =
    1.20f; // disc scale-up factor in Classic view

// ----------------------------------------------------------------
// AI
// ----------------------------------------------------------------
constexpr float AI_PURSUE_RANGE = 180.0f;
constexpr float AI_ATTACK_RANGE = 60.0f;
constexpr float AI_EVADE_HEALTH = 0.25f; // fraction
constexpr float SPATIAL_CELL_SIZE = 60.0f;

// ----------------------------------------------------------------
// Combat — TIME-TO-KILL BUDGET (combat_tuning.md authoritative)
// HP values DERIVED from TTK targets. Adjust TTK first, never hull
// directly. Cannon DPS = CANNON_DAMAGE / CANNON_FIRE_RATE = 100.
// ----------------------------------------------------------------
constexpr float CANNON_DPS = CANNON_DAMAGE / CANNON_FIRE_RATE;

// TTK targets (seconds of sustained Cannon fire)
constexpr float TTK_DRONE = 0.08f;
constexpr float TTK_SEEDER = 0.50f;
constexpr float TTK_FIGHTER = 2.00f;
constexpr float TTK_BOMBER = 5.00f;
constexpr float TTK_CARRIER = 25.0f;
constexpr float TTK_TURRET = 4.00f;

// Shield fractions per enemy type
constexpr float SHIELD_FRAC_FIGHTER = 0.20f;
constexpr float SHIELD_FRAC_BOMBER = 0.30f;
constexpr float SHIELD_FRAC_CARRIER = 0.40f; // total split across 4 sectors

// Total HP (for derivation)
constexpr float TOTAL_DRONE = TTK_DRONE * CANNON_DPS;
constexpr float TOTAL_SEEDER = TTK_SEEDER * CANNON_DPS;
constexpr float TOTAL_FIGHTER = TTK_FIGHTER * CANNON_DPS;
constexpr float TOTAL_BOMBER = TTK_BOMBER * CANNON_DPS;
constexpr float TOTAL_CARRIER = TTK_CARRIER * CANNON_DPS;
constexpr float TOTAL_TURRET = TTK_TURRET * CANNON_DPS;

// Shield HP
constexpr float SHIELD_DRONE = 0.0f;
constexpr float SHIELD_SEEDER = 0.0f;
constexpr float SHIELD_FIGHTER = TOTAL_FIGHTER * SHIELD_FRAC_FIGHTER; // 40
constexpr float SHIELD_BOMBER = TOTAL_BOMBER * SHIELD_FRAC_BOMBER;    // 150
constexpr float SHIELD_CARRIER_PER_SECTOR =
    TOTAL_CARRIER * SHIELD_FRAC_CARRIER * 0.25f; // 250 per sector × 4
constexpr float SHIELD_TURRET = 0.0f;

// Hull HP (derived: total - shield)
constexpr float HULL_DRONE = TOTAL_DRONE - SHIELD_DRONE;       // 8
constexpr float HULL_SEEDER = TOTAL_SEEDER - SHIELD_SEEDER;    // 50
constexpr float HULL_FIGHTER = TOTAL_FIGHTER - SHIELD_FIGHTER; // 160
constexpr float HULL_BOMBER = TOTAL_BOMBER - SHIELD_BOMBER;    // 350
constexpr float HULL_CARRIER =
    TOTAL_CARRIER - (SHIELD_CARRIER_PER_SECTOR * 4.0f);     // 1500
constexpr float HULL_TURRET = TOTAL_TURRET - SHIELD_TURRET; // 400

// Shield recharge (delay before regen starts; rate while regenerating)
constexpr float SHIELD_DELAY_FIGHTER = 4.0f;
constexpr float SHIELD_DELAY_BOMBER = 5.0f;
constexpr float SHIELD_DELAY_CARRIER = 2.0f;
constexpr float SHIELD_RATE_FIGHTER = 20.0f;
constexpr float SHIELD_RATE_BOMBER = 25.0f;
constexpr float SHIELD_RATE_CARRIER = 80.0f;

// Player combat
constexpr float PLAYER_HULL_HP = 100.0f;
constexpr float PLAYER_SHIELD_HP_PER_SECTOR = 100.0f; // ×4 sectors = 400
constexpr float PLAYER_SHIELD_DELAY = 3.0f;
constexpr float PLAYER_SHIELD_RATE = 8.0f;
constexpr float PLAYER_SHIELD_FLASH = 0.7f;  // sec — bubble visible after hit
constexpr float PLAYER_SHIELD_RADIUS = 4.5f; // world units, sphere radius

// Fighter return-fire stats — ~25 DPS per spec player-survivability table
constexpr float FIGHTER_FIRE_RATE = 0.20f;  // sec between shots
constexpr float FIGHTER_FIRE_DAMAGE = 5.0f; // 5 / 0.20 = 25 DPS
constexpr float FIGHTER_PROJ_SPEED = 90.0f; // slower than player cannon
constexpr float FIGHTER_PROJ_RANGE = 80.0f;
constexpr float FIGHTER_THRUST = 18.0f;        // m/s² forward thrust
constexpr float FIGHTER_MAX_SPEED = 35.0f;     // slower than player
constexpr float FIGHTER_TURN_RATE = 1.2f;      // rad/s yaw rate
constexpr float FIGHTER_PREFERRED_ALT = 60.0f; // AGL hover target

// Drone — kamikaze swarm enemy. No weapons; damages by contact.
// Boids-style flocking (separation + alignment + cohesion) plus a
// pursuit force toward the player. 1-shot kill from Cannon (TTK 0.08s).
constexpr float DRONE_CONTACT_DAMAGE = 10.0f; // damage to player on impact
constexpr float DRONE_THRUST = 26.0f;         // m/s² (faster than fighter)
constexpr float DRONE_MAX_SPEED = 30.0f;
constexpr float DRONE_PREFERRED_ALT = 30.0f; // AGL hover target
constexpr float DRONE_HIT_RADIUS = 1.5f;
// Boids weights / radii
constexpr float DRONE_SEP_RADIUS = 5.0f;
constexpr float DRONE_ALIGN_RADIUS = 12.0f;
constexpr float DRONE_COHESION_RADIUS = 15.0f;
constexpr float DRONE_SEP_WEIGHT = 2.5f;
constexpr float DRONE_ALIGN_WEIGHT = 0.6f;
constexpr float DRONE_COHESION_WEIGHT = 0.4f;
constexpr float DRONE_PURSUE_WEIGHT = 1.4f;

// Seeder — slow flying carrier-lite. Drops drones at intervals while
// drifting at high altitude. Fragile on its own (TTK 0.5s), so the
// player can stop the drone bleed by killing the seeder. Force
// multiplier — one seeder left alive can spawn an entire swarm.
constexpr float SEEDER_THRUST = 8.0f;           // m/s² (slow)
constexpr float SEEDER_MAX_SPEED = 14.0f;       // top horizontal speed
constexpr float SEEDER_PREFERRED_ALT = 95.0f;   // hovers high — above fighter
constexpr float SEEDER_HIT_RADIUS = 3.0f;       // fat target — easy to hit
constexpr float SEEDER_DEPLOY_INTERVAL = 4.0f;  // sec between drone drops
constexpr float SEEDER_DEPLOY_RANGE = 240.0f;   // only deploys if player closer
constexpr float SEEDER_FIRST_DROP_DELAY = 2.0f; // grace period after spawn
constexpr float SEEDER_DRIFT_RADIUS = 120.0f;   // orbit radius around player
constexpr float SEEDER_RETREAT_RANGE = 90.0f;   // closer than this = peel away

// Carrier — boss-tier enemy. Hovers high overhead and continuously
// spawns drones. Four-sector directional shield is the headline
// mechanic: the player can't just dump cannon into the front, they
// have to flank or stay mobile to wear down a different sector
// while the front recharges. TTK 25s assumes the player kills it by
// methodically chewing through one sector at a time; if they
// circle-strafe they can break sectors faster.
//
// Doesn't fire weapons directly — the threat is the steady drone
// drip + opportunity cost (every second the Carrier is alive is
// another drone in your face). Same hover-and-drift pattern as
// Seeder but lower drift speed, higher altitude.
constexpr float CARRIER_THRUST = 6.0f;           // m/s² (very slow)
constexpr float CARRIER_MAX_SPEED = 10.0f;       // top horizontal speed
constexpr float CARRIER_PREFERRED_ALT = 130.0f;  // above everything else
constexpr float CARRIER_HIT_RADIUS = 6.0f;       // huge target
constexpr float CARRIER_DRIFT_RADIUS = 160.0f;   // orbit radius around player
constexpr float CARRIER_RETREAT_RANGE = 120.0f;  // peel away if closer
constexpr float CARRIER_DEPLOY_INTERVAL = 1.8f;  // sec between drone drops
constexpr float CARRIER_DEPLOY_RANGE = 320.0f;   // wide engagement
constexpr float CARRIER_FIRST_DROP_DELAY = 3.0f; // grace after spawn

// Bomber — heavy bruiser. Slower than Fighter on every axis (turn,
// thrust, top speed) but heavier hull + shield (TTK 5s) and a slow
// punishing fire pattern: ~31 DPS in chunky 25-damage shots that the
// player can dodge by moving but punish a stationary hover. Same
// PURSUE/ATTACK/EVADE state machine as Fighter, with the same
// damaged-engines visual when below 25% hull. STRAFE_FRIENDLY state
// is deferred to 5g when friendly units land.
constexpr float BOMBER_FIRE_RATE = 0.80f;   // sec between shots (slow)
constexpr float BOMBER_FIRE_DAMAGE = 25.0f; // 25 / 0.8 = 31.25 DPS
constexpr float BOMBER_PROJ_SPEED = 70.0f;  // slower tracer (dodgeable)
constexpr float BOMBER_PROJ_RANGE = 110.0f;
constexpr float BOMBER_THRUST = 14.0f;        // m/s² forward thrust
constexpr float BOMBER_MAX_SPEED = 28.0f;     // top horizontal speed
constexpr float BOMBER_TURN_RATE = 0.7f;      // rad/s yaw rate
constexpr float BOMBER_PREFERRED_ALT = 50.0f; // AGL hover target
constexpr float BOMBER_HIT_RADIUS = 4.0f;     // big target

// Ground Tank — formerly a stationary turret, now a tracked vehicle
// that drives toward the player while in good shape and reverses
// course at low HP. TANK_TURN_RATE governs chassis rotation; the
// existing TURRET_* fire constants are reused for the barrel since
// the visual + projectile model is identical.
constexpr float TANK_DRIVE_SPEED = 14.0f;       // m/s forward speed
constexpr float TANK_TURN_RATE = 1.1f;          // rad/s chassis rotation
constexpr float TANK_EVADE_RECOVERY = 0.50f;    // hull frac to leave EVADE
constexpr float TANK_FIRE_CONE = 0.20f;         // ~25° cone (wider, moving target)

// Ground Turret — stationary, terrain-anchored. Tracks the player
// with a rotating barrel and fires when in range and aligned. First
// low-altitude threat, makes hugging the ground risky. No shield,
// medium hull (TTK 4.0s), long engagement range so the player has to
// actively suppress them rather than ignoring.
constexpr float TURRET_AIM_RATE = 1.5f;         // rad/s rotation cap
constexpr float TURRET_ENGAGE_RANGE = 180.0f;   // start tracking inside this
constexpr float TURRET_FIRE_RANGE = 150.0f;     // open fire inside this
constexpr float TURRET_FIRE_CONE_ENEMY = 0.10f; // ~6° fire cone
constexpr float TURRET_ENEMY_FIRE_RATE = 0.45f; // sec between shots
constexpr float TURRET_ENEMY_DAMAGE = 8.0f;     // ~18 DPS
constexpr float TURRET_PROJ_SPEED = 100.0f;     // tracer
constexpr float TURRET_PROJ_RANGE = 220.0f;
constexpr float TURRET_HIT_RADIUS = 2.8f;
constexpr float TURRET_MOUNT_HEIGHT = 1.6f;       // base above terrain
constexpr float TURRET_BARREL_HEIGHT = 3.4f;      // barrel pivot above ground
constexpr float TURRET_GROUND_SPAWN_DIST = 90.0f; // place this far from player

// Friendly units — ground-anchored installations the player has to
// defend. Losing all of them = game over (alongside player death).
// Bombers prefer friendlies as targets when one is within their
// engagement range — that's the entity's main pressure axis.
constexpr float COLLECTOR_HULL = 60.0f;
constexpr float COLLECTOR_SPEED = 6.0f;          // ground vehicle
constexpr float COLLECTOR_WAYPOINT_RADIUS = 4.0f;
constexpr float COLLECTOR_HIT_RADIUS = 2.4f;
// Delivery loop — pickup sites are picked randomly inside this radius
// around the home Base each cycle. Dwell time at pickup AND unload.
constexpr float COLLECTOR_PICKUP_MIN_DIST = 80.0f;
constexpr float COLLECTOR_PICKUP_MAX_DIST = 200.0f;
constexpr float COLLECTOR_DWELL_TIME = 1.6f; // sec at each end of the loop
constexpr int COLLECTOR_DELIVERY_SCORE = 25; // points per successful delivery

// Base — Collector delivery destination. Stationary, durable, counts
// as friendly for game-over. One spawned per round. Carries a defensive
// auto-turret that engages the nearest enemy in range; turret damage is
// modest so the player still has to defend it.
constexpr float BASE_HULL = 240.0f;
constexpr float BASE_HIT_RADIUS = 4.5f;
constexpr float BASE_TURRET_RANGE = 110.0f;       // engagement radius
constexpr float BASE_TURRET_AIM_RATE = 2.2f;      // rad/s rotation
constexpr float BASE_TURRET_FIRE_RATE = 0.45f;    // sec between shots
constexpr float BASE_TURRET_DAMAGE = 7.0f;        // per shot (15 DPS)
constexpr float BASE_TURRET_FIRE_CONE = 0.12f;    // ~14° fire cone
constexpr float BASE_TURRET_BARREL_HEIGHT = 5.0f; // muzzle Y above base pos

constexpr float REPAIR_STATION_HULL = 90.0f;
constexpr float REPAIR_STATION_HEAL_RADIUS = 30.0f; // player needs to be close
constexpr float REPAIR_STATION_HEAL_RATE = 5.0f;    // hull HP / sec
constexpr float REPAIR_STATION_HIT_RADIUS = 3.0f;

constexpr float RADAR_BOOSTER_HULL = 80.0f;
constexpr float RADAR_BOOSTER_HIT_RADIUS = 2.8f;

// World spawn — total friendlies placed at round start, scattered
// inside the SPAWN_RING radius from the player's start position.
constexpr int FRIENDLY_COLLECTOR_COUNT = 2;
constexpr int FRIENDLY_REPAIR_COUNT = 1;
constexpr int FRIENDLY_BOOSTER_COUNT = 1;
constexpr float FRIENDLY_SPAWN_RING = 220.0f; // metres from player start
constexpr float FRIENDLY_SPAWN_MIN_DIST = 40.0f; // keep them separated

// Bomber engagement of friendlies — preference threshold. If the
// nearest friendly is closer than (dist to player × FRIENDLY_PRIORITY),
// the Bomber switches to STRAFE_FRIENDLY for that target. Set > 1
// so bombers chase friendlies even when the player is somewhat closer.
constexpr float BOMBER_FRIENDLY_PRIORITY = 2.0f;

// Pool sizes — pre-allocated, no heap in hot path
constexpr int ENTITY_POOL_SIZE = 256;     // enemies + friendlies
constexpr int PROJECTILE_POOL_SIZE = 512; // player + enemy projectiles

// Wave manager — staggered spawning + intermission between waves.
// Wave list is hardcoded for now; difficulty escalates after the
// table runs out (count grows linearly past the last defined wave).
constexpr float WAVE_INTERMISSION = 5.0f;      // sec between waves
constexpr float WAVE_FIRST_DELAY = 2.0f;       // sec before wave 1 starts
constexpr float WAVE_SPAWN_RING_MIN = 130.0f;  // min radius around player
constexpr float WAVE_SPAWN_RING_MAX = 220.0f;  // max radius
constexpr float WAVE_SPAWN_ALT_OFFSET = 25.0f; // spawn altitude above player

// Collision
constexpr float HIT_RADIUS_PROJECTILE = 0.4f; // projectile sphere radius
constexpr float HIT_RADIUS_FIGHTER = 2.5f;
constexpr float HIT_RADIUS_DRONE = 1.5f;
constexpr float HIT_RADIUS_SEEDER = 3.0f;
constexpr float HIT_RADIUS_TURRET = 2.8f;
constexpr float HIT_RADIUS_BOMBER = 4.0f;
constexpr float HIT_RADIUS_CARRIER = 6.0f;
constexpr float HIT_RADIUS_COLLECTOR = 2.4f;
constexpr float HIT_RADIUS_REPAIR = 3.0f;
constexpr float HIT_RADIUS_BOOSTER = 2.8f;
constexpr float HIT_RADIUS_BASE = 4.5f;
constexpr float HIT_RADIUS_PLAYER = 2.0f;

// ----------------------------------------------------------------
// Difficulty modifiers (indexed by DifficultyPreset enum)
// ----------------------------------------------------------------
//                                   Veteran  Pilot  Recruit  Commander
constexpr float AGGRESSION_MULT[4] = {1.3f, 1.0f, 0.8f, 0.6f};
constexpr float RECHARGE_MULT[4] = {0.6f, 1.0f, 1.4f, 2.0f};
constexpr float PICKUP_RATE[4] = {0.5f, 1.0f, 1.5f, 2.0f};
// Flight assist level per preset (maps to ASSIST_LEVEL_COEFFS above)
constexpr int PRESET_ASSIST[4] = {0, 1, 2, 3};

// ----------------------------------------------------------------
// Particles
// ----------------------------------------------------------------
constexpr int PARTICLE_POOL_SIZE = 2000;
constexpr float EXPLOSION_LIFETIME = 0.8f;
// Original Zarch/Virus exhaust (lander.bbcelite.com BounceParticle):
// all 3 velocity components halved on bounce, Y reflected. Lifespan
// counter = 8 ticks (≈0.3s at original frame rate). 8 particles per
// emit cycle. Small pixel-dot visuals — flat-shaded cubes here match
// the geometric, hard-edged look the user remembers.
constexpr float EXHAUST_LIFETIME = 0.45f;
constexpr float EXHAUST_EMIT_RATE = 130.0f;    // particles/sec while thrusting
constexpr float EXHAUST_INITIAL_SIZE = 0.30f;  // world units (cube side)
constexpr float EXHAUST_INITIAL_SPEED = 22.0f; // m/s downward (local-down)
constexpr float EXHAUST_SPREAD = 1.6f;         // m/s lateral random spread
constexpr float EXHAUST_GRAVITY = 9.8f;        // matches world NEWTON_GRAVITY
constexpr float EXHAUST_RESTITUTION = 0.5f; // Y kept on bounce (original 1/2)
constexpr float EXHAUST_BOUNCE_FRICTION =
    0.5f; // X/Z kept on bounce (original 1/2)

// ----------------------------------------------------------------
// Ground shadow — player ship's drop shadow on terrain. Both alpha
// and radius fall off rapidly with altitude (atmospheric scatter
// convention) so the shadow reads as a strong altitude cue at low
// AGL and disappears at modest height.
// ----------------------------------------------------------------
constexpr float SHADOW_RADIUS = 3.2f;        // world units, AGL=0
constexpr float SHADOW_FADE_MAX_AGL = 80.0f; // fully transparent above this
constexpr float SHADOW_FADE_EXPONENT = 1.6f; // >1 = more aggressive mid-range
constexpr float SHADOW_BASE_ALPHA = 180.0f;  // alpha at AGL=0 (max 255)
constexpr float SHADOW_SHRINK = 0.7f;        // 1 − 0.7 = 30% radius at fade max
constexpr float SHADOW_MARGIN = 0.5f;        // height above local terrain

// ----------------------------------------------------------------
// Audio
// ----------------------------------------------------------------
constexpr int SOUND_CHANNELS = 4; // concurrent weapon fire channels
constexpr float AUDIO_MAX_DISTANCE = 300.0f;

// ----------------------------------------------------------------
// Terrain generation — tileable Perlin (gradient) noise + fBM.
// Sine sums produced visible parallel ridge bands no matter how many
// terms or how strong the warp; gradient noise has no preferred
// direction. Tileability is preserved by wrapping the noise lattice
// modulo the tile period — every octave's lattice cell size divides
// (HEIGHTMAP_SIZE - 1) evenly so all octaves wrap exactly.
// ----------------------------------------------------------------
constexpr float SEA_LEVEL = 0.30f; // fraction of HEIGHT_MAX — ~30% ocean

constexpr int FBM_OCTAVES = 7; // octaves accumulated for each cell
constexpr int FBM_LARGEST_CELL =
    512; // largest lattice spacing (heightmap cells)
         // 512, 256, 128, 64, 32, 16, 8 — all divide 1024
constexpr float FBM_PERSISTENCE =
    0.50f; // amplitude multiplier per octave
           // Lower = bigger continents, fewer islands
constexpr float FBM_SHAPE_EXPONENT =
    1.40f; // symmetric contrast around 0.5
           // >1 = peaks rise, valleys deepen,
           // median stays put → SEA_LEVEL holds
           // its meaning regardless of shaping

// Mountain layer — ridged fBM masked to high-elevation regions only.
// `(1 - |perlin|)^2` gives sharp ridge lines (Musgrave's classic
// ridged-multifractal trick); the smoothstep mask between LOW/HIGH
// thresholds means flat lowlands stay flat and only the highlands get
// crinkled up into peaks. Standard libnoise-style layered terrain.
constexpr int FBM_MOUNTAIN_OCTAVES = 5;
constexpr int FBM_MOUNTAIN_LARGEST_CELL = 128;
constexpr float FBM_MOUNTAIN_AMP = 0.30f;
constexpr float FBM_MOUNTAIN_THRESHOLD_LOW = 0.60f;
constexpr float FBM_MOUNTAIN_THRESHOLD_HIGH = 0.90f;

// Domain warp — coupled 2D. warpX and warpZ each depend on both x and z
// so the perturbation bends features diagonally instead of stretching
// along the grid axes. Applied BEFORE Perlin sampling.
constexpr float SINE_WARP_AMPLITUDE = 32.0f; // cells of input perturbation

// Rivers
constexpr int RIVER_COUNT = 8; // number of rivers to carve
constexpr float RIVER_SOURCE_MIN_H = 0.65f;
constexpr float RIVER_CARVE_DEPTH = 0.035f;
constexpr int RIVER_WIDTH = 3;
constexpr int RIVER_MIN_LENGTH = 60;

// Lakes — flood-fill from confirmed local minima, with a post-flood
// size check so sub-threshold "speck" minima are dropped.
//
// Smooth Perlin terrain has lots of nearly-flat regions where a single
// cell can dip slightly below its neighbours; without filtering these
// produce 1-3 cell rectangular "lakes" littering the map. The size
// floor and stricter neighbour-depth gate eliminate them.
constexpr int LAKE_COUNT = 14;
constexpr float LAKE_MIN_H = 0.42f;
constexpr float LAKE_MAX_H = 0.65f;
constexpr int LAKE_MAX_CELLS = 600;
constexpr int LAKE_MIN_SIZE = 20;             // drop floods smaller than this
constexpr float LAKE_FILL_EPSILON = 0.012f;   // basin-fill height margin
constexpr float LAKE_MIN_DEPTH = 0.003f;      // depth below higher neighbours
constexpr int LAKE_MIN_HIGHER_NEIGHBOURS = 7; // of 8; lower = more lakes

} // namespace Config