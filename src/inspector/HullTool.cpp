#include "HullTool.hpp"

#include "Inspector.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"
#include "rlgl.h"

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

constexpr int kNumFields = 5;

float &fieldAt(ProfileView &v, int idx) {
  switch (idx) {
  case 0: return v.hullHP;
  case 1: return v.hullCollisionRadius;
  case 2: return v.hullMass;
  case 3: return v.hullWreckageMetal;
  case 4: return v.hullWreckageBio;
  }
  static float sink = 0.0f;
  return sink;
}

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "hp             ";
  case 1: return "collisionRadius";
  case 2: return "mass           ";
  case 3: return "wreckage.metal ";
  case 4: return "wreckage.bio   ";
  }
  return "?";
}

// Per-field nudge step. HP / wreckage are integer-ish; radius/mass
// are sub-unit, so they need finer fine-step.
void stepValues(int idx, float &fine, float &coarse) {
  switch (idx) {
  case 0: fine = 5.0f;  coarse = 50.0f; return; // hp
  case 1: fine = 0.05f; coarse = 0.5f;  return; // radius
  case 2: fine = 0.05f; coarse = 1.0f;  return; // mass
  case 3:
  case 4: fine = 1.0f;  coarse = 10.0f; return; // wreckage
  }
  fine = 0.05f; coarse = 1.0f;
}

} // anonymous namespace

void HullTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;

  // `N` adds a hull block if there isn't one (lets the tool work on
  // sidecars that don't yet have a hull section).
  if (IsKeyPressed(KEY_N) && !v.hullPresent) {
    v.hullPresent = true;
    m_dirty = true;
  }
  if (!v.hullPresent) return;

  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
  if (up || down) {
    float fine, coarse;
    stepValues(m_focus, fine, coarse);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    float step = shift ? coarse : fine;
    float &f = fieldAt(v, m_focus);
    f += (up ? step : -step);
    if (f < 0.0f) f = 0.0f; // hull values clamp ≥ 0
    m_dirty = true;
  }
}

void HullTool::render3D(const Inspector &insp) const {
  const ProfileView &v = insp.profile().view;
  if (!v.hullPresent) return;
  if (v.hullCollisionRadius <= 0.0f) return;

  // Translucent yellow wireframe sphere at the pivot. raylib's
  // DrawSphereWires takes ring/slice counts — keep coarse (8/10) so
  // the look matches the flat-shaded retro aesthetic.
  Vector3 c = v.pivot;
  float r = v.hullCollisionRadius;
  DrawSphereWires(c, r, 8, 10, {255, 220, 80, 200});
}

void HullTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  if (!v.hullPresent) {
    DrawText("hull (no hull section in sidecar  |  press N to add)",
             10, yCursor, 14, {200, 180, 140, 240});
    yCursor += 22;
    return;
  }

  DrawText("hull (./, to cycle  |  ↑/↓ fine  |  Shift+↑/↓ coarse)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  char buf[128];
  for (int i = 0; i < kNumFields; ++i) {
    bool focused = (i == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(i), fieldAt(
                      const_cast<ProfileView &>(v), i));
    DrawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  }
}

bool HullTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[HullTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void HullTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
