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

const char *flightModeName(FlightMode m) {
  switch (m) {
  case FlightMode::Classic: return "Classic (Newtonian)";
  case FlightMode::Arcade:  return "Arcade (Bank-to-turn)";
  default:                  return "Unknown";
  }
}

// Friendly name for HUD / menu
const char *craftName(CraftType c) {
  switch (c) {
  case CraftType::DeltaWing:    return "Delta Wing";
  case CraftType::ForwardSwept: return "Forward Swept";
  case CraftType::X36:          return "X-36";
  case CraftType::YB49:         return "YB-49 Flying Wing";
  case CraftType::Saucer:       return "Saucer";
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
// YB-49 — Northrop flying wing bomber. Pure flying wing with no
// fuselage and no tail. Tapered hexagonal outline (root chord >
// tip chord), small central fuselage bump for the cockpit, four
// dorsal fins on top of the wing, and two pairs of jet exhausts.
// Proportions match the real aircraft: span ~3x the root chord.
// ================================================================
static void buildYB49Mesh(std::vector<Tri> &tris) {
  // ---- Colour palette — bare aluminium bomber ----
  const Color wingTop = {180, 184, 192, 255};
  const Color wingBot = {90, 95, 105, 255};
  const Color wingEdge = {130, 135, 142, 255};
  const Color cockpitGlass = {65, 110, 160, 255};
  const Color cockpitBody = {150, 155, 165, 255};
  const Color finCol = {120, 125, 132, 255};
  const Color nozzle = {255, 145, 45, 255};

  // ================================================================
  // 1. Wing outline — tapered hexagonal flying wing
  //    Span = ±4.5  (9 wide)
  //    Root chord = 2.8  (front +1.40 to rear -1.40)
  //    Tip chord  = 0.9  (front +0.45 to rear -0.45)
  //    Both leading AND trailing edges sweep back toward the tips.
  // ================================================================
  const float TY = 0.05f;  // top surface Y
  const float BY = -0.10f; // bottom surface Y

  // Top surface vertices (6 perimeter points)
  const Vector3 cf_t = {0.00f, TY, 1.40f};   // center front
  const Vector3 rfo_t = {4.50f, TY, 0.45f};  // right tip front (outer)
  const Vector3 rro_t = {4.50f, TY, -0.45f}; // right tip rear (outer)
  const Vector3 cr_t = {0.00f, TY, -1.40f};  // center rear
  const Vector3 lro_t = {-4.50f, TY, -0.45f};
  const Vector3 lfo_t = {-4.50f, TY, 0.45f};

  // Bottom surface (slightly below)
  const Vector3 cf_b = {0.00f, BY, 1.40f};
  const Vector3 rfo_b = {4.50f, BY, 0.45f};
  const Vector3 rro_b = {4.50f, BY, -0.45f};
  const Vector3 cr_b = {0.00f, BY, -1.40f};
  const Vector3 lro_b = {-4.50f, BY, -0.45f};
  const Vector3 lfo_b = {-4.50f, BY, 0.45f};

  // Top surface — triangle fan from center front (CCW from above → +Y)
  tris.push_back({cf_t, rfo_t, rro_t, wingTop});
  tris.push_back({cf_t, rro_t, cr_t, wingTop});
  tris.push_back({cf_t, cr_t, lro_t, wingTop});
  tris.push_back({cf_t, lro_t, lfo_t, wingTop});

  // Bottom surface — reversed winding (-Y normal)
  tris.push_back({cf_b, rro_b, rfo_b, wingBot});
  tris.push_back({cf_b, cr_b, rro_b, wingBot});
  tris.push_back({cf_b, lro_b, cr_b, wingBot});
  tris.push_back({cf_b, lfo_b, lro_b, wingBot});

  // Edge strips around the perimeter (each quad → 2 tris)
  auto edge = [&](const Vector3 &t1, const Vector3 &b1,
                  const Vector3 &t2, const Vector3 &b2) {
    tris.push_back({t1, b1, b2, wingEdge});
    tris.push_back({t1, b2, t2, wingEdge});
  };
  // Right leading edge (cf → rfo)
  edge(cf_t, cf_b, rfo_t, rfo_b);
  // Right wing tip (rfo → rro)
  edge(rfo_t, rfo_b, rro_t, rro_b);
  // Right trailing edge (rro → cr)
  edge(rro_t, rro_b, cr_t, cr_b);
  // Left trailing edge (cr → lro)
  edge(cr_t, cr_b, lro_t, lro_b);
  // Left wing tip (lro → lfo)
  edge(lro_t, lro_b, lfo_t, lfo_b);
  // Left leading edge (lfo → cf)
  edge(lfo_t, lfo_b, cf_t, cf_b);

  // ================================================================
  // 2. Central fuselage bump — small streamlined body running
  //    front-to-back along the wing centerline. Houses cockpit
  //    forward and gear bay rearward.
  // ================================================================
  addBox(tris, -0.35f, TY, -1.20f, 0.35f, 0.22f, 1.30f,
         cockpitBody, cockpitBody, cockpitBody);

  // Cockpit canopy bulge above the front of the fuselage
  const Vector3 ck_fl = {-0.22f, 0.22f, 1.10f};
  const Vector3 ck_fr = {0.22f, 0.22f, 1.10f};
  const Vector3 ck_rl = {-0.26f, 0.22f, 0.30f};
  const Vector3 ck_rr = {0.26f, 0.22f, 0.30f};
  const Vector3 ck_pk = {0.00f, 0.42f, 0.70f};
  tris.push_back({ck_fl, ck_fr, ck_pk, cockpitGlass});
  tris.push_back({ck_fr, ck_rr, ck_pk, cockpitGlass});
  tris.push_back({ck_rr, ck_rl, ck_pk, cockpitGlass});
  tris.push_back({ck_rl, ck_fl, ck_pk, cockpitGlass});

  // ================================================================
  // 3. Four dorsal fins — small vertical stabilizers on top of the
  //    wing, paired inboard and outboard on each side.
  // ================================================================
  auto fin = [&](float x, float zFront, float zRear, float height) {
    Vector3 bf = {x, TY, zFront};
    Vector3 br = {x, TY, zRear};
    Vector3 tf = {x, TY + height, zFront + 0.10f};
    Vector3 tr = {x, TY + height, zRear};
    tris.push_back({bf, tf, tr, finCol});
    tris.push_back({bf, tr, br, finCol});
    tris.push_back({bf, tr, tf, finCol});
    tris.push_back({bf, br, tr, finCol});
  };
  // Inboard pair
  fin(-1.20f, -0.20f, -1.10f, 0.35f);
  fin(1.20f, -0.20f, -1.10f, 0.35f);
  // Outboard pair
  fin(-2.40f, -0.40f, -1.10f, 0.30f);
  fin(2.40f, -0.40f, -1.10f, 0.30f);

  // ================================================================
  // 4. Engine exhausts — two paired clusters near the trailing edge
  //    matching the YB-49's grouped jet layout. Drawn as orange
  //    rectangles on the rear face of small engine humps.
  // ================================================================
  auto engineHump = [&](float x0, float x1) {
    addBox(tris, x0, BY + 0.02f, -1.30f, x1, TY + 0.06f, -0.90f,
           cockpitBody, cockpitBody, cockpitBody);
    // Nozzle on the rear face
    tris.push_back({{x0 + 0.04f, BY + 0.04f, -1.31f},
                    {x0 + 0.04f, TY + 0.04f, -1.31f},
                    {x1 - 0.04f, TY + 0.04f, -1.31f}, nozzle});
    tris.push_back({{x0 + 0.04f, BY + 0.04f, -1.31f},
                    {x1 - 0.04f, TY + 0.04f, -1.31f},
                    {x1 - 0.04f, BY + 0.04f, -1.31f}, nozzle});
  };
  // Left pair
  engineHump(-2.10f, -1.55f);
  engineHump(-1.45f, -0.90f);
  // Right pair
  engineHump(0.90f, 1.45f);
  engineHump(1.55f, 2.10f);
}

// ================================================================
// Saucer — simple flying saucer for Classic/Newtonian mode.
// Flat disc underside, domed top with central cockpit, ring of
// glowing thrusters around the rim underneath. Intentionally
// symmetric since Classic thrust is along LOCAL UP — heading yaw
// is all that distinguishes "forward" for the player.
// ================================================================
static void buildSaucerMesh(std::vector<Tri> &tris) {
  // ---- Colour palette ----
  const Color hullTop = {155, 160, 168, 255};  // brushed steel top
  const Color hullMid = {115, 120, 128, 255};  // mid band
  const Color hullBot = {70, 75, 82, 255};     // dark underside
  const Color rim = {90, 95, 100, 255};        // edge rim
  const Color domeGlass = {60, 130, 180, 255}; // cockpit glass
  const Color thruster = {255, 160, 40, 255};  // glow

  // ---- Saucer disc — 16-sided regular polygon for a clean circle ----
  constexpr int SEGS = 16;
  const float outerR = 2.40f;           // rim radius
  const float midR   = 1.80f;           // where the hull meets the dome
  const float domeR  = 0.80f;           // base of cockpit dome
  const float rimY_top = 0.10f;         // top of the rim edge
  const float rimY_bot = -0.10f;        // bottom of the rim edge
  const float midY_top = 0.30f;         // where the hull slopes start from
  const float domeY_base = 0.35f;       // base of dome (on hull)
  const float domeY_peak = 0.95f;       // dome apex
  const float domeY_ring = 0.65f;       // faceted ring on the dome

  // Precompute vertices around each radial ring
  Vector3 rimTop[SEGS], rimBot[SEGS];
  Vector3 midRing[SEGS];
  Vector3 domeRing[SEGS];
  Vector3 domeMid[SEGS];
  for (int i = 0; i < SEGS; ++i) {
    float a = 2.0f * 3.14159265f * (float)i / (float)SEGS;
    float cx = cosf(a), cz = sinf(a);
    rimTop[i]  = {outerR * cx, rimY_top, outerR * cz};
    rimBot[i]  = {outerR * cx, rimY_bot, outerR * cz};
    midRing[i] = {midR * cx,   midY_top, midR * cz};
    domeRing[i] = {domeR * cx, domeY_base, domeR * cz};
    domeMid[i]  = {domeR * 0.6f * cx, domeY_ring, domeR * 0.6f * cz};
  }
  Vector3 domePeak = {0.0f, domeY_peak, 0.0f};
  Vector3 belly   = {0.0f, -0.25f, 0.0f}; // underside central point

  // ---- Top surface: outer ring (rim_top → mid_ring) ----
  for (int i = 0; i < SEGS; ++i) {
    int j = (i + 1) % SEGS;
    tris.push_back({rimTop[i], rimTop[j], midRing[j], hullMid});
    tris.push_back({rimTop[i], midRing[j], midRing[i], hullMid});
  }
  // ---- Hull top slope: mid_ring → dome_ring ----
  for (int i = 0; i < SEGS; ++i) {
    int j = (i + 1) % SEGS;
    tris.push_back({midRing[i], midRing[j], domeRing[j], hullTop});
    tris.push_back({midRing[i], domeRing[j], domeRing[i], hullTop});
  }
  // ---- Dome faceted bubble: dome_ring → dome_mid → dome_peak ----
  for (int i = 0; i < SEGS; ++i) {
    int j = (i + 1) % SEGS;
    tris.push_back({domeRing[i], domeRing[j], domeMid[j], domeGlass});
    tris.push_back({domeRing[i], domeMid[j], domeMid[i], domeGlass});
    tris.push_back({domeMid[i], domeMid[j], domePeak, domeGlass});
  }
  // ---- Rim edge (outer vertical band) ----
  for (int i = 0; i < SEGS; ++i) {
    int j = (i + 1) % SEGS;
    tris.push_back({rimTop[i], rimBot[i], rimBot[j], rim});
    tris.push_back({rimTop[i], rimBot[j], rimTop[j], rim});
  }
  // ---- Underside: outer ring to central belly point ----
  for (int i = 0; i < SEGS; ++i) {
    int j = (i + 1) % SEGS;
    tris.push_back({rimBot[i], belly, rimBot[j], hullBot});
  }

  // ---- Thruster ring — 8 glowing squares under the rim ----
  constexpr int THR = 8;
  const float thrusterR = 1.80f;
  for (int i = 0; i < THR; ++i) {
    float a = 2.0f * 3.14159265f * (float)i / (float)THR;
    float cx = cosf(a), cz = sinf(a);
    float px = thrusterR * cx;
    float pz = thrusterR * cz;
    // A small horizontal quad facing down
    float s = 0.18f;
    Vector3 p1 = {px - s * cz, -0.09f, pz + s * cx};
    Vector3 p2 = {px + s * cz, -0.09f, pz - s * cx};
    Vector3 p3 = {px + s * cz - 0.25f * cx, -0.09f, pz - s * cx - 0.25f * cz};
    Vector3 p4 = {px - s * cz - 0.25f * cx, -0.09f, pz + s * cx - 0.25f * cz};
    tris.push_back({p1, p3, p2, thruster});
    tris.push_back({p1, p4, p3, thruster});
  }
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
  case CraftType::Saucer:
    buildSaucerMesh(tris);
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
// setFlightMode — swap physics model at runtime. Auto-assigns a
// sensible craft for each mode when called: Saucer for Classic,
// Delta Wing for Arcade. Existing flight state is reset so the
// ship doesn't inherit arcade/classic-specific invariants.
// ================================================================
void Player::setFlightMode(FlightMode mode) {
  if (m_flightMode == mode) return;
  m_flightMode = mode;

  // Reset transient state — otherwise arcade's targetSpeed or
  // classic's momentum carry across the transition in weird ways.
  m_vel = {0, 0, 0};
  m_pitch = 0.0f;
  m_roll = 0.0f;
  m_pitchVis = 0.0f;
  m_rollRate = 0.0f;
  m_pitchRate = 0.0f;
  m_currentSpeed = (mode == FlightMode::Arcade)
                   ? Config::ARCADE_CRUISE_SPEED : 0.0f;
  m_targetSpeed = m_currentSpeed;

  // Auto-assign the canonical craft for each mode
  if (mode == FlightMode::Classic)
    setCraft(CraftType::Saucer);
  else
    setCraft(CraftType::DeltaWing);
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
// handleArcadeInput — Air Combat 22 style (bank-to-turn, smoothed)
// ================================================================
void Player::handleArcadeInput(float dt) {
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
// applyArcadePhysics — Air Combat 22 style
// ================================================================
void Player::applyArcadePhysics(float dt, const Planet &planet) {
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
// handleClassicInput — Newtonian Virus-style attitude control.
//   A/D       = roll (bank)
//   Mouse X   = roll (additive)
//   W/S / Up/Down = pitch (nose up/down)
//   Mouse Y   = pitch (additive)
//   Q/E       = yaw (direct heading change)
//   Shift     = thrust (full)
//   Space / no key = idle (zero thrust, gravity pulls down)
//   No auto-level — pilot holds attitude manually.
// ================================================================
void Player::handleClassicInput(float dt) {
  // Smoothed mouse delta (reuse arcade's lowpass)
  Vector2 rawMouse = GetMouseDelta();
  float mouseSmooth = 1.0f - expf(-Config::CLASSIC_INPUT_SMOOTH * dt);
  m_smoothMouse.x += (rawMouse.x - m_smoothMouse.x) * mouseSmooth;
  m_smoothMouse.y += (rawMouse.y - m_smoothMouse.y) * mouseSmooth;

  // ---- Thrust: Shift = full, else idle ----
  m_boosting = false;
  m_thrusting = IsKeyDown(KEY_LEFT_SHIFT);
  // Classic doesn't use target/current speed lerp — thrust is binary
  m_targetSpeed = m_thrusting ? Config::CLASSIC_THRUST : Config::CLASSIC_THRUST_IDLE;

  // ---- Roll: A/D + mouse X ----
  float rollInput = 0.0f;
  if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
    rollInput -= 1.0f;
  if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
    rollInput += 1.0f;
  rollInput += m_smoothMouse.x * Config::CLASSIC_MOUSE_ROLL_SENS / dt;
  if (rollInput > 1.0f) rollInput = 1.0f;
  if (rollInput < -1.0f) rollInput = -1.0f;
  m_turnInput = rollInput;

  // Integrate roll directly — no auto-level in Classic
  m_roll += rollInput * Config::CLASSIC_ROLL_RATE * dt;
  // Wrap to ±π so we can do full rolls
  while (m_roll > 3.14159f)  m_roll -= 6.28318f;
  while (m_roll < -3.14159f) m_roll += 6.28318f;

  // ---- Pitch: W/S or Up/Down + mouse Y ----
  // Classic/Virus convention: push stick FORWARD = craft tilts nose
  // DOWN = thrust redirects FORWARD = fly forward. So W / mouse-up
  // decrements m_pitch (render shows nose going down).
  float pitchInput = 0.0f;
  if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
    pitchInput -= 1.0f;
  if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
    pitchInput += 1.0f;
  pitchInput += m_smoothMouse.y * Config::CLASSIC_MOUSE_PITCH_SENS / dt;
  if (pitchInput > 1.0f) pitchInput = 1.0f;
  if (pitchInput < -1.0f) pitchInput = -1.0f;

  m_pitch += pitchInput * Config::CLASSIC_PITCH_RATE * dt;
  if (m_pitch > Config::CLASSIC_PITCH_MAX)
    m_pitch = Config::CLASSIC_PITCH_MAX;
  if (m_pitch < -Config::CLASSIC_PITCH_MAX)
    m_pitch = -Config::CLASSIC_PITCH_MAX;

  // ---- Yaw: Q/E for direct heading change (saucer-style) ----
  // Q = turn left (yaw decreases), E = turn right (yaw increases) from
  // the pilot's perspective with the camera behind the ship.
  float yawInput = 0.0f;
  if (IsKeyDown(KEY_Q)) yawInput += 1.0f;
  if (IsKeyDown(KEY_E)) yawInput -= 1.0f;
  m_yaw += yawInput * Config::CLASSIC_YAW_RATE * dt;
}

// ================================================================
// applyClassicPhysics — Newtonian thrust-vs-gravity.
//   velocity += (thrust_along_local_up + gravity_world_down) * dt
//   position += velocity * dt
//   thrust direction = ship's rotated UP axis (tilt redirects lift)
// ================================================================
void Player::applyClassicPhysics(float dt, const Planet &planet) {
  // Compute the ship's LOCAL UP axis in world space, matching the
  // render matrix (which applies Rx with m_pitchVis = -m_pitch).
  // Rotation sequence as in the render: Rz(roll) → Rx(-m_pitch) → Ry(yaw).
  float cr = cosf(m_roll),  sr = sinf(m_roll);
  float cp = cosf(m_pitch), sp = sinf(m_pitch);
  float cy = cosf(m_yaw),   sy = sinf(m_yaw);

  // Local up = (0,1,0). After Rz(roll): (-sr, cr, 0)
  float ux = -sr;
  float uy = cr;
  float uz = 0.0f;
  // After Rx(-m_pitch): x unchanged,
  //   y' = y*cos(-mp) - z*sin(-mp) = y*cp + z*sp
  //   z' = y*sin(-mp) + z*cos(-mp) = -y*sp + z*cp
  float uy2 = uy * cp + uz * sp;
  float uz2 = -uy * sp + uz * cp;
  uy = uy2;
  uz = uz2;
  // After Ry(yaw)
  float ux2 = ux * cy + uz * sy;
  float uz3 = -ux * sy + uz * cy;
  ux = ux2;
  uz = uz3;
  Vector3 localUp = {ux, uy, uz};

  // ---- Forces ----
  float thrustAccel = m_thrusting ? Config::CLASSIC_THRUST : 0.0f;
  Vector3 thrustVec = Vector3Scale(localUp, thrustAccel);
  Vector3 gravityVec = {0.0f, -Config::CLASSIC_GRAVITY, 0.0f};
  Vector3 accel = Vector3Add(thrustVec, gravityVec);

  // Integrate velocity
  m_vel = Vector3Add(m_vel, Vector3Scale(accel, dt));

  // Near-zero drag (per-tick exponential decay)
  float dragFactor = 1.0f - Config::CLASSIC_DRAG * dt;
  if (dragFactor < 0.0f) dragFactor = 0.0f;
  m_vel = Vector3Scale(m_vel, dragFactor);

  // Hard speed clamp (safety)
  float speed = Vector3Length(m_vel);
  if (speed > Config::CLASSIC_MAX_SPEED)
    m_vel = Vector3Scale(m_vel, Config::CLASSIC_MAX_SPEED / speed);

  m_currentSpeed = speed;

  // Integrate position
  m_pos = Vector3Add(m_pos, Vector3Scale(m_vel, dt));

  // ---- Ground interaction ----
  float groundH = planet.heightAt(m_pos.x, m_pos.z);
  float minH = groundH + Config::PLAYER_MIN_ALTITUDE;
  if (m_pos.y < minH) {
    // Crash vs bounce check
    if (m_vel.y < -Config::CLASSIC_GROUND_IMPACT) {
      // Hard crash — drain health
      applyDamage(100.0f * (fabsf(m_vel.y) / Config::CLASSIC_GROUND_IMPACT));
    }
    // Snap to minimum altitude and bounce/absorb vertical velocity
    m_pos.y = minH;
    if (m_vel.y < 0.0f)
      m_vel.y = -m_vel.y * Config::CLASSIC_GROUND_BOUNCE;
  }

  // Ceiling
  if (m_pos.y > Config::PLAYER_MAX_ALTITUDE + groundH) {
    m_pos.y = Config::PLAYER_MAX_ALTITUDE + groundH;
    if (m_vel.y > 0.0f) m_vel.y = 0.0f;
  }

  // Visual pitch follows real pitch (row-vector convention)
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
// Input/physics dispatchers — route to the active FlightMode path
// ================================================================
void Player::handleInput(float dt) {
  if (m_flightMode == FlightMode::Classic)
    handleClassicInput(dt);
  else
    handleArcadeInput(dt);
}

void Player::applyPhysics(float dt, const Planet &planet) {
  if (m_flightMode == FlightMode::Classic)
    applyClassicPhysics(dt, planet);
  else
    applyArcadePhysics(dt, planet);
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