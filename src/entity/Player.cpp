#include "Player.hpp"
#include "core/Config.hpp"
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
  m_health = 100.0f;
  m_thrusting = false;
  m_landed = false;
  m_assistLevel = flightAssistLevel < 0   ? 0
                  : flightAssistLevel > 3 ? 3
                                          : flightAssistLevel;

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
  m_health -= amount;
  if (m_health < 0.0f)
    m_health = 0.0f;
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
  // ---- Mouse delta with low-pass filter ----
  Vector2 raw = GetMouseDelta();
  float mouseSmooth = 1.0f - expf(-Config::NEWTON_INPUT_SMOOTH * dt);
  m_smoothMouse.x += (raw.x - m_smoothMouse.x) * mouseSmooth;
  m_smoothMouse.y += (raw.y - m_smoothMouse.y) * mouseSmooth;

  // ---- Pitch (mouse Y + keyboard W/S, S/up arrow nose-up) ----
  // Mouse forward (dy negative) tilts nose DOWN — helicopter convention.
  float pitchDelta = -m_smoothMouse.y * Config::NEWTON_MOUSE_PITCH_SENS;
  if (IsKeyDown(KEY_DOWN)) // pull stick back = nose up
    pitchDelta += Config::NEWTON_PITCH_RATE * dt;
  if (IsKeyDown(KEY_UP)) // push stick forward = nose down
    pitchDelta -= Config::NEWTON_PITCH_RATE * dt;
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

  // ---- Thrust (W or LMB) — gated on charge being non-empty ----
  m_thrusting = (IsKeyDown(KEY_W) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) &&
                m_thrustCharge > 0.0f;
}

// ====================================================================
// Flight assist — corrective layer. Does NOT replace physics; layered on top.
//   Level 0: Raw           — no correction
//   Level 1: Minimal       — auto-level roll
//   Level 2: Standard      — auto-level roll + auto-reduce pitch
//   Level 3: Full          — Standard + terrain-avoidance look-ahead
// ====================================================================
void Player::applyFlightAssist(float dt, const Planet &planet) {
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
  // m_infiniteCharge (DEV F3) the meter is pinned at MAX and never drains.
  if (m_thrusting && !ceilingCut && m_thrustCharge > 0.0f) {
    Vector3 thrustDir = up();
    m_vel = Vector3Add(m_vel, Vector3Scale(thrustDir,
                                           Config::NEWTON_THRUST * dt));
    if (!m_infiniteCharge) {
      m_thrustCharge -= Config::NEWTON_THRUST_DRAIN_RATE * dt;
      if (m_thrustCharge <= 0.0f) {
        m_thrustCharge = 0.0f;
        m_thrusting = false;
      }
    }
  } else if (!m_infiniteCharge) {
    m_thrustCharge += Config::NEWTON_THRUST_RECHARGE_RATE * dt;
    if (m_thrustCharge > Config::NEWTON_THRUST_CHARGE_MAX)
      m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;
  }
  if (m_infiniteCharge)
    m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;

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
}

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
void Player::render() const {
  if (!m_built) return;

  Matrix worldMat = MatrixMultiply(
      MatrixMultiply(
          MatrixMultiply(MatrixRotateZ(m_roll), MatrixRotateX(-m_pitch)),
          MatrixRotateY(m_yaw)),
      MatrixTranslate(m_pos.x, m_pos.y, m_pos.z));

  rlDisableBackfaceCulling();
  DrawMesh(m_mesh, m_model.materials[0], worldMat);
  rlEnableBackfaceCulling();
}
