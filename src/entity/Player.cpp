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
Vector3 Player::forward() const {
  // Includes pitch — climbing/diving tilts the forward vector,
  // which is what locks velocity to the ship's nose direction.
  float cp = cosf(m_pitch);
  return {cp * sinf(m_yaw), sinf(m_pitch), cp * cosf(m_yaw)};
}

Vector3 Player::right() const { return {cosf(m_yaw), 0.0f, -sinf(m_yaw)}; }

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

// Friendly name for HUD / menu
const char *craftName(CraftType c) {
  switch (c) {
  case CraftType::DeltaWing:    return "Delta Wing";
  case CraftType::ForwardSwept: return "Forward Swept";
  case CraftType::X36:          return "X-36";
  case CraftType::YB49:         return "YB-49 Flying Wing";
  default:                      return "Unknown";
  }
}

// ================================================================
// Delta Wing — modern swept-back fighter (current default)
// ================================================================
static void buildDeltaWingMesh(std::vector<Tri> &tris) {
  // ---- Colour palette — modern delta-wing fighter ----
  const Color wingTop = {72, 88, 98, 255};      // slate blue-grey top
  const Color wingBot = {32, 40, 48, 255};      // dark underside
  const Color wingEdge = {55, 65, 75, 255};     // wing edge strip
  const Color cockpitGlass = {35, 80, 130, 255};// dark blue canopy
  const Color cockpitFrame = {40, 48, 55, 255}; // canopy frame
  const Color stripe = {180, 45, 45, 255};      // red nose stripe
  const Color engineCasing = {50, 55, 62, 255}; // engine metal
  const Color engineTop = {65, 72, 80, 255};    // engine top
  const Color engineBot = {32, 38, 44, 255};    // engine bottom
  const Color nozzle = {255, 130, 30, 255};     // nozzle glow

  // ================================================================
  // 1. Delta wing outline — 5-point (nose, 2 wing tips, 2 tail corners)
  //    +Z = nose, -Z = tail, +X = right, +Y = up
  // ================================================================
  const Vector3 n_t = {0.00f, 0.12f, 3.40f};    // nose top
  const Vector3 lw_t = {-2.50f, -0.05f, -2.00f};// left wing tip top
  const Vector3 rw_t = {2.50f, -0.05f, -2.00f}; // right wing tip top
  const Vector3 lt_t = {-0.90f, 0.08f, -2.60f}; // left tail top
  const Vector3 rt_t = {0.90f, 0.08f, -2.60f};  // right tail top

  const Vector3 n_b = {0.00f, -0.05f, 3.40f};
  const Vector3 lw_b = {-2.50f, -0.18f, -2.00f};
  const Vector3 rw_b = {2.50f, -0.18f, -2.00f};
  const Vector3 lt_b = {-0.90f, -0.18f, -2.60f};
  const Vector3 rt_b = {0.90f, -0.18f, -2.60f};

  // Top wing surface — three triangles (left half, right half, middle)
  tris.push_back({n_t, lt_t, lw_t, wingTop});
  tris.push_back({n_t, rw_t, rt_t, wingTop});
  tris.push_back({n_t, rt_t, lt_t, wingTop});

  // Bottom wing surface — reversed winding for -Y normal
  tris.push_back({n_b, lw_b, lt_b, wingBot});
  tris.push_back({n_b, rt_b, rw_b, wingBot});
  tris.push_back({n_b, lt_b, rt_b, wingBot});

  // Left leading edge — from nose out to left wing tip
  tris.push_back({n_t, lw_t, lw_b, wingEdge});
  tris.push_back({n_t, lw_b, n_b, wingEdge});
  // Right leading edge
  tris.push_back({n_t, n_b, rw_b, wingEdge});
  tris.push_back({n_t, rw_b, rw_t, wingEdge});

  // Left wing-tip / outer trailing edge (lw → lt)
  tris.push_back({lw_t, lw_b, lt_b, wingEdge});
  tris.push_back({lw_t, lt_b, lt_t, wingEdge});
  // Right wing-tip / outer trailing edge
  tris.push_back({rw_t, rt_t, rt_b, wingEdge});
  tris.push_back({rw_t, rt_b, rw_b, wingEdge});

  // Tail edge (lt → rt)
  tris.push_back({lt_t, lt_b, rt_b, wingEdge});
  tris.push_back({lt_t, rt_b, rt_t, wingEdge});

  // ================================================================
  // 2. Cockpit canopy — faceted bubble on top of the central spine
  // ================================================================
  const Vector3 ck_fl = {-0.32f, 0.12f, 1.80f}; // front-left base
  const Vector3 ck_fr = {0.32f, 0.12f, 1.80f};  // front-right base
  const Vector3 ck_rl = {-0.40f, 0.12f, 0.20f}; // rear-left base
  const Vector3 ck_rr = {0.40f, 0.12f, 0.20f};  // rear-right base
  const Vector3 ck_pk = {0.00f, 0.55f, 1.00f};  // peak (center top)
  const Vector3 ck_ft = {0.00f, 0.30f, 2.00f};  // tapered front-top
  const Vector3 ck_bt = {0.00f, 0.35f, 0.05f};  // tapered rear-top

  // Sloped front (nose of canopy)
  tris.push_back({ck_fl, ck_fr, ck_ft, cockpitFrame});
  // Top panels — faceted bubble from peak outward
  tris.push_back({ck_ft, ck_fr, ck_pk, cockpitGlass});
  tris.push_back({ck_ft, ck_pk, ck_fl, cockpitGlass});
  tris.push_back({ck_pk, ck_fr, ck_rr, cockpitGlass});
  tris.push_back({ck_pk, ck_rr, ck_bt, cockpitGlass});
  tris.push_back({ck_pk, ck_bt, ck_rl, cockpitGlass});
  tris.push_back({ck_pk, ck_rl, ck_fl, cockpitGlass});
  // Rear panel
  tris.push_back({ck_bt, ck_rr, ck_rl, cockpitFrame});

  // ================================================================
  // 3. Red nose stripe — thin painted band on the centerline
  //    forward of the cockpit, sitting just above the wing surface
  // ================================================================
  tris.push_back({{-0.10f, 0.13f, 2.95f},
                  {0.10f, 0.13f, 2.95f},
                  {0.08f, 0.13f, 2.10f},
                  stripe});
  tris.push_back({{-0.10f, 0.13f, 2.95f},
                  {0.08f, 0.13f, 2.10f},
                  {-0.08f, 0.13f, 2.10f},
                  stripe});

  // ================================================================
  // 4. Engine nacelles — two low-profile intakes at the rear
  //    Flush against the wing top, extending slightly beyond the tail
  // ================================================================
  addBox(tris, -1.35f, 0.08f, -2.80f, -0.55f, 0.28f, -1.30f,
         engineTop, engineCasing, engineBot);
  addBox(tris, 0.55f, 0.08f, -2.80f, 1.35f, 0.28f, -1.30f,
         engineTop, engineCasing, engineBot);

  // Engine nozzle glow — rectangles on the back face of each engine
  // Left nozzle
  tris.push_back({{-1.25f, 0.12f, -2.81f},
                  {-1.25f, 0.24f, -2.81f},
                  {-0.65f, 0.24f, -2.81f},
                  nozzle});
  tris.push_back({{-1.25f, 0.12f, -2.81f},
                  {-0.65f, 0.24f, -2.81f},
                  {-0.65f, 0.12f, -2.81f},
                  nozzle});
  // Right nozzle
  tris.push_back({{0.65f, 0.12f, -2.81f},
                  {0.65f, 0.24f, -2.81f},
                  {1.25f, 0.24f, -2.81f},
                  nozzle});
  tris.push_back({{0.65f, 0.12f, -2.81f},
                  {1.25f, 0.24f, -2.81f},
                  {1.25f, 0.12f, -2.81f},
                  nozzle});
}

// ================================================================
// Forward-Swept Wing — X-29/Su-47 style experimental fighter.
// Wings sweep FORWARD (tips are ahead of roots), small canards near
// the nose, single vertical tail fin at the rear.
// ================================================================
static void buildForwardSweptMesh(std::vector<Tri> &tris) {
  // ---- Colour palette ----
  const Color hullTop = {50, 58, 66, 255};     // slate body top
  const Color hullSide = {38, 44, 50, 255};    // darker sides
  const Color hullBot = {25, 30, 35, 255};     // dark underside
  const Color wingTop = {55, 62, 70, 255};     // dark grey wing top
  const Color wingBot = {30, 35, 42, 255};     // dark wing underside
  const Color wingEdge = {42, 48, 55, 255};    // wing edge
  const Color accent = {220, 95, 30, 255};     // bright orange accent
  const Color accentDark = {160, 65, 20, 255}; // shadowed accent
  const Color cockpitGlass = {35, 80, 130, 255};
  const Color cockpitFrame = {40, 48, 55, 255};
  const Color nozzle = {255, 140, 40, 255};

  // ================================================================
  // 1. Fuselage — long central body (use addBox for most of it)
  // ================================================================
  // Main body box
  addBox(tris, -0.35f, -0.20f, -2.30f, 0.35f, 0.20f, 1.90f,
         hullTop, hullSide, hullBot);

  // Pointed nose cone — triangles from the tip out to the front face
  const Vector3 tip = {0.00f, 0.05f, 3.40f};
  const Vector3 ft_tl = {-0.35f, 0.20f, 1.90f};
  const Vector3 ft_tr = {0.35f, 0.20f, 1.90f};
  const Vector3 ft_bl = {-0.35f, -0.20f, 1.90f};
  const Vector3 ft_br = {0.35f, -0.20f, 1.90f};

  // Four triangular faces of the nose cone
  tris.push_back({tip, ft_tr, ft_tl, hullTop});  // top
  tris.push_back({tip, ft_bl, ft_br, hullBot});  // bottom
  tris.push_back({tip, ft_tl, ft_bl, hullSide}); // left
  tris.push_back({tip, ft_br, ft_tr, hullSide}); // right

  // ================================================================
  // 2. Forward-swept main wings — signature feature
  //    Wing ROOT at rear of fuselage, wing TIPS ahead of root.
  // ================================================================
  // Left wing — 4-point outline (root rear, root front, tip front, tip rear)
  const Vector3 lw_rr_t = {-0.35f, 0.02f, -1.60f};
  const Vector3 lw_rf_t = {-0.35f, 0.02f, -0.30f};
  const Vector3 lw_tf_t = {-2.80f, 0.00f, 1.10f};
  const Vector3 lw_tr_t = {-2.80f, 0.00f, 0.30f};

  const Vector3 lw_rr_b = {-0.35f, -0.08f, -1.60f};
  const Vector3 lw_rf_b = {-0.35f, -0.08f, -0.30f};
  const Vector3 lw_tf_b = {-2.80f, -0.10f, 1.10f};
  const Vector3 lw_tr_b = {-2.80f, -0.10f, 0.30f};

  // Top surface (CCW from above → +Y normal)
  tris.push_back({lw_rr_t, lw_rf_t, lw_tf_t, wingTop});
  tris.push_back({lw_rr_t, lw_tf_t, lw_tr_t, wingTop});
  // Bottom surface (reversed)
  tris.push_back({lw_rr_b, lw_tf_b, lw_rf_b, wingBot});
  tris.push_back({lw_rr_b, lw_tr_b, lw_tf_b, wingBot});
  // Leading edge (root front → tip front)
  tris.push_back({lw_rf_t, lw_rf_b, lw_tf_b, wingEdge});
  tris.push_back({lw_rf_t, lw_tf_b, lw_tf_t, wingEdge});
  // Trailing edge (tip rear → root rear)
  tris.push_back({lw_tr_t, lw_tr_b, lw_rr_b, wingEdge});
  tris.push_back({lw_tr_t, lw_rr_b, lw_rr_t, wingEdge});
  // Wing tip edge (tip front → tip rear)
  tris.push_back({lw_tf_t, lw_tf_b, lw_tr_b, wingEdge});
  tris.push_back({lw_tf_t, lw_tr_b, lw_tr_t, wingEdge});

  // Right wing — mirrored
  const Vector3 rw_rr_t = {0.35f, 0.02f, -1.60f};
  const Vector3 rw_rf_t = {0.35f, 0.02f, -0.30f};
  const Vector3 rw_tf_t = {2.80f, 0.00f, 1.10f};
  const Vector3 rw_tr_t = {2.80f, 0.00f, 0.30f};
  const Vector3 rw_rr_b = {0.35f, -0.08f, -1.60f};
  const Vector3 rw_rf_b = {0.35f, -0.08f, -0.30f};
  const Vector3 rw_tf_b = {2.80f, -0.10f, 1.10f};
  const Vector3 rw_tr_b = {2.80f, -0.10f, 0.30f};

  // Top (reversed winding for right side)
  tris.push_back({rw_rr_t, rw_tr_t, rw_tf_t, wingTop});
  tris.push_back({rw_rr_t, rw_tf_t, rw_rf_t, wingTop});
  // Bottom
  tris.push_back({rw_rr_b, rw_rf_b, rw_tf_b, wingBot});
  tris.push_back({rw_rr_b, rw_tf_b, rw_tr_b, wingBot});
  // Leading edge
  tris.push_back({rw_rf_t, rw_tf_t, rw_tf_b, wingEdge});
  tris.push_back({rw_rf_t, rw_tf_b, rw_rf_b, wingEdge});
  // Trailing edge
  tris.push_back({rw_tr_t, rw_rr_t, rw_rr_b, wingEdge});
  tris.push_back({rw_tr_t, rw_rr_b, rw_tr_b, wingEdge});
  // Wing tip edge
  tris.push_back({rw_tf_t, rw_tr_t, rw_tr_b, wingEdge});
  tris.push_back({rw_tf_t, rw_tr_b, rw_tf_b, wingEdge});

  // ================================================================
  // 3. Canards — small forward control wings near the nose
  // ================================================================
  // Left canard: simple triangular wing, painted orange
  const Vector3 lc_root_r = {-0.35f, 0.15f, 1.50f};
  const Vector3 lc_root_f = {-0.35f, 0.15f, 2.10f};
  const Vector3 lc_tip =    {-1.00f, 0.15f, 1.75f};
  tris.push_back({lc_root_r, lc_root_f, lc_tip, accent});
  tris.push_back({lc_root_r, lc_tip, lc_root_f, accentDark}); // underside
  // Right canard
  const Vector3 rc_root_r = {0.35f, 0.15f, 1.50f};
  const Vector3 rc_root_f = {0.35f, 0.15f, 2.10f};
  const Vector3 rc_tip =    {1.00f, 0.15f, 1.75f};
  tris.push_back({rc_root_r, rc_tip, rc_root_f, accent});
  tris.push_back({rc_root_r, rc_root_f, rc_tip, accentDark}); // underside

  // ================================================================
  // 4. Vertical tail fin — single swept-back orange fin
  // ================================================================
  const Vector3 tf_bf = {0.00f, 0.20f, -1.00f};  // base front
  const Vector3 tf_br = {0.00f, 0.20f, -2.30f};  // base rear
  const Vector3 tf_tf = {0.00f, 0.95f, -1.60f};  // top front
  const Vector3 tf_tr = {0.00f, 0.95f, -2.30f};  // top rear
  // Two sides of the fin (both painted accent; flat-sided fin)
  tris.push_back({tf_bf, tf_tf, tf_tr, accent});
  tris.push_back({tf_bf, tf_tr, tf_br, accent});
  tris.push_back({tf_bf, tf_tr, tf_tf, accentDark});
  tris.push_back({tf_bf, tf_br, tf_tr, accentDark});

  // ================================================================
  // 5. Cockpit canopy — low blister on top of the forward fuselage
  // ================================================================
  const Vector3 ck_fl = {-0.25f, 0.20f, 1.40f};
  const Vector3 ck_fr = {0.25f, 0.20f, 1.40f};
  const Vector3 ck_rl = {-0.30f, 0.20f, 0.30f};
  const Vector3 ck_rr = {0.30f, 0.20f, 0.30f};
  const Vector3 ck_pk = {0.00f, 0.45f, 0.85f};
  tris.push_back({ck_fl, ck_fr, ck_pk, cockpitGlass});
  tris.push_back({ck_fr, ck_rr, ck_pk, cockpitGlass});
  tris.push_back({ck_rr, ck_rl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_fl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_rr, ck_fr, cockpitFrame}); // rear panel closing

  // ================================================================
  // 6. Engine nozzles — two orange glows on the back of the fuselage
  // ================================================================
  // Left nozzle quad on the back face of the fuselage (z = -2.30)
  tris.push_back({{-0.30f, -0.15f, -2.31f},
                  {-0.30f, 0.15f, -2.31f},
                  {-0.05f, 0.15f, -2.31f},
                  nozzle});
  tris.push_back({{-0.30f, -0.15f, -2.31f},
                  {-0.05f, 0.15f, -2.31f},
                  {-0.05f, -0.15f, -2.31f},
                  nozzle});
  // Right nozzle
  tris.push_back({{0.05f, -0.15f, -2.31f},
                  {0.05f, 0.15f, -2.31f},
                  {0.30f, 0.15f, -2.31f},
                  nozzle});
  tris.push_back({{0.05f, -0.15f, -2.31f},
                  {0.30f, 0.15f, -2.31f},
                  {0.30f, -0.15f, -2.31f},
                  nozzle});
}

// ================================================================
// X-36 — NASA tailless lambda-wing demonstrator.
// Long pointed fuselage, twin canards, kinked (lambda) main wing,
// no vertical tail, single rear engine.
// ================================================================
static void buildX36Mesh(std::vector<Tri> &tris) {
  // ---- Colour palette — clean prototype white/grey ----
  const Color hullTop = {180, 180, 185, 255};   // light grey top
  const Color hullSide = {130, 130, 135, 255};  // mid grey
  const Color hullBot = {80, 82, 88, 255};      // dark underside
  const Color wingTop = {165, 165, 170, 255};
  const Color wingBot = {75, 78, 84, 255};
  const Color wingEdge = {110, 112, 118, 255};
  const Color canard = {150, 152, 158, 255};
  const Color cockpitGlass = {35, 80, 130, 255};
  const Color cockpitFrame = {60, 65, 72, 255};
  const Color nozzle = {255, 140, 40, 255};
  const Color stripe = {180, 45, 45, 255};

  // ================================================================
  // 1. Fuselage — long slim body with pointed nose
  // ================================================================
  addBox(tris, -0.28f, -0.18f, -2.20f, 0.28f, 0.18f, 1.80f,
         hullTop, hullSide, hullBot);

  // Pointed nose cone
  const Vector3 tip = {0.00f, 0.00f, 3.40f};
  const Vector3 ft_tl = {-0.28f, 0.18f, 1.80f};
  const Vector3 ft_tr = {0.28f, 0.18f, 1.80f};
  const Vector3 ft_bl = {-0.28f, -0.18f, 1.80f};
  const Vector3 ft_br = {0.28f, -0.18f, 1.80f};
  tris.push_back({tip, ft_tr, ft_tl, hullTop});
  tris.push_back({tip, ft_bl, ft_br, hullBot});
  tris.push_back({tip, ft_tl, ft_bl, hullSide});
  tris.push_back({tip, ft_br, ft_tr, hullSide});

  // ================================================================
  // 2. Lambda main wing — kinked leading edge, blended into fuselage
  //    5 points per side (root_front, kink, tip_front, tip_rear, root_rear)
  // ================================================================
  // LEFT WING
  const Vector3 lw_rf_t = {-0.28f, -0.05f, 0.40f};
  const Vector3 lw_k_t  = {-1.30f, -0.08f, -0.10f};
  const Vector3 lw_tf_t = {-2.50f, -0.10f, -0.95f};
  const Vector3 lw_tr_t = {-2.50f, -0.10f, -1.65f};
  const Vector3 lw_rr_t = {-0.28f, -0.05f, -2.10f};

  const Vector3 lw_rf_b = {-0.28f, -0.12f, 0.40f};
  const Vector3 lw_k_b  = {-1.30f, -0.16f, -0.10f};
  const Vector3 lw_tf_b = {-2.50f, -0.18f, -0.95f};
  const Vector3 lw_tr_b = {-2.50f, -0.18f, -1.65f};
  const Vector3 lw_rr_b = {-0.28f, -0.12f, -2.10f};

  // Top surface — fan from rf to other points (CCW from above for +Y)
  tris.push_back({lw_rf_t, lw_k_t, lw_tf_t, wingTop});
  tris.push_back({lw_rf_t, lw_tf_t, lw_tr_t, wingTop});
  tris.push_back({lw_rf_t, lw_tr_t, lw_rr_t, wingTop});
  // Bottom surface (reversed)
  tris.push_back({lw_rf_b, lw_tf_b, lw_k_b, wingBot});
  tris.push_back({lw_rf_b, lw_tr_b, lw_tf_b, wingBot});
  tris.push_back({lw_rf_b, lw_rr_b, lw_tr_b, wingBot});
  // Leading edge (rf → k → tf)
  tris.push_back({lw_rf_t, lw_rf_b, lw_k_b, wingEdge});
  tris.push_back({lw_rf_t, lw_k_b, lw_k_t, wingEdge});
  tris.push_back({lw_k_t, lw_k_b, lw_tf_b, wingEdge});
  tris.push_back({lw_k_t, lw_tf_b, lw_tf_t, wingEdge});
  // Wing tip edge (tf → tr)
  tris.push_back({lw_tf_t, lw_tf_b, lw_tr_b, wingEdge});
  tris.push_back({lw_tf_t, lw_tr_b, lw_tr_t, wingEdge});
  // Trailing edge (tr → rr)
  tris.push_back({lw_tr_t, lw_tr_b, lw_rr_b, wingEdge});
  tris.push_back({lw_tr_t, lw_rr_b, lw_rr_t, wingEdge});

  // RIGHT WING (mirrored)
  const Vector3 rw_rf_t = {0.28f, -0.05f, 0.40f};
  const Vector3 rw_k_t  = {1.30f, -0.08f, -0.10f};
  const Vector3 rw_tf_t = {2.50f, -0.10f, -0.95f};
  const Vector3 rw_tr_t = {2.50f, -0.10f, -1.65f};
  const Vector3 rw_rr_t = {0.28f, -0.05f, -2.10f};
  const Vector3 rw_rf_b = {0.28f, -0.12f, 0.40f};
  const Vector3 rw_k_b  = {1.30f, -0.16f, -0.10f};
  const Vector3 rw_tf_b = {2.50f, -0.18f, -0.95f};
  const Vector3 rw_tr_b = {2.50f, -0.18f, -1.65f};
  const Vector3 rw_rr_b = {0.28f, -0.12f, -2.10f};

  // Top surface (reverse winding for right side → +Y normal)
  tris.push_back({rw_rf_t, rw_tf_t, rw_k_t, wingTop});
  tris.push_back({rw_rf_t, rw_tr_t, rw_tf_t, wingTop});
  tris.push_back({rw_rf_t, rw_rr_t, rw_tr_t, wingTop});
  // Bottom surface
  tris.push_back({rw_rf_b, rw_k_b, rw_tf_b, wingBot});
  tris.push_back({rw_rf_b, rw_tf_b, rw_tr_b, wingBot});
  tris.push_back({rw_rf_b, rw_tr_b, rw_rr_b, wingBot});
  // Leading edge
  tris.push_back({rw_rf_t, rw_k_t, rw_k_b, wingEdge});
  tris.push_back({rw_rf_t, rw_k_b, rw_rf_b, wingEdge});
  tris.push_back({rw_k_t, rw_tf_t, rw_tf_b, wingEdge});
  tris.push_back({rw_k_t, rw_tf_b, rw_k_b, wingEdge});
  // Wing tip edge
  tris.push_back({rw_tf_t, rw_tr_t, rw_tr_b, wingEdge});
  tris.push_back({rw_tf_t, rw_tr_b, rw_tf_b, wingEdge});
  // Trailing edge
  tris.push_back({rw_tr_t, rw_rr_t, rw_rr_b, wingEdge});
  tris.push_back({rw_tr_t, rw_rr_b, rw_tr_b, wingEdge});

  // ================================================================
  // 3. Canards — small triangular forewings just behind the cockpit
  // ================================================================
  const Vector3 lc_rr = {-0.28f, 0.18f, 1.20f};
  const Vector3 lc_rf = {-0.28f, 0.18f, 1.70f};
  const Vector3 lc_t  = {-0.95f, 0.18f, 1.40f};
  tris.push_back({lc_rr, lc_rf, lc_t, canard});
  tris.push_back({lc_rr, lc_t, lc_rf, canard}); // both sides

  const Vector3 rc_rr = {0.28f, 0.18f, 1.20f};
  const Vector3 rc_rf = {0.28f, 0.18f, 1.70f};
  const Vector3 rc_t  = {0.95f, 0.18f, 1.40f};
  tris.push_back({rc_rr, rc_t, rc_rf, canard});
  tris.push_back({rc_rr, rc_rf, rc_t, canard});

  // ================================================================
  // 4. Cockpit canopy — small bubble forward of the canards
  // ================================================================
  const Vector3 ck_fl = {-0.20f, 0.18f, 2.30f};
  const Vector3 ck_fr = {0.20f, 0.18f, 2.30f};
  const Vector3 ck_rl = {-0.22f, 0.18f, 1.60f};
  const Vector3 ck_rr = {0.22f, 0.18f, 1.60f};
  const Vector3 ck_pk = {0.00f, 0.40f, 1.95f};
  tris.push_back({ck_fl, ck_fr, ck_pk, cockpitGlass});
  tris.push_back({ck_fr, ck_rr, ck_pk, cockpitGlass});
  tris.push_back({ck_rr, ck_rl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_fl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_rr, ck_fr, cockpitFrame});

  // ================================================================
  // 5. Red ID stripe down the spine, forward of the cockpit
  // ================================================================
  tris.push_back({{-0.08f, 0.19f, 3.10f}, {0.08f, 0.19f, 3.10f},
                  {0.06f, 0.19f, 2.40f}, stripe});
  tris.push_back({{-0.08f, 0.19f, 3.10f}, {0.06f, 0.19f, 2.40f},
                  {-0.06f, 0.19f, 2.40f}, stripe});

  // ================================================================
  // 6. Single engine nozzle on the back face of the fuselage
  // ================================================================
  tris.push_back({{-0.20f, -0.12f, -2.21f}, {-0.20f, 0.12f, -2.21f},
                  {0.20f, 0.12f, -2.21f}, nozzle});
  tris.push_back({{-0.20f, -0.12f, -2.21f}, {0.20f, 0.12f, -2.21f},
                  {0.20f, -0.12f, -2.21f}, nozzle});
}

// ================================================================
// YB-49 — Northrop flying wing. No fuselage, no tail. Just a wide
// swept-back triangular wing with a small cockpit bulge in the
// center and four engine nozzles across the trailing edge.
// ================================================================
static void buildYB49Mesh(std::vector<Tri> &tris) {
  // ---- Colour palette — silver bomber ----
  const Color wingTop = {175, 178, 185, 255};
  const Color wingBot = {85, 90, 100, 255};
  const Color wingEdge = {120, 125, 132, 255};
  const Color cockpitGlass = {65, 110, 160, 255};
  const Color cockpitBody = {145, 150, 158, 255};
  const Color finCol = {130, 135, 145, 255};
  const Color nozzle = {255, 145, 45, 255};

  // ================================================================
  // 1. Wing outline — wide swept-back triangle
  //    Apex at front, swept leading edges, straight trailing edge.
  // ================================================================
  const Vector3 nose_t = {0.00f, 0.05f, 2.80f};
  const Vector3 ltip_t = {-4.50f, -0.08f, -2.00f};
  const Vector3 rtip_t = {4.50f, -0.08f, -2.00f};
  const Vector3 ltrl_t = {-1.10f, -0.05f, -2.40f};  // left rear (inner)
  const Vector3 rtrl_t = {1.10f, -0.05f, -2.40f};   // right rear (inner)

  const Vector3 nose_b = {0.00f, -0.18f, 2.80f};
  const Vector3 ltip_b = {-4.50f, -0.22f, -2.00f};
  const Vector3 rtip_b = {4.50f, -0.22f, -2.00f};
  const Vector3 ltrl_b = {-1.10f, -0.20f, -2.40f};
  const Vector3 rtrl_b = {1.10f, -0.20f, -2.40f};

  // Top surface — fan from nose
  tris.push_back({nose_t, rtip_t, ltip_t, wingTop});
  tris.push_back({nose_t, rtrl_t, rtip_t, wingTop});
  tris.push_back({nose_t, ltip_t, ltrl_t, wingTop});
  tris.push_back({nose_t, ltrl_t, rtrl_t, wingTop});

  // Bottom surface (reversed winding)
  tris.push_back({nose_b, ltip_b, rtip_b, wingBot});
  tris.push_back({nose_b, rtip_b, rtrl_b, wingBot});
  tris.push_back({nose_b, ltrl_b, ltip_b, wingBot});
  tris.push_back({nose_b, rtrl_b, ltrl_b, wingBot});

  // Left leading edge (nose → ltip): visible thin strip
  tris.push_back({nose_t, ltip_t, ltip_b, wingEdge});
  tris.push_back({nose_t, ltip_b, nose_b, wingEdge});
  // Right leading edge
  tris.push_back({nose_t, nose_b, rtip_b, wingEdge});
  tris.push_back({nose_t, rtip_b, rtip_t, wingEdge});

  // Left wing-tip edge (ltip → ltrl)
  tris.push_back({ltip_t, ltrl_t, ltrl_b, wingEdge});
  tris.push_back({ltip_t, ltrl_b, ltip_b, wingEdge});
  // Right wing-tip edge
  tris.push_back({rtip_t, rtip_b, rtrl_b, wingEdge});
  tris.push_back({rtip_t, rtrl_b, rtrl_t, wingEdge});

  // Trailing edge (ltrl → rtrl)
  tris.push_back({ltrl_t, ltrl_b, rtrl_b, wingEdge});
  tris.push_back({ltrl_t, rtrl_b, rtrl_t, wingEdge});

  // ================================================================
  // 2. Center cockpit bulge — small dome on top center
  // ================================================================
  addBox(tris, -0.45f, 0.05f, 0.30f, 0.45f, 0.20f, 1.80f,
         cockpitBody, cockpitBody, cockpitBody);
  // Glass canopy peak above the body
  const Vector3 ck_fl = {-0.30f, 0.20f, 1.40f};
  const Vector3 ck_fr = {0.30f, 0.20f, 1.40f};
  const Vector3 ck_rl = {-0.35f, 0.20f, 0.50f};
  const Vector3 ck_rr = {0.35f, 0.20f, 0.50f};
  const Vector3 ck_pk = {0.00f, 0.40f, 0.95f};
  tris.push_back({ck_fl, ck_fr, ck_pk, cockpitGlass});
  tris.push_back({ck_fr, ck_rr, ck_pk, cockpitGlass});
  tris.push_back({ck_rr, ck_rl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_fl, ck_pk, cockpitGlass});

  // ================================================================
  // 3. Two small vertical fins on top of the wing (visual interest)
  // ================================================================
  // Left fin
  const Vector3 lf_bf = {-1.80f, 0.00f, -0.80f};
  const Vector3 lf_br = {-1.80f, 0.00f, -1.80f};
  const Vector3 lf_tf = {-1.80f, 0.40f, -1.10f};
  const Vector3 lf_tr = {-1.80f, 0.40f, -1.80f};
  tris.push_back({lf_bf, lf_tf, lf_tr, finCol});
  tris.push_back({lf_bf, lf_tr, lf_br, finCol});
  tris.push_back({lf_bf, lf_tr, lf_tf, finCol});
  tris.push_back({lf_bf, lf_br, lf_tr, finCol});
  // Right fin
  const Vector3 rf_bf = {1.80f, 0.00f, -0.80f};
  const Vector3 rf_br = {1.80f, 0.00f, -1.80f};
  const Vector3 rf_tf = {1.80f, 0.40f, -1.10f};
  const Vector3 rf_tr = {1.80f, 0.40f, -1.80f};
  tris.push_back({rf_bf, rf_tf, rf_tr, finCol});
  tris.push_back({rf_bf, rf_tr, rf_br, finCol});
  tris.push_back({rf_bf, rf_tr, rf_tf, finCol});
  tris.push_back({rf_bf, rf_br, rf_tr, finCol});

  // ================================================================
  // 4. Four engine nozzles arranged across the trailing edge
  //    Two pairs (jets are grouped on the YB-49)
  // ================================================================
  auto nozz = [&](float x0, float x1) {
    tris.push_back({{x0, -0.12f, -2.41f}, {x0, 0.05f, -2.41f},
                    {x1, 0.05f, -2.41f}, nozzle});
    tris.push_back({{x0, -0.12f, -2.41f}, {x1, 0.05f, -2.41f},
                    {x1, -0.12f, -2.41f}, nozzle});
  };
  // Left pair
  nozz(-1.00f, -0.65f);
  nozz(-0.55f, -0.20f);
  // Right pair
  nozz(0.20f, 0.55f);
  nozz(0.65f, 1.00f);
}

// ================================================================
// buildMesh — dispatches to the craft-specific builder and uploads
// ================================================================
void Player::buildMesh() {
  std::vector<Tri> tris;
  tris.reserve(256);

  switch (m_craftType) {
  case CraftType::DeltaWing:
    buildDeltaWingMesh(tris);
    break;
  case CraftType::ForwardSwept:
    buildForwardSweptMesh(tris);
    break;
  case CraftType::X36:
    buildX36Mesh(tris);
    break;
  case CraftType::YB49:
    buildYB49Mesh(tris);
    break;
  default:
    buildDeltaWingMesh(tris);
    break;
  }

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
// setCraft — swap to a different craft at runtime
// ================================================================
void Player::setCraft(CraftType craft) {
  if (m_craftType == craft && m_built)
    return;
  m_craftType = craft;
  if (m_built) {
    UnloadModel(m_model);
    m_built = false;
  }
  buildMesh();
}

// ================================================================
// init
// ================================================================
void Player::init(Vector3 startPos, int flightAssistLevel, CraftType craft) {
  m_craftType = craft;
  m_pos = startPos;
  m_vel = {0, 0, 0};
  m_yaw = 0.0f;
  m_pitch = 0.0f;
  m_roll = 0.0f;
  m_pitchVis = 0.0f;
  m_rollRate = 0.0f;
  m_pitchRate = 0.0f;
  m_smoothMouse = {0.0f, 0.0f};
  m_currentSpeed = Config::ARCADE_CRUISE_SPEED;
  m_targetSpeed = Config::ARCADE_CRUISE_SPEED;
  m_boosting = false;
  m_health = 100.0f;
  m_assistLevel = flightAssistLevel;

  buildMesh();
}

// ================================================================
// handleInput
// ================================================================
void Player::handleInput(float dt) {
  // ---- Lowpass-filter raw mouse delta to kill per-frame jitter ----
  Vector2 rawMouse = GetMouseDelta();
  float mouseSmooth = 1.0f - expf(-Config::ARCADE_MOUSE_SMOOTH * dt);
  m_smoothMouse.x += (rawMouse.x - m_smoothMouse.x) * mouseSmooth;
  m_smoothMouse.y += (rawMouse.y - m_smoothMouse.y) * mouseSmooth;

  // ---- Throttle: W = speed up, S = slow down ----
  if (IsKeyDown(KEY_W))
    m_targetSpeed += Config::ARCADE_THROTTLE_RATE * dt;
  if (IsKeyDown(KEY_S))
    m_targetSpeed -= Config::ARCADE_THROTTLE_RATE * dt;

  // ---- Boost: hold Shift ----
  m_boosting = IsKeyDown(KEY_LEFT_SHIFT);
  if (m_boosting)
    m_targetSpeed = Config::ARCADE_BOOST_SPEED;

  float topSpeed =
      m_boosting ? Config::ARCADE_BOOST_SPEED : Config::ARCADE_MAX_SPEED;
  if (m_targetSpeed > topSpeed)
    m_targetSpeed = topSpeed;
  if (m_targetSpeed < Config::ARCADE_MIN_SPEED)
    m_targetSpeed = Config::ARCADE_MIN_SPEED;

  m_thrusting = (m_targetSpeed > Config::ARCADE_CRUISE_SPEED) || m_boosting;

  // ---- Roll input: A/D + smoothed mouse X ----
  // Combine into a normalized control input ([-1, 1]).
  float rollInput = 0.0f;
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
    rollInput -= 1.0f;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
    rollInput += 1.0f;
  rollInput += m_smoothMouse.x * Config::ARCADE_MOUSE_ROLL_SENS / dt;
  if (rollInput > 1.0f)
    rollInput = 1.0f;
  if (rollInput < -1.0f)
    rollInput = -1.0f;
  m_turnInput = rollInput;

  // ---- Roll: P-controller toward target bank, with rate smoothing ----
  // Target bank angle is proportional to control input. The roll rate
  // is set to chase that target with a P-gain, then smoothed so it
  // never snaps. When input returns to zero, target = 0 → auto-levels.
  float targetBank = rollInput * Config::ARCADE_BANK_MAX;
  float targetRollRate =
      (targetBank - m_roll) * Config::ARCADE_BANK_RESPONSE;
  if (targetRollRate > Config::ARCADE_ROLL_RATE)
    targetRollRate = Config::ARCADE_ROLL_RATE;
  if (targetRollRate < -Config::ARCADE_ROLL_RATE)
    targetRollRate = -Config::ARCADE_ROLL_RATE;

  float rateSmooth = 1.0f - expf(-Config::ARCADE_RATE_SMOOTH * dt);
  m_rollRate += (targetRollRate - m_rollRate) * rateSmooth;
  m_roll += m_rollRate * dt;

  if (m_roll > Config::ARCADE_BANK_MAX)
    m_roll = Config::ARCADE_BANK_MAX;
  if (m_roll < -Config::ARCADE_BANK_MAX)
    m_roll = -Config::ARCADE_BANK_MAX;

  // ---- Pitch input: Up/Down arrows + smoothed mouse Y ----
  // Mouse up (delta.y < 0) = nose up = positive pitch input.
  float pitchInput = 0.0f;
  if (IsKeyDown(KEY_UP))
    pitchInput += 1.0f;
  if (IsKeyDown(KEY_DOWN))
    pitchInput -= 1.0f;
  pitchInput -= m_smoothMouse.y * Config::ARCADE_MOUSE_PITCH_SENS / dt;
  if (pitchInput > 1.0f)
    pitchInput = 1.0f;
  if (pitchInput < -1.0f)
    pitchInput = -1.0f;

  // ---- Pitch: rate-based with smoothing (no auto-level — Ace Combat) ----
  // Input drives a target rate; current rate eases toward it; integrate.
  // When input returns to zero, rate decays to 0 and pitch HOLDS.
  float targetPitchRate = pitchInput * Config::ARCADE_PITCH_RATE;
  m_pitchRate += (targetPitchRate - m_pitchRate) * rateSmooth;
  m_pitch += m_pitchRate * dt;

  if (m_pitch > Config::ARCADE_PITCH_MAX) {
    m_pitch = Config::ARCADE_PITCH_MAX;
    if (m_pitchRate > 0.0f) m_pitchRate = 0.0f;
  }
  if (m_pitch < -Config::ARCADE_PITCH_MAX) {
    m_pitch = -Config::ARCADE_PITCH_MAX;
    if (m_pitchRate < 0.0f) m_pitchRate = 0.0f;
  }

  // Yaw is derived from bank angle (bank-to-turn). Positive roll is
  // a LEFT bank in our convention, so it must produce a LEFT turn.
  float yawRate = -m_roll * Config::ARCADE_TURN_COEFF;
  m_yaw += yawRate * dt;
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
  // ---- Speed: lerp current toward target, then apply energy trade ----
  m_currentSpeed +=
      (m_targetSpeed - m_currentSpeed) * Config::ARCADE_ACCEL_K * dt;

  // Energy trade: climbing bleeds speed, diving gains it (40% of real)
  m_currentSpeed += -Config::ARCADE_GRAVITY * sinf(m_pitch) *
                    Config::ARCADE_ENERGY_TRADE * dt;

  // Hard floor / ceiling on speed
  if (m_currentSpeed < Config::ARCADE_MIN_SPEED)
    m_currentSpeed = Config::ARCADE_MIN_SPEED;
  if (m_currentSpeed > Config::ARCADE_BOOST_SPEED)
    m_currentSpeed = Config::ARCADE_BOOST_SPEED;

  // ---- Velocity hard-lock to forward vector (the Ace Combat trick) ----
  Vector3 fwd = forward();
  m_vel = Vector3Scale(fwd, m_currentSpeed);

  // ---- Position integration ----
  m_pos = Vector3Add(m_pos, Vector3Scale(m_vel, dt));

  // ---- Speed-aware terrain pullup ----
  // The danger zone scales with speed: faster ships need more altitude
  // to recover from a dive. Inside the zone, pitch is smoothly pulled
  // up with strength proportional to depth.
  float groundH = planet.heightAt(m_pos.x, m_pos.z);
  float currentAGL = m_pos.y - groundH;
  float pullupAGL = Config::ARCADE_PULLUP_BASE +
                    m_currentSpeed * Config::ARCADE_PULLUP_SPEED_FACTOR;

  if (currentAGL < pullupAGL && m_pitch < 0.0f) {
    float dangerSpan = pullupAGL - Config::PLAYER_MIN_ALTITUDE;
    if (dangerSpan < 0.1f) dangerSpan = 0.1f;
    float depth = (pullupAGL - currentAGL) / dangerSpan;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;

    // Recovery rate ramps from zero at the danger edge to max at the floor
    float pull = Config::ARCADE_PULLUP_STRENGTH * depth * dt;
    m_pitch += -m_pitch * pull;
    m_pitchRate += -m_pitchRate * pull;
  }

  // Hard floor clamp (safety net — should rarely fire if pullup works)
  float minH = groundH + Config::PLAYER_MIN_ALTITUDE;
  if (m_pos.y < minH) {
    m_pos.y = minH;
    if (m_pitch < 0.0f) {
      m_pitch = 0.0f;
      m_pitchRate = 0.0f;
    }
  }

  // Hard ceiling
  if (m_pos.y > Config::PLAYER_MAX_ALTITUDE + groundH)
    m_pos.y = Config::PLAYER_MAX_ALTITUDE + groundH;

  // ---- Visual pitch tracks real flight pitch ----
  // raylib row-vector convention: negate to get nose-up for positive pitch.
  m_pitchVis = -m_pitch;
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
  // Order: roll → pitch → yaw → translate. Roll must come BEFORE pitch
  // so that banking only tilts the wings around the unrotated forward
  // axis, instead of cross-coupling into pitch when the nose is tilted.
  Matrix worldMat = MatrixMultiply(
      MatrixMultiply(
          MatrixMultiply(MatrixRotateZ(m_roll), MatrixRotateX(m_pitchVis)),
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