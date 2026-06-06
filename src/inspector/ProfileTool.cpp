#include "ProfileTool.hpp"

#include "Inspector.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

// Resolve focus index → mutable reference into the typed view. Layout:
//   0,1,2 = forward.x, forward.y, forward.z
//   3     = scale
//   4,5,6 = pivot.x, pivot.y, pivot.z
float &fieldAt(ProfileView &v, int idx) {
  switch (idx) {
  case 0: return v.forward.x;
  case 1: return v.forward.y;
  case 2: return v.forward.z;
  case 3: return v.scale;
  case 4: return v.pivot.x;
  case 5: return v.pivot.y;
  case 6: return v.pivot.z;
  }
  // Unreachable in practice; static field as a guard so the
  // compiler can prove all paths return.
  static float sink = 0.0f;
  return sink;
}

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "forward.x";
  case 1: return "forward.y";
  case 2: return "forward.z";
  case 3: return "scale";
  case 4: return "pivot.x";
  case 5: return "pivot.y";
  case 6: return "pivot.z";
  }
  return "?";
}

} // anonymous namespace

void ProfileTool::handleInput(Inspector &insp) {
  // Cycle focused field (Shift+Tab walks back). TAB itself cycles
  // tools at the Inspector level; we use plain keys here.
  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % 7;
  if (IsKeyPressed(KEY_COMMA)) m_focus = (m_focus + 6) % 7;

  // Adjust the focused field. Coarse / fine steps; shift = coarse.
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  float step = shift ? 1.0f : 0.05f;
  // Auto-repeat on hold for smoother editing.
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
  if (up || down) {
    float &f = fieldAt(insp.profile().view, m_focus);
    f += (up ? step : -step);
    // Forward gets renormalised so the arrow stays a unit vector —
    // the typed extractor does NOT renormalise at load time so the
    // user can pre-stage a non-unit vector, but editing always
    // produces a unit vector. Skip if all components collapse to 0
    // (avoid div-by-zero — leave the field alone).
    if (m_focus >= 0 && m_focus <= 2) {
      Vector3 &fwd = insp.profile().view.forward;
      float len = Vector3Length(fwd);
      if (len > 1e-4f) fwd = Vector3Scale(fwd, 1.0f / len);
    }
    // Scale clamped to a safe positive band.
    if (m_focus == 3) {
      float &s = insp.profile().view.scale;
      if (s < 0.01f) s = 0.01f;
      if (s > 50.0f) s = 50.0f;
    }
    m_dirty = true;
  }
}

void ProfileTool::render3D(const Inspector & /*insp*/) const {
  // 3D viewer overlay (forward arrow / hardpoints / AI rings / ...)
  // is rendered by Inspector::render() as a global overlay — it's
  // visible whether or not this tool is the active one. This method
  // intentionally renders nothing extra; the form panel in renderHud
  // is the tool's only contribution to the screen.
}

void ProfileTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  char buf[128];
  DrawText("profile (",  10, yCursor, 14, {220, 230, 250, 240});
  DrawText("./, to cycle  |  ↑/↓ ±0.05  |  Shift+↑/↓ ±1.0",
           74, yCursor, 14, {160, 180, 200, 220});
  DrawText(")", 462, yCursor, 14, {220, 230, 250, 240});
  yCursor += 22;

  auto row = [&](int idx, float value) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.4f",
                  focused ? "►" : "  ", fieldLabel(idx), value);
    DrawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  row(0, v.forward.x);
  row(1, v.forward.y);
  row(2, v.forward.z);
  row(3, v.scale);
  row(4, v.pivot.x);
  row(5, v.pivot.y);
  row(6, v.pivot.z);
}

bool ProfileTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[ProfileTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void ProfileTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
