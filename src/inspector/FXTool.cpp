#include "FXTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"

#include <cstdio>

namespace tsmesh {

namespace {

constexpr int kNumFields = 5;

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "smokeAtHPFrac      ";
  case 1: return "deathExplosionScale";
  case 2: return "engineGlow.r       ";
  case 3: return "engineGlow.g       ";
  case 4: return "engineGlow.b       ";
  }
  return "?";
}

void stepFloat(int idx, float &fine, float &coarse) {
  switch (idx) {
  case 0: fine = 0.05f; coarse = 0.10f; return; // smokeAtHPFrac (0..1)
  case 1: fine = 0.1f;  coarse = 0.5f;  return; // deathExplosionScale
  }
  fine = 0.05f; coarse = 0.1f;
}

} // anonymous namespace

void FXTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;

  if (IsKeyPressed(KEY_N) && !v.fxPresent) {
    v.fxPresent = true;
    m_dirty = true;
  }
  if (!v.fxPresent) return;

  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

  if (!(up || down)) return;

  if (m_focus == 0) {
    float fine, coarse;
    stepFloat(0, fine, coarse);
    v.smokeAtHPFrac += (up ? (shift ? coarse : fine) : -(shift ? coarse : fine));
    if (v.smokeAtHPFrac < 0.0f) v.smokeAtHPFrac = 0.0f;
    if (v.smokeAtHPFrac > 1.0f) v.smokeAtHPFrac = 1.0f;
    m_dirty = true;
  } else if (m_focus == 1) {
    float fine, coarse;
    stepFloat(1, fine, coarse);
    v.deathExplosionScale +=
        (up ? (shift ? coarse : fine) : -(shift ? coarse : fine));
    if (v.deathExplosionScale < 0.0f) v.deathExplosionScale = 0.0f;
    m_dirty = true;
  } else {
    // RGB component: ±5 fine, ±25 coarse; clamps to [0, 255].
    int step = shift ? 25 : 5;
    auto bump = [&](unsigned char &b) {
      int next = static_cast<int>(b) + (up ? step : -step);
      if (next < 0) next = 0;
      if (next > 255) next = 255;
      b = static_cast<unsigned char>(next);
      m_dirty = true;
    };
    if (m_focus == 2) bump(v.engineGlowR);
    else if (m_focus == 3) bump(v.engineGlowG);
    else if (m_focus == 4) bump(v.engineGlowB);
  }
}

void FXTool::render3D(const Inspector & /*insp*/) const {
  // No 3D overlay for now — smoke threshold is per-tick gameplay,
  // glow is for the engine emitter system (future wire-up). The
  // colour swatch in the HUD shows the current glow value.
}

void FXTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  if (!v.fxPresent) {
    drawText("fx (no fx section  |  press N to add)",
             10, yCursor, 14, {200, 180, 140, 240});
    yCursor += 22;
    return;
  }

  drawText("fx (./, cycle  |  ↑/↓ adjust  |  Shift = coarse  |  "
           "RGB step ±5 / ±25)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  char buf[128];
  auto fRow = [&](int idx, float val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(idx), val);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto iRow = [&](int idx, int val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %d",
                  focused ? "►" : "  ", fieldLabel(idx), val);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  fRow(0, v.smokeAtHPFrac);
  fRow(1, v.deathExplosionScale);
  iRow(2, static_cast<int>(v.engineGlowR));
  iRow(3, static_cast<int>(v.engineGlowG));
  iRow(4, static_cast<int>(v.engineGlowB));

  // Colour swatch — a small filled rectangle next to the form using
  // the live RGB so the user can see the result of their edits.
  Color swatch = {v.engineGlowR, v.engineGlowG, v.engineGlowB, 255};
  DrawRectangle(220, yCursor - 4 * 18 + 24, 60, 60, swatch);
  DrawRectangleLines(220, yCursor - 4 * 18 + 24, 60, 60,
                     {120, 150, 190, 255});
}

bool FXTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[FXTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void FXTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
