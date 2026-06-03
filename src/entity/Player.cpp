#include "Player.hpp"
#include "core/Config.hpp"
#include "entity/Entity.hpp" // ShieldSector enum
#include "mesh/MeshRegistry.hpp"
#include "rlgl.h"
#include "world/Planet.hpp"
#include <cmath>
#include <cstring>
#include <vector>

// ====================================================================
// Lifetime
// ====================================================================
void Player::init(Vector3 startPos, int flightAssistLevel) {
  m_pos = startPos;
  m_vel = {0, 0, 0};
  m_yaw = 0.0f;
  m_pitch = 0.0f;
  m_roll = 0.0f;
  m_smoothMouse = {0.0f, 0.0f};
  m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;
  m_health = Config::PLAYER_HULL_HP;
  m_thrusting = false;
  m_landed = false;
  m_assistLevel = flightAssistLevel < 0   ? 0
                  : flightAssistLevel > 3 ? 3
                                          : flightAssistLevel;
  m_primaryWeapon = PrimaryWeapon::Cannon;
  m_secondaryWeapon = SecondaryWeapon::Missile;
  m_specialWeapon = SpecialWeapon::EMP;
  m_missileAmmo = Config::MISSILE_AMMO_MAX;
  m_clusterAmmo = Config::CLUSTER_AMMO_MAX;
  m_depthChargeAmmo = Config::DEPTH_CHARGE_MAX;
  m_primaryEnergy = Config::PRIMARY_ENERGY_MAX;
  m_beamFiring = false;
  m_empCooldown = 0.0f;
  m_shieldBoosterCooldown = 0.0f;
  m_autoTurretEnabled = false;
  m_turretYaw = 0.0f;
  m_turretTimer = 0.0f;

  // Directional shield — all four sectors full at spawn. Same
  // per-sector HP defined in Config (PLAYER_SHIELD_HP_PER_SECTOR ×
  // 4 = total shield budget across the four faces).
  for (int i = 0; i < 4; ++i) {
    m_sectorMax[i] = Config::PLAYER_SHIELD_HP_PER_SECTOR;
    m_sectorHP[i] = m_sectorMax[i];
    m_sectorTimer[i] = 0.0f;
  }

  buildMesh();
}

void Player::unload() {
  if (m_built) {
    UnloadModel(m_model);
    m_built = false;
  }
}

void Player::setFlightAssist(int level) {
  m_assistLevel = level < 0 ? 0 : level > 3 ? 3 : level;
}

void Player::applyDamage(float amount) {
  // God mode (DEV F3) — invincible. Skip damage entirely so HUD stays
  // pristine and downstream state machines (GameOver transition etc.)
  // never trigger during testing.
  if (m_godMode) return;
  m_health -= amount;
  if (m_health < 0.0f)
    m_health = 0.0f;
}

void Player::heal(float amount) {
  // Repair Stations call this each tick while the player is inside
  // their heal radius. Caps at the spawn hull HP — no overheal.
  if (m_health <= 0.0f) return; // wreck can't be healed
  m_health += amount;
  if (m_health > Config::PLAYER_HULL_HP)
    m_health = Config::PLAYER_HULL_HP;
}

// Directional damage — drains the hit sector first, overflow rolls to
// hull. Sector resolution mirrors EntityManager::damageSectorFromHit:
// world-space hit vector rotated by -yaw to get target-local space,
// then dominant axis + sign picks the quadrant. Per-sector timer is
// reset so this sector's recharge starts over while opposite faces
// keep regenerating undisturbed.
void Player::applyDamage(float amount, Vector3 hitPos) {
  if (m_godMode) return;

  // Pick sector from hit direction in ship-local space. Only yaw is
  // used — pitch/roll deliberately ignored so the player's mental
  // model ("front = where my nose points") matches the HUD pie
  // regardless of how banked the ship is.
  float c = cosf(m_yaw), s = sinf(m_yaw);
  Vector3 worldDir = {hitPos.x - m_pos.x, 0.0f, hitPos.z - m_pos.z};
  float lx = worldDir.x * c - worldDir.z * s;
  float lz = worldDir.x * s + worldDir.z * c;
  int sector;
  if (fabsf(lz) > fabsf(lx)) {
    sector = (lz > 0.0f) ? static_cast<int>(ShieldSector::Front)
                         : static_cast<int>(ShieldSector::Rear);
  } else {
    sector = (lx > 0.0f) ? static_cast<int>(ShieldSector::Right)
                         : static_cast<int>(ShieldSector::Left);
  }

  m_sectorTimer[sector] = 0.0f;
  if (m_sectorHP[sector] > 0.0f) {
    // Shield engaged — fire the bubble flash. Reset to full duration
    // each impact so a stream of incoming fire keeps the bubble lit.
    m_shieldFlashTimer = Config::PLAYER_SHIELD_FLASH;
    if (amount <= m_sectorHP[sector]) {
      m_sectorHP[sector] -= amount;
      return;
    }
    amount -= m_sectorHP[sector];
    m_sectorHP[sector] = 0.0f;
  }

  // Sector depleted — bleed into hull. No flash; the absence of the
  // bubble tells the player "this side is exposed".
  m_health -= amount;
  if (m_health < 0.0f)
    m_health = 0.0f;
}

float Player::sectorHP(int sector) const {
  if (sector < 0 || sector >= 4) return 0.0f;
  return m_sectorHP[sector];
}

float Player::sectorMax(int sector) const {
  if (sector < 0 || sector >= 4) return 0.0f;
  return m_sectorMax[sector];
}

float Player::sectorHPFrac(int sector) const {
  if (sector < 0 || sector >= 4) return 0.0f;
  if (m_sectorMax[sector] <= 0.0f) return 0.0f;
  float t = m_sectorHP[sector] / m_sectorMax[sector];
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

// ====================================================================
// Accessors — orientation derived from yaw/pitch/roll Euler angles.
// Matches the render rotation order: Rz(roll) → Rx(-pitch) → Ry(yaw).
//   forward = body +Z mapped to world
//   up      = body +Y mapped to world (= thrust direction)
//   right   = body +X mapped to world
// ====================================================================
static inline Vector3 rotateBodyAxis(Vector3 body, float yaw, float pitch,
                                     float roll) {
  // Apply Rz(roll) -> Rx(-pitch) -> Ry(yaw) to a body-frame axis.
  float cr = cosf(roll), sr = sinf(roll);
  float cp = cosf(pitch), sp = sinf(pitch);
  float cy = cosf(yaw), sy = sinf(yaw);

  // After Rz(roll): standard column-vector rotation
  float x1 = body.x * cr - body.y * sr;
  float y1 = body.x * sr + body.y * cr;
  float z1 = body.z;

  // After Rx(-pitch): y' = y*cos(-p) - z*sin(-p) = y*cp + z*sp
  //                   z' = y*sin(-p) + z*cos(-p) = -y*sp + z*cp
  float x2 = x1;
  float y2 = y1 * cp + z1 * sp;
  float z2 = -y1 * sp + z1 * cp;

  // After Ry(yaw): standard column-vector rotation
  float x3 = x2 * cy + z2 * sy;
  float y3 = y2;
  float z3 = -x2 * sy + z2 * cy;

  return {x3, y3, z3};
}

Vector3 Player::forward() const {
  return rotateBodyAxis({0.0f, 0.0f, 1.0f}, m_yaw, m_pitch, m_roll);
}
Vector3 Player::up() const {
  return rotateBodyAxis({0.0f, 1.0f, 0.0f}, m_yaw, m_pitch, m_roll);
}
Vector3 Player::right() const {
  return rotateBodyAxis({1.0f, 0.0f, 0.0f}, m_yaw, m_pitch, m_roll);
}

float Player::speed() const { return Vector3Length(m_vel); }

// ====================================================================
// Input — single handler. Mouse pitch/yaw, keyboard backup, optional roll.
// Conventions (Virus / helicopter style):
//   Mouse forward (dy < 0) → nose DOWN  (m_pitch decreases)
//   Mouse back    (dy > 0) → nose UP    (m_pitch increases)
//   Mouse right   (dx > 0) → yaw right
//   W key                  → thrust on
//   Q / E                  → optional roll left / right
// ====================================================================
void Player::handleInput(float dt) {
  // Dead pilot — no input. The ship continues to fall under gravity
  // (applyPhysics) but cannot thrust, fire, or steer. Once it hits
  // the ground GameState fires the final explosion + GameOver.
  if (m_health <= 0.0f) {
    m_thrusting = false;
    m_fireRequested = false;
    m_secondaryFireRequested = false;
    m_specialFireRequested = false;
    return;
  }

  // Focus-loss defence — if the window lost focus (alt-tab, WM grab,
  // etc.), KEYUP events may have been delivered to a different window
  // and raylib's IsKeyDown can be left "stuck" reporting true on a key
  // the user has actually released. Force-clear movement-relevant
  // inputs while unfocused so a stuck thrust can't run away with the
  // ship. Mouse delta is also unreliable when not focused.
  if (!IsWindowFocused()) {
    m_thrusting = false;
    m_fireRequested = false;
    m_secondaryFireRequested = false;
    m_specialFireRequested = false;
    m_smoothMouse = {0.0f, 0.0f};
    return;
  }
  // ---- Mouse delta with low-pass filter ----
  Vector2 raw = GetMouseDelta();
  float mouseSmooth = 1.0f - expf(-Config::NEWTON_INPUT_SMOOTH * dt);
  m_smoothMouse.x += (raw.x - m_smoothMouse.x) * mouseSmooth;
  m_smoothMouse.y += (raw.y - m_smoothMouse.y) * mouseSmooth;

  // Inversion multipliers — flipped via the future settings menu.
  const float pitchSign = m_invertPitch ? -1.0f : 1.0f;
  const float yawSign = m_invertYaw ? -1.0f : 1.0f;

  // ---- Pitch (mouse Y + keyboard W/S, S/up arrow nose-up) ----
  // Mouse forward (dy negative) tilts nose DOWN — helicopter convention.
  float pitchDelta = -m_smoothMouse.y * Config::NEWTON_MOUSE_PITCH_SENS;
  if (IsKeyDown(KEY_DOWN)) // pull stick back = nose up
    pitchDelta += Config::NEWTON_PITCH_RATE * dt;
  if (IsKeyDown(KEY_UP)) // push stick forward = nose down
    pitchDelta -= Config::NEWTON_PITCH_RATE * dt;
  pitchDelta *= pitchSign;
  m_pitch += pitchDelta;
  if (m_pitch > Config::NEWTON_PITCH_MAX)
    m_pitch = Config::NEWTON_PITCH_MAX;
  if (m_pitch < -Config::NEWTON_PITCH_MAX)
    m_pitch = -Config::NEWTON_PITCH_MAX;

  // ---- Yaw (mouse X + A/D) ----
  float yawDelta = m_smoothMouse.x * Config::NEWTON_MOUSE_YAW_SENS;
  if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))
    yawDelta -= Config::NEWTON_YAW_RATE * dt;
  if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))
    yawDelta += Config::NEWTON_YAW_RATE * dt;
  yawDelta *= yawSign;
  m_yaw += yawDelta;

  // ---- Roll (Q/E only — mouse roll is reserved for later) ----
  float rollDelta = 0.0f;
  if (IsKeyDown(KEY_Q))
    rollDelta -= Config::NEWTON_ROLL_RATE * dt;
  if (IsKeyDown(KEY_E))
    rollDelta += Config::NEWTON_ROLL_RATE * dt;
  m_roll += rollDelta;
  if (m_roll > Config::NEWTON_ROLL_MAX)
    m_roll = Config::NEWTON_ROLL_MAX;
  if (m_roll < -Config::NEWTON_ROLL_MAX)
    m_roll = -Config::NEWTON_ROLL_MAX;

  // ---- Thrust (W only — LMB now fires cannon) ----
  // Mouse-only philosophy keeps thrust off the mouse buttons so the
  // player can fire while thrusting without input conflicts.
  m_thrusting = IsKeyDown(KEY_W) && m_thrustCharge > 0.0f;

  // ---- Primary fire request — LMB or SPACE ----
  // For Cannon / Plasma this is "held = fire pulses". For Beam, it's
  // "held = beam on, energy draining". update() decides which model
  // to use based on m_primaryWeapon.
  m_fireRequested =
      IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE);

  // ---- Primary cycle — Tab (Cannon → Plasma → Beam → ShieldLaser → Cannon) ----
  bool tabDown = IsKeyDown(KEY_TAB);
  if (tabDown && !m_tabWasDown) {
    int next = (static_cast<int>(m_primaryWeapon) + 1) % 4;
    m_primaryWeapon = static_cast<PrimaryWeapon>(next);
    // Switching off a beam mid-fire kills the visual + damage
    // immediately on the next tick; flag flips false in update.
    m_beamFiring = false;
  }
  m_tabWasDown = tabDown;

  // ---- Secondary fire — RMB (held = continuous attempts for missile,
  // single-pulse on first frame for the others — fire rate gates rate).
  m_secondaryFireRequested = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

  // ---- Secondary cycle — Z (Missile → Cluster → DepthCharge → ...).
  bool zDown = IsKeyDown(KEY_Z);
  if (zDown && !m_zWasDown) {
    int next = (static_cast<int>(m_secondaryWeapon) + 1) % 3;
    m_secondaryWeapon = static_cast<SecondaryWeapon>(next);
  }
  m_zWasDown = zDown;

  // ---- Special cycle — X (EMP → ShieldBooster → ...).
  bool xDown = IsKeyDown(KEY_X);
  if (xDown && !m_xWasDown) {
    int next = (static_cast<int>(m_specialWeapon) + 1) % 2;
    m_specialWeapon = static_cast<SpecialWeapon>(next);
  }
  m_xWasDown = xDown;

  // ---- Special fire — F (edge-triggered; cooldown gates rate) ----
  bool fDown = IsKeyDown(KEY_F);
  m_specialFireRequested = (fDown && !m_fWasDown);
  m_fWasDown = fDown;

  // ---- Auto Turret toggle — T (edge-triggered) ----
  bool tDown = IsKeyDown(KEY_T);
  if (tDown && !m_tWasDown) {
    m_autoTurretEnabled = !m_autoTurretEnabled;
  }
  m_tWasDown = tDown;
}

// ====================================================================
// Flight assist — corrective layer. Does NOT replace physics; layered on top.
//   Level 0: Raw           — no correction
//   Level 1: Minimal       — auto-level roll
//   Level 2: Standard      — auto-level roll + auto-reduce pitch
//   Level 3: Full          — Standard + terrain-avoidance look-ahead
// ====================================================================
void Player::applyFlightAssist(float dt, const Planet &planet) {
  // Dead pilot — avionics offline, no auto-stabilisation.
  if (m_health <= 0.0f) return;
  if (m_assistLevel <= 0)
    return;

  float coeff = Config::ASSIST_LEVEL_COEFFS[m_assistLevel];

  // Level 1+: auto-level roll back to zero when no input
  bool rollInput = IsKeyDown(KEY_Q) || IsKeyDown(KEY_E);
  if (!rollInput) {
    float k = coeff * 4.0f * dt;
    if (k > 1.0f) k = 1.0f;
    m_roll -= m_roll * k;
  }

  // Level 2+: auto-reduce pitch toward level when no pitch input
  bool pitchInput = IsKeyDown(KEY_UP) || IsKeyDown(KEY_DOWN) ||
                    fabsf(m_smoothMouse.y) > 0.5f;
  if (m_assistLevel >= 2 && !pitchInput) {
    float k = coeff * 1.2f * dt;
    if (k > 1.0f) k = 1.0f;
    m_pitch -= m_pitch * k;
  }

  // Level 3: terrain look-ahead — nudge upward if heading into ground
  if (m_assistLevel >= 3) {
    Vector3 ahead = Vector3Add(
        m_pos, Vector3Scale(m_vel, Config::ASSIST_PULLUP_LOOKAHEAD));
    float aheadGround = planet.heightAt(ahead.x, ahead.z);
    float dangerAGL = aheadGround + Config::PLAYER_MIN_ALTITUDE * 4.0f;
    if (m_pos.y < dangerAGL) {
      float depth = (dangerAGL - m_pos.y) /
                    (Config::PLAYER_MIN_ALTITUDE * 4.0f);
      if (depth > 1.0f) depth = 1.0f;
      m_vel.y += depth * Config::ASSIST_PULLUP_STRENGTH * coeff * dt;
    }
  }
}

// ====================================================================
// Physics — Newtonian. Thrust along local UP, gravity always world-down.
// ====================================================================
void Player::applyPhysics(float dt, const Planet &planet) {
  // ---- Flight ceiling: thrust cuts above NEWTON_FLIGHT_CEILING AGL ----
  float groundH = planet.heightAt(m_pos.x, m_pos.z);
  float agl = m_pos.y - groundH;
  bool ceilingCut = (agl > Config::NEWTON_FLIGHT_CEILING);

  // ---- Thrust along local UP ----
  // Charge drains while thrusting and rebuilds whenever it isn't. With
  // m_godMode (DEV F3) the meter is pinned at MAX and never drains.
  // applyingThrust reflects whether thrust force is ACTUALLY being added
  // this tick (input held AND below ceiling AND have charge). Setting
  // m_thrusting to this at the end keeps isThrusting() / HUD / exhaust
  // particles in sync with physical reality — without it, holding the
  // thrust key above the flight ceiling kept m_thrusting==true even
  // though no force was applied, leaving the exhaust still firing.
  bool applyingThrust =
      m_thrusting && !ceilingCut && m_thrustCharge > 0.0f;

  if (applyingThrust) {
    Vector3 thrustDir = up();
    m_vel = Vector3Add(m_vel, Vector3Scale(thrustDir,
                                           Config::NEWTON_THRUST * dt));
    if (!m_godMode) {
      m_thrustCharge -= Config::NEWTON_THRUST_DRAIN_RATE * dt;
      if (m_thrustCharge <= 0.0f) {
        m_thrustCharge = 0.0f;
        applyingThrust = false;
      }
    }
  } else if (!m_godMode) {
    m_thrustCharge += Config::NEWTON_THRUST_RECHARGE_RATE * dt;
    if (m_thrustCharge > Config::NEWTON_THRUST_CHARGE_MAX)
      m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;
  }
  if (m_godMode)
    m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;

  m_thrusting = applyingThrust;

  // ---- Gravity (always world -Y) ----
  m_vel.y -= Config::NEWTON_GRAVITY * dt;

  // ---- Drag (near-zero linear damping) ----
  float dragFactor = 1.0f - Config::NEWTON_DRAG * dt;
  if (dragFactor < 0.0f) dragFactor = 0.0f;
  m_vel = Vector3Scale(m_vel, dragFactor);

  // ---- Hard speed clamp ----
  float spd = Vector3Length(m_vel);
  if (spd > Config::NEWTON_MAX_SPEED)
    m_vel = Vector3Scale(m_vel, Config::NEWTON_MAX_SPEED / spd);

  // ---- Position integration ----
  m_pos = Vector3Add(m_pos, Vector3Scale(m_vel, dt));

  // ---- Ground interaction ----
  groundH = planet.heightAt(m_pos.x, m_pos.z);
  float minH = groundH + Config::PLAYER_MIN_ALTITUDE;
  m_landed = false;
  if (m_pos.y < minH) {
    float impact = -m_vel.y; // positive when descending
    bool levelEnough = (fabsf(m_pitch) < Config::NEWTON_LAND_ATTITUDE) &&
                       (fabsf(m_roll) < Config::NEWTON_LAND_ATTITUDE);

    if (impact > Config::NEWTON_CRASH_SPEED || !levelEnough) {
      // CRASH — total destruction
      applyDamage(m_health);
      m_vel = {0, 0, 0};
    } else if (impact > Config::NEWTON_LAND_SPEED) {
      // HARD landing — damage proportional to excess speed
      float excess = impact - Config::NEWTON_LAND_SPEED;
      applyDamage(excess * 6.0f);
      m_vel.y = 0.0f;
    } else {
      // SUCCESSFUL landing
      m_landed = true;
      m_vel = {0, 0, 0};
      // TODO Phase 3: refuel if at launch pad
    }
    m_pos.y = minH;
  }

  // ---- Hard ceiling (in addition to thrust cutout — prevents creep) ----
  float maxH = groundH + Config::PLAYER_MAX_ALTITUDE;
  if (m_pos.y > maxH) {
    m_pos.y = maxH;
    if (m_vel.y > 0.0f) m_vel.y = 0.0f;
  }

  // No position wrap — terrain renders tiled around the camera, so the
  // world appears infinite in every direction. Letting coordinates grow
  // unbounded is preferable to teleporting: it keeps the camera-follow
  // lerp continuous and avoids any visible edge. Heightmap queries wrap
  // internally so terrain lookups stay correct at any coordinate. Single
  // precision floats hold sub-metre accuracy out to ~8M units, far
  // beyond practical play distances.
  (void)planet;
}

void Player::wrapPosition(float /*worldSize*/) {
  // Retained for API compatibility; world wrapping happens implicitly
  // via Planet::draw's per-chunk offset and Heightmap's wrapping query.
}

// ====================================================================
// update — top-level pipeline
// ====================================================================
void Player::update(float dt, const Planet &planet) {
  handleInput(dt);
  applyFlightAssist(dt, planet);
  applyPhysics(dt, planet);

  // Per-sector shield recharge — each face has its own time-since-hit
  // counter so sustained pressure on one face doesn't suppress
  // regen on the opposite side. Same delay + rate constants for all
  // sectors (PLAYER_SHIELD_DELAY/RATE in Config).
  for (int i = 0; i < 4; ++i) {
    if (m_sectorMax[i] <= 0.0f) continue;
    m_sectorTimer[i] += dt;
    if (m_sectorTimer[i] >= Config::PLAYER_SHIELD_DELAY &&
        m_sectorHP[i] < m_sectorMax[i]) {
      m_sectorHP[i] += Config::PLAYER_SHIELD_RATE * dt;
      if (m_sectorHP[i] > m_sectorMax[i])
        m_sectorHP[i] = m_sectorMax[i];
    }
  }
  // Bubble flash decay — visual is invisible once this hits zero.
  if (m_shieldFlashTimer > 0.0f) {
    m_shieldFlashTimer -= dt;
    if (m_shieldFlashTimer < 0.0f) m_shieldFlashTimer = 0.0f;
  }

  // Primary — branch by weapon. Cannon + Plasma share the
  // m_cannonTimer (single discrete-fire cooldown); Beam uses its
  // own energy meter and is continuous-fire while held.
  if (m_cannonTimer > 0.0f) m_cannonTimer -= dt;
  if (m_cannonTimer < 0.0f) m_cannonTimer = 0.0f;

  // Shared primary energy pool (Slice B.1/B.2). Cannon is free;
  // Plasma deducts a discrete cost per shot; Beam and Shield Laser
  // drain continuously while held at their own rates. Recharge runs
  // every tick neither continuous-beam weapon is firing.
  bool isBeamLike = (m_primaryWeapon == PrimaryWeapon::Beam ||
                     m_primaryWeapon == PrimaryWeapon::ShieldLaser);
  bool wantBeam = isBeamLike && m_fireRequested && m_health > 0.0f &&
                  m_primaryEnergy > 0.0f;
  if (wantBeam) {
    Vector3 fwd = forward();
    m_beamOrigin = Vector3Add(m_pos, Vector3Scale(fwd, 2.4f));
    m_beamDir = fwd;
    m_beamFiring = true;
    float drain = (m_primaryWeapon == PrimaryWeapon::ShieldLaser)
                      ? Config::SHIELD_LASER_ENERGY_PS
                      : Config::BEAM_ENERGY_DRAIN_PS;
    m_primaryEnergy -= drain * dt;
    if (m_primaryEnergy < 0.0f) m_primaryEnergy = 0.0f;
  } else {
    m_beamFiring = false;
    m_primaryEnergy += Config::PRIMARY_ENERGY_RECHARGE * dt;
    if (m_primaryEnergy > Config::PRIMARY_ENERGY_MAX)
      m_primaryEnergy = Config::PRIMARY_ENERGY_MAX;
  }

  // Cannon / Plasma — discrete projectile firing on the shared timer.
  // Plasma additionally requires PLASMA_ENERGY_PER_SHOT in the pool;
  // if the player drained it on a beam they need to wait for recharge
  // before Plasma will fire again. Beam-class weapons (Beam,
  // ShieldLaser) handle their own firing above.
  bool wantPrimary = m_fireRequested && m_cannonTimer <= 0.0f &&
                     m_health > 0.0f && !isBeamLike;
  if (wantPrimary && m_primaryWeapon == PrimaryWeapon::Plasma &&
      m_primaryEnergy < Config::PLASMA_ENERGY_PER_SHOT) {
    wantPrimary = false;
  }
  if (wantPrimary) {
    Vector3 fwd = forward();
    m_shotPos = Vector3Add(m_pos, Vector3Scale(fwd, 3.0f));
    float spd = (m_primaryWeapon == PrimaryWeapon::Plasma)
                    ? Config::PLASMA_SPEED
                    : Config::CANNON_SPEED;
    m_shotVel = Vector3Add(Vector3Scale(fwd, spd), m_vel);
    m_pendingShot = true;
    m_cannonTimer = (m_primaryWeapon == PrimaryWeapon::Plasma)
                        ? Config::PLASMA_FIRE_RATE
                        : Config::CANNON_FIRE_RATE;
    if (m_primaryWeapon == PrimaryWeapon::Plasma) {
      m_primaryEnergy -= Config::PLASMA_ENERGY_PER_SHOT;
      if (m_primaryEnergy < 0.0f) m_primaryEnergy = 0.0f;
    }
  }

  // Secondary — branch by selected weapon. All three share the
  // m_secondaryTimer so cycling Z mid-cooldown doesn't bypass it.
  if (m_secondaryTimer > 0.0f) m_secondaryTimer -= dt;
  if (m_secondaryTimer < 0.0f) m_secondaryTimer = 0.0f;
  if (m_secondaryFireRequested && m_secondaryTimer <= 0.0f &&
      m_health > 0.0f) {
    Vector3 fwd = forward();
    switch (m_secondaryWeapon) {
    case SecondaryWeapon::Missile:
      if (m_missileAmmo > 0) {
        m_missilePos = Vector3Add(m_pos, Vector3Scale(fwd, 3.0f));
        m_missileVel =
            Vector3Add(Vector3Scale(fwd, Config::MISSILE_SPEED), m_vel);
        m_pendingMissile = true;
        m_secondaryTimer = Config::MISSILE_FIRE_RATE;
        --m_missileAmmo;
      }
      break;
    case SecondaryWeapon::Cluster:
      if (m_clusterAmmo > 0) {
        // Fire a SINGLE carrier missile forward. It splits into 4
        // sub-missiles when it nears its lock target — that logic
        // lives in updateProjectile (ClusterParent kind). Visually
        // identical to a missile until split.
        m_clusterPos = Vector3Add(m_pos, Vector3Scale(fwd, 3.0f));
        m_clusterVel =
            Vector3Add(Vector3Scale(fwd, Config::MISSILE_SPEED), m_vel);
        m_pendingCluster = true;
        m_secondaryTimer = Config::MISSILE_FIRE_RATE * 1.5f;
        --m_clusterAmmo;
      }
      break;
    case SecondaryWeapon::DepthCharge:
      if (m_depthChargeAmmo > 0) {
        // Drop from underside, inherit ship velocity, no propulsion.
        // updateProjectile applies gravity to DepthCharge kind.
        m_depthChargePos = {m_pos.x, m_pos.y - 1.5f, m_pos.z};
        // Initial velocity = ship vel + a small forward kick so it
        // doesn't fall right onto the launching ship.
        m_depthChargeVel = Vector3Add(m_vel, Vector3Scale(fwd, 8.0f));
        m_pendingDepthCharge = true;
        m_secondaryTimer = 0.5f;
        --m_depthChargeAmmo;
      }
      break;
    }
  }

  // Special — branch by selected weapon. Each special has its own
  // cooldown so cycling X mid-fight doesn't bypass them.
  if (m_empCooldown > 0.0f) m_empCooldown -= dt;
  if (m_empCooldown < 0.0f) m_empCooldown = 0.0f;
  if (m_shieldBoosterCooldown > 0.0f) m_shieldBoosterCooldown -= dt;
  if (m_shieldBoosterCooldown < 0.0f) m_shieldBoosterCooldown = 0.0f;
  if (m_specialFireRequested && m_health > 0.0f) {
    switch (m_specialWeapon) {
    case SpecialWeapon::EMP:
      if (m_empCooldown <= 0.0f) {
        m_empPos = m_pos;
        m_pendingEMP = true;
        m_empCooldown = Config::EMP_COOLDOWN;
      }
      break;
    case SpecialWeapon::ShieldBooster:
      if (m_shieldBoosterCooldown <= 0.0f) {
        // Refill all 4 sectors to max — applied immediately here
        // (Player owns the sector state). Signal flag lets
        // GameState fire a visual cue but the gameplay effect is
        // already done.
        for (int i = 0; i < 4; ++i) {
          m_sectorHP[i] = m_sectorMax[i];
          m_sectorTimer[i] = 0.0f;
        }
        m_shieldFlashTimer = Config::PLAYER_SHIELD_FLASH;
        m_pendingShieldBoost = true;
        m_shieldBoosterCooldown = 20.0f;
      }
      break;
    }
  }

  // Auto Turret — independent passive subsystem. When enabled,
  // scans for the nearest target each tick (lock + fire happens in
  // GameState because it has EntityManager). Here we just tick the
  // shot cooldown so the turret pulses on its own schedule.
  if (m_turretTimer > 0.0f) m_turretTimer -= dt;
  if (m_turretTimer < 0.0f) m_turretTimer = 0.0f;

  // God mode (DEV F3) — refill all ammo + zero all cooldowns + max
  // beam energy each tick. Mirrors the thrust-charge pin above. F3
  // is now genuinely "all weapons unlimited" across the full slot set.
  if (m_godMode) {
    m_missileAmmo = Config::MISSILE_AMMO_MAX;
    m_clusterAmmo = Config::CLUSTER_AMMO_MAX;
    m_depthChargeAmmo = Config::DEPTH_CHARGE_MAX;
    m_primaryEnergy = Config::PRIMARY_ENERGY_MAX;
    m_empCooldown = 0.0f;
    m_shieldBoosterCooldown = 0.0f;
  }
}

// Consume any pending shot — GameState calls this after update() and
// hands the resulting position+velocity to EntityManager.
bool Player::consumePendingShot(Vector3 &outPos, Vector3 &outVel) {
  if (!m_pendingShot) return false;
  outPos = m_shotPos;
  outVel = m_shotVel;
  m_pendingShot = false;
  return true;
}

bool Player::consumePendingMissile(Vector3 &outPos, Vector3 &outVel) {
  if (!m_pendingMissile) return false;
  outPos = m_missilePos;
  outVel = m_missileVel;
  m_pendingMissile = false;
  return true;
}

bool Player::consumePendingDepthCharge(Vector3 &outPos, Vector3 &outVel) {
  if (!m_pendingDepthCharge) return false;
  outPos = m_depthChargePos;
  outVel = m_depthChargeVel;
  m_pendingDepthCharge = false;
  return true;
}

bool Player::consumePendingCluster(Vector3 &outPos, Vector3 &outVel) {
  if (!m_pendingCluster) return false;
  outPos = m_clusterPos;
  outVel = m_clusterVel;
  m_pendingCluster = false;
  return true;
}

bool Player::consumePendingEMP(Vector3 &outPos) {
  if (!m_pendingEMP) return false;
  outPos = m_empPos;
  m_pendingEMP = false;
  return true;
}

bool Player::consumePendingShieldBoost() {
  if (!m_pendingShieldBoost) return false;
  m_pendingShieldBoost = false;
  return true;
}

bool Player::beamIsFiringThisTick(Vector3 &outOrigin, Vector3 &outDir) {
  if (!m_beamFiring) return false;
  outOrigin = m_beamOrigin;
  outDir = m_beamDir;
  return true;
}

bool Player::consumePendingTurretShot(Vector3 &outPos, Vector3 &outVel) {
  if (!m_pendingTurretShot) return false;
  outPos = m_turretShotPos;
  outVel = m_turretShotVel;
  m_pendingTurretShot = false;
  return true;
}

float Player::empMaxCooldown() const { return Config::EMP_COOLDOWN; }
float Player::primaryEnergyMax() const { return Config::PRIMARY_ENERGY_MAX; }
float Player::shieldBoosterMaxCooldown() const { return 20.0f; }

// ====================================================================
// Mesh — single hovercraft, derived from the previous Saucer geometry
// trimmed toward an "elongated diamond" shape per the spec.
// ====================================================================

namespace {

struct Tri {
  Vector3 a, b, c;
  Color col;
};

// Compute face normal from triangle vertices and apply directional lighting.
Color litColour(Color base, Vector3 a, Vector3 b, Vector3 c) {
  Vector3 e1 = {b.x - a.x, b.y - a.y, b.z - a.z};
  Vector3 e2 = {c.x - a.x, c.y - a.y, c.z - a.z};
  Vector3 n = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
               e1.x * e2.y - e1.y * e2.x};
  float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  if (len > 0.0f) {
    n.x /= len;
    n.y /= len;
    n.z /= len;
  }
  // Sun matches terrain lighting
  const float sx = 0.57f, sy = 0.74f, sz = 0.36f;
  float diff = sx * n.x + sy * n.y + sz * n.z;
  if (diff < 0.0f) diff = 0.0f;
  float light = 0.38f + 0.62f * diff;
  auto cu = [](float v) -> unsigned char {
    int i = static_cast<int>(v);
    return static_cast<unsigned char>(i < 0 ? 0 : i > 255 ? 255 : i);
  };
  return {cu(base.r * light), cu(base.g * light), cu(base.b * light), 255};
}

} // namespace

void Player::buildMesh() {
  // ---- Palette — the original Virus lander was simple flat colours ----
  const Color hullTop = {165, 170, 178, 255};
  const Color hullBot = {70, 75, 82, 255};
  const Color rim = {95, 100, 108, 255};
  const Color domeGlass = {60, 130, 180, 255};
  const Color thrusterGlow = {255, 160, 40, 255};

  // Diamond hovercraft geometry — top diamond + bottom diamond meeting at rim.
  // Slightly elongated front-to-back so the nose direction is readable.
  // Body extents (local frame):
  //   +X = right, +Y = up, +Z = forward (nose)
  const float halfLen = 2.40f;     // nose to tail
  const float halfWid = 1.80f;     // wingtip to wingtip
  const float topY = 0.55f;        // dome peak height before cockpit
  const float rimY = 0.05f;        // rim plane (slightly above mid)
  const float botY = -0.45f;       // belly point

  const Vector3 nose = {0.0f, rimY, halfLen};
  const Vector3 tail = {0.0f, rimY, -halfLen};
  const Vector3 starboard = {halfWid, rimY, 0.0f};
  const Vector3 port = {-halfWid, rimY, 0.0f};
  const Vector3 apex = {0.0f, topY, 0.0f};
  const Vector3 belly = {0.0f, botY, 0.0f};

  std::vector<Tri> tris;
  tris.reserve(64);

  // ---- Top — four triangles meeting at apex ----
  tris.push_back({nose, starboard, apex, hullTop});
  tris.push_back({starboard, tail, apex, hullTop});
  tris.push_back({tail, port, apex, hullTop});
  tris.push_back({port, nose, apex, hullTop});

  // ---- Bottom — four triangles meeting at belly (reversed winding) ----
  tris.push_back({nose, belly, starboard, hullBot});
  tris.push_back({starboard, belly, tail, hullBot});
  tris.push_back({tail, belly, port, hullBot});
  tris.push_back({port, belly, nose, hullBot});

  // ---- Cockpit dome — small bubble centred on apex ----
  // Faceted dome: 8-sided ring at half-height, single peak above apex.
  constexpr int DOME_SEGS = 8;
  const float domeR = 0.42f;
  const float domeRingY = topY + 0.12f;
  const float domePeakY = topY + 0.42f;
  Vector3 domeRing[DOME_SEGS];
  for (int i = 0; i < DOME_SEGS; ++i) {
    float a = 2.0f * 3.14159265f * (float)i / (float)DOME_SEGS;
    domeRing[i] = {domeR * cosf(a), domeRingY, domeR * sinf(a)};
  }
  Vector3 domePeak = {0.0f, domePeakY, 0.0f};
  for (int i = 0; i < DOME_SEGS; ++i) {
    int j = (i + 1) % DOME_SEGS;
    tris.push_back({domeRing[i], domeRing[j], domePeak, domeGlass});
    // Dome base ring connects to apex (same hull colour as top)
    tris.push_back({apex, domeRing[i], domeRing[j], rim});
  }

  // ---- Rim accent — thin band at rimY between top and bottom diamonds ----
  // (Just visual interest — uses the rim colour on a few inset triangles
  //  that overlap the existing hull faces. Cheap and effective.)
  // No additional geometry needed — the litColour shading already differentiates
  // the top and bottom slabs naturally.

  // ---- Single rear thruster glow on the underside ----
  // A quad on the belly facing -Y so it shows as a coloured patch under the ship.
  const float thrW = 0.30f;
  const float thrZBack = -1.20f;
  const float thrZFwd = -0.40f;
  const float thrY = botY + 0.02f;
  Vector3 t_bl = {-thrW, thrY, thrZBack};
  Vector3 t_br = {thrW, thrY, thrZBack};
  Vector3 t_fr = {thrW, thrY, thrZFwd};
  Vector3 t_fl = {-thrW, thrY, thrZFwd};
  tris.push_back({t_bl, t_br, t_fr, thrusterGlow});
  tris.push_back({t_bl, t_fr, t_fl, thrusterGlow});

  // ================================================================
  // Upload to GPU
  // ================================================================
  int vertCount = static_cast<int>(tris.size()) * 3;
  m_mesh = {};
  m_mesh.vertexCount = vertCount;
  m_mesh.triangleCount = static_cast<int>(tris.size());
  m_mesh.vertices =
      static_cast<float *>(RL_MALLOC(vertCount * 3 * sizeof(float)));
  m_mesh.normals =
      static_cast<float *>(RL_MALLOC(vertCount * 3 * sizeof(float)));
  m_mesh.colors = static_cast<unsigned char *>(
      RL_MALLOC(vertCount * 4 * sizeof(unsigned char)));

  int vi = 0, ni = 0, ci = 0;
  for (const Tri &t : tris) {
    Color lit = litColour(t.col, t.a, t.b, t.c);
    Vector3 e1 = {t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z};
    Vector3 e2 = {t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z};
    Vector3 n = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                 e1.x * e2.y - e1.y * e2.x};
    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0.0f) {
      n.x /= len;
      n.y /= len;
      n.z /= len;
    }
    for (const Vector3 *v : {&t.a, &t.b, &t.c}) {
      m_mesh.vertices[vi++] = v->x;
      m_mesh.vertices[vi++] = v->y;
      m_mesh.vertices[vi++] = v->z;
      m_mesh.normals[ni++] = n.x;
      m_mesh.normals[ni++] = n.y;
      m_mesh.normals[ni++] = n.z;
      m_mesh.colors[ci++] = lit.r;
      m_mesh.colors[ci++] = lit.g;
      m_mesh.colors[ci++] = lit.b;
      m_mesh.colors[ci++] = lit.a;
    }
  }

  UploadMesh(&m_mesh, false);
  m_model = LoadModelFromMesh(m_mesh);
  m_built = true;
}

// ====================================================================
// Render — local→world transform: Rz(roll) → Rx(-pitch) → Ry(yaw) → T(pos)
// ====================================================================
void Player::render(const tsmesh::MeshRegistry *registry) const {
  // Build the full attitude matrix once; both paths use it. Order:
  // Rz(roll) → Rx(-pitch) → Ry(yaw) → T(position). Matches the
  // orientation accessors (forward/up/right) so what you see matches
  // what the physics is using.
  Matrix worldMat = MatrixMultiply(
      MatrixMultiply(
          MatrixMultiply(MatrixRotateZ(m_roll), MatrixRotateX(-m_pitch)),
          MatrixRotateY(m_yaw)),
      MatrixTranslate(m_pos.x, m_pos.y, m_pos.z));

  rlDisableBackfaceCulling();

  // Mesh-based path — if the registry has the OBJ loaded, render from
  // it. The hovercraft mesh has per-vertex colours baked in from the
  // palette so we pass WHITE as the material colour.
  const Model *playerModel = registry ? registry->playerModel() : nullptr;
  if (playerModel && playerModel->meshCount > 0) {
    DrawMesh(playerModel->meshes[0], playerModel->materials[0], worldMat);
  } else if (m_built) {
    // Procedural fallback — used until the OBJ pipeline migration is
    // complete or if the OBJ failed to load. buildMesh()'s output.
    DrawMesh(m_mesh, m_model.materials[0], worldMat);
  }

  rlEnableBackfaceCulling();
}

// ====================================================================
// Ground shadow — translucent dark disk on the terrain directly below
// the ship. Alpha fades linearly with AGL and radius shrinks (atmospheric
// scatter convention) so high-altitude shadows are small and faint.
//
// Each fan vertex samples the terrain height at its own world coords so
// the disk drapes over uneven ground rather than a single flat Y. This
// kills the z-fighting glitch you'd get when the flat disk's edge dips
// below a rising slope. Per-vertex margin keeps the disk above the
// chunk surface without floating visibly.
// ====================================================================
void Player::renderGroundShadow(const Planet &planet) const {
  if (!m_built) return;

  float centreGround = planet.heightAt(m_pos.x, m_pos.z);
  float agl = m_pos.y - centreGround;
  if (agl < 0.0f) agl = 0.0f;
  if (agl >= Config::SHADOW_FADE_MAX_AGL) return;

  // Alpha: pow(1 − t, e) with e>1 — more aggressive than linear in the
  // mid-range, so the shadow reads as faint surprisingly quickly.
  float linT = agl / Config::SHADOW_FADE_MAX_AGL;
  float fade = powf(1.0f - linT, Config::SHADOW_FADE_EXPONENT);
  unsigned char alpha =
      static_cast<unsigned char>(fade * Config::SHADOW_BASE_ALPHA);
  Color shadowCol = {0, 0, 0, alpha};

  // Radius: shrinks from 100% at AGL=0 to (1 − SHRINK) at fade max.
  const float r = Config::SHADOW_RADIUS *
                  (1.0f - Config::SHADOW_SHRINK * linT);

  // Per-vertex ground sampling — each fan vertex sits at its own local
  // terrain height plus a margin. Margin is generous (0.5) because
  // bilinear interpolation between vertices can dip the shadow's
  // triangle interior below the terrain even when all three vertices
  // are above their respective ground samples; the larger margin pushes
  // the whole disk far enough above to clear that case without visible
  // float at low AGL.
  constexpr int SEGS = 24;
  const float MARGIN = Config::SHADOW_MARGIN;

  Vector3 centre = {m_pos.x, centreGround + MARGIN, m_pos.z};

  rlDisableBackfaceCulling();
  Vector3 prev;
  {
    float px = m_pos.x + r;
    float pz = m_pos.z;
    prev = {px, planet.heightAt(px, pz) + MARGIN, pz};
  }
  for (int i = 1; i <= SEGS; ++i) {
    float a = 6.28318530f * static_cast<float>(i) / static_cast<float>(SEGS);
    float vx = m_pos.x + r * cosf(a);
    float vz = m_pos.z + r * sinf(a);
    float vy = planet.heightAt(vx, vz) + MARGIN;
    Vector3 v = {vx, vy, vz};
    DrawTriangle3D(centre, v, prev, shadowCol);
    prev = v;
  }
  rlEnableBackfaceCulling();
}

// ====================================================================
// Shield bubble — translucent low-poly sphere around the ship that
// fades in on any shield-absorbing hit and fades back to invisible
// over PLAYER_SHIELD_FLASH seconds. Dim blue with a quick alpha
// punch so the player gets an unmistakable "absorbed" cue without
// the bubble being a permanent visual.
//
// Low-poly slice/ring counts give the flat-shaded faceted look the
// rest of the game uses — solid lit sphere would clash with the
// 1988 aesthetic.
// ====================================================================
void Player::renderShieldBubble() const {
  if (m_shieldFlashTimer <= 0.0f) return;

  // Normalised flash progress: 1.0 just after impact, 0.0 at fadeout.
  float t = m_shieldFlashTimer / Config::PLAYER_SHIELD_FLASH;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  // Eased curve — quick punch then smooth tail. Squaring makes the
  // first 0.1s the visible peak; the rest is gentle decay.
  float alphaF = t * t;
  unsigned char alpha = static_cast<unsigned char>(alphaF * 110.0f);
  Color shield = {60, 140, 220, alpha};

  // 8 rings × 12 slices = 96 quads — coarse enough to read as
  // faceted, dense enough to look round at close range.
  DrawSphereEx(m_pos, Config::PLAYER_SHIELD_RADIUS, 8, 12, shield);
}
