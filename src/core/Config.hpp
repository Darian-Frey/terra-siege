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
constexpr float PLAYER_MIN_ALTITUDE = 2.5f; // metres above terrain
constexpr float PLAYER_MAX_ALTITUDE = 120.0f;

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
constexpr float CAM_FOLLOW_LAG = 0.08f; // lerp coefficient per tick
constexpr float CAM_HEIGHT = 6.0f;
constexpr float CAM_DISTANCE = 14.0f;
constexpr float CAM_FOV = 70.0f; // degrees

// ----------------------------------------------------------------
// Terrain / planet
// ----------------------------------------------------------------
constexpr int HEIGHTMAP_SIZE = 512;   // must be 2^n + 1
constexpr int CHUNK_COUNT = 16;       // NxN chunks
constexpr int CHUNK_VERTS = 32;       // quads per chunk edge
constexpr float TERRAIN_SCALE = 4.0f; // world units per heightmap cell
constexpr float TERRAIN_HEIGHT_MAX = 55.0f;
constexpr float TERRAIN_CURVATURE = 0.00015f; // curvature cosine coefficient
constexpr float FOG_NEAR = 400.0f;
constexpr float FOG_FAR = 900.0f;

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
constexpr float SEA_LEVEL =
    0.20f; // fraction of HEIGHT_MAX — below this is flat ocean

} // namespace Config