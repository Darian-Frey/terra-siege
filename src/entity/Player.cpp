#include "Player.hpp"
#include "core/Config.hpp"
#include "rlgl.h"
#include "world/Planet.hpp"
#include <cmath>
#include <cstring>
#include <vector>

// ================================================================
// Accessors
// ================================================================
Vector3 Player::forward() const { return {sinf(m_yaw), 0.0f, cosf(m_yaw)}; }

Vector3 Player::right() const { return {cosf(m_yaw), 0.0f, -sinf(m_yaw)}; }

float Player::speed() const { return Vector3Length(m_vel); }

void Player::applyDamage(float amount) {
  m_health -= amount;
  if (m_health < 0.0f)
    m_health = 0.0f;
}

void Player::setFlightAssist(int level) {
  m_assistLevel = level < 0 ? 0 : level > 3 ? 3 : level;
}

// ================================================================
// Ship mesh — procedural flat-shaded hovercraft
// All vertices in local space: +Z = forward (nose), +Y = up
// ================================================================

struct Tri {
  Vector3 a, b, c;
  Color col;
};

// Compute face normal and apply directional lighting
static Color litColour(Color base, Vector3 a, Vector3 b, Vector3 c) {
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

  // Sun direction (same as terrain)
  const float sx = 0.57f, sy = 0.74f, sz = 0.36f;
  float diff = sx * n.x + sy * n.y + sz * n.z;
  if (diff < 0.0f)
    diff = 0.0f;
  float light = 0.38f + 0.62f * diff;

  auto cu = [](float v) -> unsigned char {
    int i = static_cast<int>(v);
    return static_cast<unsigned char>(i < 0 ? 0 : i > 255 ? 255 : i);
  };
  return {cu(base.r * light), cu(base.g * light), cu(base.b * light), 255};
}

// Add a box — all 6 faces, 12 triangles
static void addBox(std::vector<Tri> &tris, float x0, float y0, float z0,
                   float x1, float y1, float z1, Color topC, Color sideC,
                   Color botC) {
  Vector3 tlb = {x0, y1, z0}, trb = {x1, y1, z0}, trf = {x1, y1, z1},
          tlf = {x0, y1, z1};
  Vector3 blb = {x0, y0, z0}, brb = {x1, y0, z0}, brf = {x1, y0, z1},
          blf = {x0, y0, z1};

  tris.push_back({tlf, trb, tlb, topC}); // top
  tris.push_back({tlf, trf, trb, topC});
  tris.push_back({blf, blb, brb, botC}); // bottom
  tris.push_back({blf, brb, brf, botC});
  tris.push_back({blf, brf, trf, sideC}); // front (+Z)
  tris.push_back({blf, trf, tlf, sideC});
  tris.push_back({brb, blb, tlb, sideC}); // back (-Z)
  tris.push_back({brb, tlb, trb, sideC});
  tris.push_back({brf, brb, trb, sideC}); // right (+X)
  tris.push_back({brf, trb, trf, sideC});
  tris.push_back({blb, blf, tlf, sideC}); // left (-X)
  tris.push_back({blb, tlf, tlb, sideC});
}

void Player::buildMesh() {
  // ---- Colour palette ----
  const Color hullTop = {75, 105, 80, 255};  // olive green top
  const Color hullSide = {55, 75, 60, 255};  // darker green sides
  const Color hullBot = {40, 55, 45, 255};   // very dark bottom
  const Color podTop = {60, 65, 70, 255};    // dark grey-blue pod top
  const Color podSide = {45, 50, 55, 255};   // darker pod sides
  const Color podBot = {30, 35, 38, 255};    // pod bottom
  const Color nozzle = {255, 110, 20, 255};  // engine glow orange
  const Color cockTop = {50, 70, 95, 255};   // dark blue cockpit
  const Color cockSide = {65, 95, 130, 255}; // lighter glass blue
  const Color stripe = {180, 40, 40, 255};   // red accent stripe

  std::vector<Tri> tris;
  tris.reserve(128);

  // ================================================================
  // 1. Main hull — 6-point hexagonal outline (top and bottom surfaces)
  //    +Z = nose (forward), -Z = tail, +X = right wing
  // ================================================================
  const Vector3 t_nose = {0.00f, 0.48f, 3.00f};
  const Vector3 t_lfw = {-1.80f, 0.28f, 0.90f};
  const Vector3 t_rfw = {1.80f, 0.28f, 0.90f};
  const Vector3 t_lrw = {-1.60f, 0.16f, -1.60f};
  const Vector3 t_rrw = {1.60f, 0.16f, -1.60f};
  const Vector3 t_tail = {0.00f, 0.28f, -2.80f};

  const float BY = -0.28f; // bottom Y
  const Vector3 b_nose = {0.00f, BY, 3.00f};
  const Vector3 b_lfw = {-1.80f, BY, 0.90f};
  const Vector3 b_rfw = {1.80f, BY, 0.90f};
  const Vector3 b_lrw = {-1.60f, BY, -1.60f};
  const Vector3 b_rrw = {1.60f, BY, -1.60f};
  const Vector3 b_tail = {0.00f, BY, -2.80f};

  // Top face — fan from nose (CCW from above = +Y normal)
  tris.push_back({t_nose, t_lfw, t_lrw, hullTop});
  tris.push_back({t_nose, t_lrw, t_tail, hullTop});
  tris.push_back({t_nose, t_tail, t_rrw, hullTop});
  tris.push_back({t_nose, t_rrw, t_rfw, hullTop});

  // Bottom face — reversed winding (-Y normal)
  tris.push_back({b_nose, b_lrw, b_lfw, hullBot});
  tris.push_back({b_nose, b_tail, b_lrw, hullBot});
  tris.push_back({b_nose, b_rrw, b_tail, hullBot});
  tris.push_back({b_nose, b_rfw, b_rrw, hullBot});

  // Side skirts — 6 quads (each quad = 2 tris)
  auto quad = [&](Vector3 tA, Vector3 tB, Vector3 bA, Vector3 bB, Color c) {
    tris.push_back({tA, tB, bB, c});
    tris.push_back({tA, bB, bA, c});
  };
  // Left front face (nose → lfw)
  quad(t_nose, t_lfw, b_nose, b_lfw, hullSide);
  // Left rear face (lfw → lrw)
  quad(t_lfw, t_lrw, b_lfw, b_lrw, hullSide);
  // Left tail face (lrw → tail)
  quad(t_lrw, t_tail, b_lrw, b_tail, hullSide);
  // Right tail face (tail → rrw)
  quad(t_tail, t_rrw, b_tail, b_rrw, hullSide);
  // Right rear face (rrw → rfw)
  quad(t_rrw, t_rfw, b_rrw, b_rfw, hullSide);
  // Right front face (rfw → nose)
  quad(t_rfw, t_nose, b_rfw, b_nose, hullSide);

  // ================================================================
  // 2. Engine pods — left and right rectangular nacelles
  // ================================================================
  addBox(tris, -2.60f, -0.30f, -0.30f, -1.80f, 0.24f, 1.30f, podTop, podSide,
         podBot);
  addBox(tris, 1.80f, -0.30f, -0.30f, 2.60f, 0.24f, 1.30f, podTop, podSide,
         podBot);

  // Engine nozzle glow — flat quad at rear of each pod (-Z face, orange)
  // Left nozzle
  tris.push_back({{-2.55f, -0.22f, -0.30f},
                  {-2.55f, 0.18f, -0.30f},
                  {-1.85f, 0.18f, -0.30f},
                  nozzle});
  tris.push_back({{-2.55f, -0.22f, -0.30f},
                  {-1.85f, 0.18f, -0.30f},
                  {-1.85f, -0.22f, -0.30f},
                  nozzle});
  // Right nozzle
  tris.push_back({{1.85f, -0.22f, -0.30f},
                  {1.85f, 0.18f, -0.30f},
                  {2.55f, 0.18f, -0.30f},
                  nozzle});
  tris.push_back({{1.85f, -0.22f, -0.30f},
                  {2.55f, 0.18f, -0.30f},
                  {2.55f, -0.22f, -0.30f},
                  nozzle});

  // ================================================================
  // 3. Cockpit — raised blister behind the nose
  // ================================================================
  addBox(tris, -0.45f, 0.25f, 0.42f, 0.45f, 0.58f, 1.05f, cockTop, cockSide,
         hullTop);

  // ================================================================
  // 4. Accent stripe — thin red band across the middle of the hull
  // ================================================================
  addBox(tris, -1.55f, 0.24f, 0.15f, 1.55f, 0.28f, 0.50f, stripe, stripe,
         stripe);

  // ================================================================
  // Upload mesh
  // ================================================================
  int vertCount = static_cast<int>(tris.size()) * 3;
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
    if (len > 0) {
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

// ================================================================
// init
// ================================================================
void Player::init(Vector3 startPos, int flightAssistLevel) {
  m_pos = startPos;
  m_vel = {0, 0, 0};
  m_yaw = 0.0f;
  m_roll = 0.0f;
  m_pitchVis = 0.0f;
  m_health = 100.0f;
  m_assistLevel = flightAssistLevel;

  buildMesh();
}

// ================================================================
// handleInput
// ================================================================
void Player::handleInput(float dt) {
  // Turn (yaw)
  m_turnInput = 0.0f;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
    m_turnInput -= 1.0f;
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
    m_turnInput += 1.0f;
  m_yaw += m_turnInput * Config::PLAYER_TURN_RATE * dt;

  // Thrust — along forward vector
  m_thrusting = false;
  Vector3 fwd = forward();
  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
    float boost = IsKeyDown(KEY_LEFT_SHIFT) ? 2.5f : 1.0f;
    m_vel = Vector3Add(m_vel,
                       Vector3Scale(fwd, Config::PLAYER_THRUST * boost * dt));
    m_thrusting = true;
  }
  // Brake / reverse
  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
    m_vel = Vector3Add(m_vel,
                       Vector3Scale(fwd, -Config::PLAYER_THRUST * 0.4f * dt));

  // Altitude
  if (IsKeyDown(KEY_E))
    m_pos.y += Config::PLAYER_ALT_RATE * dt;
  if (IsKeyDown(KEY_Q))
    m_pos.y -= Config::PLAYER_ALT_RATE * dt;
}

// ================================================================
// applyFlightAssist
// ================================================================
void Player::applyFlightAssist(float dt) {
  float coeff = Config::ASSIST_LEVEL_COEFFS[m_assistLevel];
  if (coeff <= 0.0f)
    return;

  // Level 1+: auto-level roll back toward 0
  float rollReturn = coeff * 5.0f * dt;
  m_roll *= (1.0f - rollReturn);

  // Level 2+: dampen lateral (sideways) velocity — stops drifting
  if (m_assistLevel >= 2) {
    Vector3 r = right();
    float lateralSpeed = Vector3DotProduct(m_vel, r);
    float dampen = coeff * 0.35f;
    m_vel = Vector3Subtract(m_vel, Vector3Scale(r, lateralSpeed * dampen));
  }

  // Level 3: predictive terrain avoidance — look ahead and push up
  if (m_assistLevel >= 3) {
    // Not connected to Planet here — handled in applyPhysics
  }
}

// ================================================================
// applyPhysics
// ================================================================
void Player::applyPhysics(float dt, const Planet &planet) {
  // Frame-rate independent drag
  float dragFactor = powf(Config::PLAYER_DRAG, dt * 120.0f);
  m_vel = Vector3Scale(m_vel, dragFactor);

  // Speed cap
  float spd = Vector3Length(m_vel);
  if (spd > Config::PLAYER_MAX_SPEED)
    m_vel = Vector3Scale(m_vel, Config::PLAYER_MAX_SPEED / spd);

  // Update position
  m_pos = Vector3Add(m_pos, Vector3Scale(m_vel, dt));

  // Terrain altitude clamping
  float groundH = planet.heightAt(m_pos.x, m_pos.z);
  float minH = groundH + Config::PLAYER_MIN_ALTITUDE;
  if (m_pos.y < minH) {
    m_pos.y = minH;
    if (m_vel.y < 0.0f)
      m_vel.y = 0.0f;
  }

  // Hard ceiling
  if (m_pos.y > Config::PLAYER_MAX_ALTITUDE + groundH)
    m_pos.y = Config::PLAYER_MAX_ALTITUDE + groundH;

  // Level 3 terrain look-ahead: push up if terrain ahead is high
  if (m_assistLevel >= 3) {
    float lookAhead = Config::ASSIST_TERRAIN_LOOKAHEAD;
    Vector3 fwd = forward();
    float aheadX = m_pos.x + fwd.x * spd * lookAhead;
    float aheadZ = m_pos.z + fwd.z * spd * lookAhead;
    float aheadH = planet.heightAt(aheadX, aheadZ);
    float needed = aheadH + Config::PLAYER_MIN_ALTITUDE * 3.0f;
    if (m_pos.y < needed) {
      float push = (needed - m_pos.y) * 4.0f * dt;
      m_pos.y += push;
    }
  }

  // Visual banking — driven by turn input
  float targetRoll = -m_turnInput * 0.45f;
  m_roll += (targetRoll - m_roll) * dt * 5.0f;

  // Visual pitch — disabled for now; the rotation around model origin
  // shifts the ship's apparent screen position, creating a false
  // impression of altitude change. Revisit with a smaller value once
  // combat is in and the camera feel is locked down.
  // float targetPitch = m_thrusting ? -0.03f : 0.0f;
  // m_pitchVis += (targetPitch - m_pitchVis) * dt * 4.0f;
  m_pitchVis = 0.0f;
}

// ================================================================
// update
// ================================================================
void Player::update(float dt, const Planet &planet) {
  handleInput(dt);
  applyFlightAssist(dt);
  applyPhysics(dt, planet);
}

// ================================================================
// render
// ================================================================
void Player::render() const {
  if (!m_built)
    return;

  // raylib uses row-vector convention: v * M, operations left-to-right.
  // So the order is: pitch → roll → yaw → translate (local to world).
  Matrix worldMat = MatrixMultiply(
      MatrixMultiply(
          MatrixMultiply(MatrixRotateX(m_pitchVis), MatrixRotateZ(m_roll)),
          MatrixRotateY(m_yaw)),
      MatrixTranslate(m_pos.x, m_pos.y, m_pos.z));

  rlDisableBackfaceCulling();
  DrawMesh(m_mesh, m_model.materials[0], worldMat);
  rlEnableBackfaceCulling();
}

// ================================================================
// unload
// ================================================================
void Player::unload() {
  if (m_built) {
    UnloadModel(m_model);
    m_built = false;
  }
}