#include "ShieldsTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

constexpr const char *kModels[] = {"omni", "4-sector", "per-face"};
constexpr int kNumModels =
    static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));
constexpr int kNumFields = 4; // model + hp + regen + delay

int modelIndexOf(const std::string &s) {
  for (int i = 0; i < kNumModels; ++i)
    if (s == kModels[i]) return i;
  return 0; // default to omni if unrecognised
}

float *numericFieldAt(ProfileView &v, int idx) {
  switch (idx) {
  case 1: return &v.shieldHP;
  case 2: return &v.shieldRegen;
  case 3: return &v.shieldDelay;
  }
  return nullptr;
}

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "model";
  case 1: return "hp   ";
  case 2: return "regen";
  case 3: return "delay";
  }
  return "?";
}

void stepValues(int idx, float &fine, float &coarse) {
  switch (idx) {
  case 1: fine = 5.0f;  coarse = 50.0f; return; // hp
  case 2: fine = 1.0f;  coarse = 10.0f; return; // regen
  case 3: fine = 0.1f;  coarse = 1.0f;  return; // delay
  }
  fine = 0.05f; coarse = 1.0f;
}

// Choose a viz radius — collisionRadius if hull is present, else a
// safe default tied to the bounds (so the bubble matches the mesh).
float vizRadius(const Inspector &insp) {
  const auto &v = insp.profile().view;
  if (v.hullPresent && v.hullCollisionRadius > 0.0f)
    return v.hullCollisionRadius * 1.45f;
  return insp.boundsRadius() * 1.2f;
}

} // anonymous namespace

void ShieldsTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;

  if (IsKeyPressed(KEY_N) && !v.shieldsPresent) {
    v.shieldsPresent = true;
    if (v.shieldModel.empty()) v.shieldModel = "omni";
    m_dirty = true;
  }
  if (!v.shieldsPresent) return;

  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
  if (up || down) {
    if (m_focus == 0) {
      // Cycle the model enum.
      int cur = modelIndexOf(v.shieldModel);
      cur = (cur + (up ? 1 : kNumModels - 1)) % kNumModels;
      v.shieldModel = kModels[cur];
      m_dirty = true;
    } else if (float *f = numericFieldAt(v, m_focus)) {
      float fine, coarse;
      stepValues(m_focus, fine, coarse);
      bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
      *f += (up ? (shift ? coarse : fine) : -(shift ? coarse : fine));
      if (*f < 0.0f) *f = 0.0f;
      m_dirty = true;
    }
  }
}

void ShieldsTool::render3D(const Inspector &insp) const {
  const ProfileView &v = insp.profile().view;
  if (!v.shieldsPresent) return;

  float r = vizRadius(insp);
  if (r <= 0.0f) return;
  Vector3 c = v.pivot;
  Color col = {80, 160, 240, 200};

  if (v.shieldModel == "4-sector") {
    // Four pies, separated visually by a vertical great-circle and
    // a horizontal great-circle. We draw the equator + the two
    // meridians that quarter the sphere, plus a faint translucent
    // sphere outline so the user can see the bubble extent.
    DrawSphereWires(c, r, 8, 12, {80, 160, 240, 70});
    // Equator (XZ plane).
    DrawCircle3D(c, r, {0, 1, 0}, 90.0f, col);
    // Front-rear meridian (XY plane).
    DrawCircle3D(c, r, {0, 0, 1}, 0.0f, col);
    // Left-right meridian (YZ plane).
    DrawCircle3D(c, r, {1, 0, 0}, 0.0f, col);
  } else if (v.shieldModel == "per-face") {
    // Approximation for now — F.x will wire baked face groups.
    // Draw a translucent sphere outline + a hint label so the user
    // sees that the mode is recognised but not yet visualised.
    DrawSphereWires(c, r, 8, 12, {180, 220, 255, 90});
  } else {
    // omni — single bubble.
    DrawSphereWires(c, r, 8, 12, col);
  }
}

void ShieldsTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  if (!v.shieldsPresent) {
    drawText(
        "shields (no shields section  |  press N to add)",
        10, yCursor, 14, {200, 180, 140, 240});
    yCursor += 22;
    return;
  }

  drawText("shields (./, to cycle  |  ↑/↓ adjust or cycle model)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  char buf[128];
  // Model row.
  {
    bool focused = (m_focus == 0);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s model = %s",
                  focused ? "►" : "  ", v.shieldModel.c_str());
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  }
  // Numeric rows.
  for (int i = 1; i < kNumFields; ++i) {
    bool focused = (i == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    const float *f = numericFieldAt(const_cast<ProfileView &>(v), i);
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(i),
                  f ? *f : 0.0f);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  }
}

bool ShieldsTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[ShieldsTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void ShieldsTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
