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
constexpr float NEWTON_DRAG = 0.15f;           // linear damping per second
                                               // ~14% velocity loss/sec at hover;
                                               // ship coasts but eventually stops
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
// Terrain / planet — Fourier (sine) synthesis, toroidal world
// ----------------------------------------------------------------
constexpr int HEIGHTMAP_SIZE = 1025;  // must be 2^n + 1
constexpr int CHUNK_COUNT = 16;       // 1024/16 = 64 cells per chunk
constexpr int CHUNK_VERTS = 32;       // quads per chunk edge
constexpr float TERRAIN_SCALE = 8.0f; // world units per heightmap cell (world ~8192×8192)
constexpr float TERRAIN_HEIGHT_MAX = 220.0f; // peak elevation, world units
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
// Terrain generation — tileable Perlin (gradient) noise + fBM.
// Sine sums produced visible parallel ridge bands no matter how many
// terms or how strong the warp; gradient noise has no preferred
// direction. Tileability is preserved by wrapping the noise lattice
// modulo the tile period — every octave's lattice cell size divides
// (HEIGHTMAP_SIZE - 1) evenly so all octaves wrap exactly.
// ----------------------------------------------------------------
constexpr float SEA_LEVEL = 0.30f; // fraction of HEIGHT_MAX — ~30% ocean

constexpr int FBM_OCTAVES = 7;          // octaves accumulated for each cell
constexpr int FBM_LARGEST_CELL = 512;   // largest lattice spacing (heightmap cells)
                                        // 512, 256, 128, 64, 32, 16, 8 — all divide 1024
constexpr float FBM_PERSISTENCE = 0.50f;// amplitude multiplier per octave
                                        // Lower = bigger continents, fewer islands
constexpr float FBM_SHAPE_EXPONENT = 1.40f; // symmetric contrast around 0.5
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
constexpr int LAKE_MIN_SIZE = 20;        // drop floods smaller than this
constexpr float LAKE_FILL_EPSILON = 0.012f; // basin-fill height margin
constexpr float LAKE_MIN_DEPTH = 0.003f;    // depth below higher neighbours
constexpr int LAKE_MIN_HIGHER_NEIGHBOURS = 7; // of 8; lower = more lakes

} // namespace Config