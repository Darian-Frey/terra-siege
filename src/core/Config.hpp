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

constexpr float NEWTON_GRAVITY = 9.8f;         // m/s² world-down
constexpr float NEWTON_THRUST = 24.0f;         // m/s² along local UP
constexpr float NEWTON_DRAG = 0.02f;           // near-zero linear damping per second
constexpr float NEWTON_PITCH_MAX = 1.30f;      // ~74° — steep but not inverted
constexpr float NEWTON_ROLL_MAX = 0.78f;       // ±45° banking limit
constexpr float NEWTON_MAX_SPEED = 70.0f;      // hard clamp to prevent runaway
constexpr float NEWTON_MOUSE_PITCH_SENS = 0.0025f; // rad per pixel
constexpr float NEWTON_MOUSE_YAW_SENS = 0.003f;    // rad per pixel
constexpr float NEWTON_MOUSE_ROLL_SENS = 0.002f;   // rad per pixel
constexpr float NEWTON_INPUT_SMOOTH = 12.0f;       // lowpass per second on mouse
constexpr float NEWTON_PITCH_RATE = 1.8f;          // keyboard pitch rad/s
constexpr float NEWTON_YAW_RATE = 1.4f;            // keyboard yaw rad/s
constexpr float NEWTON_ROLL_RATE = 2.0f;           // keyboard roll rad/s
constexpr float NEWTON_CRASH_SPEED = 12.0f;        // |vel.y| above this = crash
constexpr float NEWTON_LAND_SPEED = 3.0f;          // safe landing speed
constexpr float NEWTON_LAND_ATTITUDE = 0.14f;      // ~8° pitch+roll tolerance for soft landing
constexpr float NEWTON_THRUST_CHARGE_MAX = 100.0f;   // thrust charge units
constexpr float NEWTON_THRUST_DRAIN_RATE = 18.0f;    // units/sec while thrusting
constexpr float NEWTON_THRUST_RECHARGE_RATE = 14.0f; // units/sec while not thrusting
constexpr float NEWTON_FLIGHT_CEILING = 250.0f;      // AGL above which thrust cuts

// Flight-assist Level 3 terrain look-ahead
constexpr float ASSIST_PULLUP_LOOKAHEAD = 0.4f; // seconds ahead
constexpr float ASSIST_PULLUP_STRENGTH = 12.0f; // upward correction strength

// ----------------------------------------------------------------
// Flight assist (0 = raw, 3 = full) — corrective layer on top of Newtonian
// ----------------------------------------------------------------
constexpr int FLIGHT_ASSIST_DEFAULT = 2; // Standard / Recruit
constexpr float ASSIST_LEVEL_COEFFS[4] = {0.0f, 0.18f, 0.42f, 0.75f};

// ----------------------------------------------------------------
// Camera (single follow camera — five-view system in camera_system.md)
// ----------------------------------------------------------------
constexpr float CAM_HEIGHT = 8.0f;
constexpr float CAM_DISTANCE = 18.0f;
constexpr float CAM_FOV = 75.0f;     // degrees
constexpr float CAM_LERP = 8.0f;     // follow lag per second

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