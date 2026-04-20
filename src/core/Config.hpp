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
// Player craft physics
// ----------------------------------------------------------------
constexpr float PLAYER_THRUST = 28.0f;
constexpr float PLAYER_DRAG = 0.92f; // velocity multiplier per tick
constexpr float PLAYER_MAX_SPEED = 40.0f;
constexpr float PLAYER_BANK_RATE = 3.5f;    // roll rad/s driven by lateral vel
constexpr float PLAYER_MIN_ALTITUDE = 5.0f; // hard floor metres above terrain
constexpr float PLAYER_MAX_ALTITUDE = 500.0f;
constexpr float PLAYER_TURN_RATE = 1.8f;  // rad/s yaw
constexpr float PLAYER_ALT_RATE = 18.0f;  // units/s manual altitude
constexpr float PLAYER_PITCH_VIS = 0.18f; // visual pitch coefficient

// ----------------------------------------------------------------
// Arcade flight model (Air Combat 22 / Ace Combat style)
// Values from research report — see project-status/research.
// ----------------------------------------------------------------
constexpr float ARCADE_MIN_SPEED = 12.0f;
constexpr float ARCADE_CRUISE_SPEED = 30.0f;
constexpr float ARCADE_MAX_SPEED = 45.0f;
constexpr float ARCADE_BOOST_SPEED = 65.0f;
constexpr float ARCADE_THROTTLE_RATE = 30.0f; // target speed Δ per second (W/S)
constexpr float ARCADE_ACCEL_K = 1.2f;        // current → target lerp factor
constexpr float ARCADE_PITCH_RATE = 1.2f;     // rad/s max pitch response
constexpr float ARCADE_PITCH_MAX = 1.22f;     // ~70° clamp
constexpr float ARCADE_ROLL_RATE = 2.5f;      // rad/s max roll response
constexpr float ARCADE_BANK_MAX = 1.31f;      // ~75° clamp
constexpr float ARCADE_TURN_COEFF = 1.6f;     // yaw_rate = bank * coeff
constexpr float ARCADE_BANK_RESPONSE = 4.0f;  // P-gain for bank target tracking
constexpr float ARCADE_ENERGY_TRADE = 0.4f;   // climb/dive speed exchange
constexpr float ARCADE_GRAVITY = 9.81f;
constexpr float ARCADE_MOUSE_PITCH_SENS = 0.003f; // rad per pixel
constexpr float ARCADE_MOUSE_ROLL_SENS = 0.0035f; // rad per pixel

// Input smoothing — lowpass filters on mouse delta and control rates.
// Higher = faster (more responsive but less smooth). Lower = smoother
// but laggier. These give "glider" feel.
constexpr float ARCADE_MOUSE_SMOOTH = 20.0f;  // mouse delta lowpass per second
constexpr float ARCADE_RATE_SMOOTH = 8.0f;    // control rate lowpass per second

// Speed-aware terrain pullup. When AGL drops into the danger zone,
// the auto-pitch-up engages proportionally to how deep we've descended.
// Faster speed = bigger danger zone (more altitude needed to recover).
constexpr float ARCADE_PULLUP_BASE = 8.0f;          // base danger AGL (m)
constexpr float ARCADE_PULLUP_SPEED_FACTOR = 0.35f; // extra AGL per u/s of speed
constexpr float ARCADE_PULLUP_STRENGTH = 10.0f;     // max recovery rate at floor

// ----------------------------------------------------------------
// Classic flight model — Newtonian Virus-style
//   Thrust along ship's LOCAL UP axis only; constant gravity pulls
//   down; near-zero drag. Player flies by tilting the ship to
//   redirect the thrust vector, balancing against gravity.
// ----------------------------------------------------------------
constexpr float CLASSIC_GRAVITY = 12.0f;      // world-down acceleration (m/s²)
constexpr float CLASSIC_THRUST = 22.0f;       // acceleration from full thrust (m/s²)
constexpr float CLASSIC_THRUST_IDLE = 0.0f;   // thrust at rest (none — gravity wins)
constexpr float CLASSIC_DRAG = 0.03f;         // near-zero linear damping per tick
constexpr float CLASSIC_PITCH_RATE = 1.8f;    // manual pitch response (rad/s)
constexpr float CLASSIC_ROLL_RATE = 2.4f;     // manual roll response (rad/s)
constexpr float CLASSIC_YAW_RATE = 1.2f;      // manual yaw (Q/E or shoulder buttons)
constexpr float CLASSIC_PITCH_MAX = 1.55f;    // ~88° — allow near-vertical
constexpr float CLASSIC_ROLL_MAX = 3.14f;     // full ±180° (can flip upside-down)
constexpr float CLASSIC_MAX_SPEED = 80.0f;    // hard clamp to prevent runaway
constexpr float CLASSIC_GROUND_IMPACT = 14.0f; // above this vertical speed = crash
constexpr float CLASSIC_GROUND_BOUNCE = 0.3f;  // vertical velocity multiplier on light touch
constexpr float CLASSIC_MOUSE_PITCH_SENS = 0.004f;
constexpr float CLASSIC_MOUSE_ROLL_SENS = 0.005f;
constexpr float CLASSIC_INPUT_SMOOTH = 15.0f;  // input lowpass per second

// ----------------------------------------------------------------
// Classic camera — higher and further back than arcade, wider FOV
// ----------------------------------------------------------------
constexpr float CLASSIC_CAM_HEIGHT = 12.0f;
constexpr float CLASSIC_CAM_DISTANCE = 22.0f;
constexpr float CLASSIC_CAM_FOV = 85.0f;
constexpr float CLASSIC_CAM_LERP = 6.0f;

// Speed-dependent chase camera — pulls back at high speed to give a
// strong sensation of acceleration. Distance lerps between min and max.
constexpr float ARCADE_CAM_DISTANCE_MIN = 14.0f; // at cruise speed
constexpr float ARCADE_CAM_DISTANCE_MAX = 26.0f; // at full boost
constexpr float ARCADE_CAM_HEIGHT_MIN = 6.0f;
constexpr float ARCADE_CAM_HEIGHT_MAX = 8.0f;
constexpr float ARCADE_CAM_FOV_MIN = 70.0f;
constexpr float ARCADE_CAM_FOV_MAX = 82.0f; // FOV widens during boost

// ----------------------------------------------------------------
// Flight assist (0 = raw, 3 = full)
// ----------------------------------------------------------------
constexpr int FLIGHT_ASSIST_DEFAULT = 2; // Standard / Recruit
constexpr float ASSIST_LEVEL_COEFFS[4] = {0.0f, 0.18f, 0.42f, 0.75f};
constexpr float ASSIST_TERRAIN_LOOKAHEAD =
    0.35f; // seconds ahead for terrain raycast

// ----------------------------------------------------------------
// Camera
// ----------------------------------------------------------------
constexpr float CAM_FOLLOW_LAG = 0.08f;  // position lerp per tick
constexpr float CAM_FOLLOW_SPEED = 6.0f; // lerp speed factor
constexpr float CAM_HEIGHT = 6.0f;
constexpr float CAM_DISTANCE = 14.0f;
constexpr float CAM_FOV = 70.0f; // degrees

// ----------------------------------------------------------------
// Terrain / planet
// ----------------------------------------------------------------
constexpr int HEIGHTMAP_SIZE = 2049;  // must be 2^n + 1 (was 1025)
constexpr int CHUNK_COUNT = 32;       // 2048/32 = 64 cells per chunk (unchanged per-chunk density)
constexpr int CHUNK_VERTS = 32;       // quads per chunk edge
constexpr float TERRAIN_SCALE = 4.0f; // world units per heightmap cell (world ~8192×8192)
constexpr float TERRAIN_HEIGHT_MAX = 180.0f; // much taller mountains (was 55)
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
constexpr int MISSILE_AMMO_MAX = 20;

constexpr int CLUSTER_SUBMUNITIONS = 4;
constexpr float CLUSTER_SPREAD = 12.0f; // degrees
constexpr int CLUSTER_AMMO_MAX = 10;

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
// Radar
// ----------------------------------------------------------------
constexpr float RADAR_BASE_RANGE = 300.0f;
constexpr float RADAR_BOOST_RANGE = 500.0f; // when Radar Booster alive
constexpr float RADAR_BLINK_NEAR = 80.0f; // distance at which blip blinks fast

// ----------------------------------------------------------------
// AI
// ----------------------------------------------------------------
constexpr float AI_PURSUE_RANGE = 180.0f;
constexpr float AI_ATTACK_RANGE = 60.0f;
constexpr float AI_EVADE_HEALTH = 0.25f; // fraction
constexpr float SPATIAL_CELL_SIZE = 60.0f;

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
constexpr float EXHAUST_LIFETIME = 0.3f;

// ----------------------------------------------------------------
// Audio
// ----------------------------------------------------------------
constexpr int SOUND_CHANNELS = 4; // concurrent weapon fire channels
constexpr float AUDIO_MAX_DISTANCE = 300.0f;

// ----------------------------------------------------------------
// Terrain generation
// ----------------------------------------------------------------
constexpr float TERRAIN_ROUGHNESS = 0.55f; // lower = smoother, larger features
constexpr float SEA_LEVEL = 0.20f;         // fraction of HEIGHT_MAX

// Rivers
constexpr int RIVER_COUNT = 20; // number of rivers to carve (scaled for 4× area)
constexpr float RIVER_SOURCE_MIN_H =
    0.60f; // min normalised height for river source
constexpr float RIVER_CARVE_DEPTH = 0.04f; // how deep to cut the channel
constexpr int RIVER_WIDTH = 2;             // cells either side of centreline
constexpr int RIVER_MIN_LENGTH = 40;       // discard rivers shorter than this

// Lakes
constexpr int LAKE_COUNT = 40;      // max inland lakes (scaled for 4× area)
constexpr float LAKE_MIN_H = 0.22f; // must be above sea level
constexpr float LAKE_MAX_H = 0.55f; // not on mountain tops
constexpr int LAKE_MAX_CELLS = 800; // max flood-fill size   // fraction of
                                    // HEIGHT_MAX — below this is flat ocean

} // namespace Config