#include "AITool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"

#include <cstdio>

namespace tsmesh {

namespace {

constexpr int kNumFields = 9;

// Profile + targetPref enum strings — kept in one place so the
// picker is consistent across files. Order matches the doc
// (terra_siege_inspect_roadmap.md F.4) plus the project's
// established names where used (boids-swarm for Drone, etc.).
constexpr const char *kProfiles[] = {
    "pursue-attack-evade", "kamikaze", "strafe-friendlies",
    "boids-swarm", "stationary-turret", "drift-deploy",
    "boss-orbit", "harvest-loop", "none"};
constexpr int kNumProfiles =
    static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));

constexpr const char *kTargetPrefs[] = {"player", "friendlies", "bases",
                                        "nearest"};
constexpr int kNumTargetPrefs =
    static_cast<int>(sizeof(kTargetPrefs) / sizeof(kTargetPrefs[0]));

int indexOf(const char *const *arr, int n, const std::string &s) {
  for (int i = 0; i < n; ++i)
    if (s == arr[i]) return i;
  return -1;
}

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "profile         ";
  case 1: return "targetPref      ";
  case 2: return "detectionRange  ";
  case 3: return "attackRange     ";
  case 4: return "evadeAtHPFrac   ";
  case 5: return "retreatAtHPFrac ";
  case 6: return "canBeInfected   ";
  case 7: return "rebootDuration  ";
  case 8: return "speedPenaltyAft ";
  }
  return "?";
}

float *floatFieldAt(ProfileView &v, int idx) {
  switch (idx) {
  case 2: return &v.detectionRange;
  case 3: return &v.attackRange;
  case 4: return &v.evadeAtHPFrac;
  case 5: return &v.retreatAtHPFrac;
  case 7: return &v.rebootDuration;
  case 8: return &v.speedPenaltyAfter;
  }
  return nullptr;
}

void stepValues(int idx, float &fine, float &coarse) {
  switch (idx) {
  case 2: fine = 10.0f; coarse = 50.0f; return; // detection
  case 3: fine = 5.0f;  coarse = 25.0f; return; // attack
  case 4: fine = 0.05f; coarse = 0.10f; return; // evadeAtHPFrac (0..1)
  case 5: fine = 0.05f; coarse = 0.10f; return; // retreatAtHPFrac
  case 7: fine = 0.1f;  coarse = 1.0f;  return; // rebootDuration
  case 8: fine = 0.05f; coarse = 0.10f; return; // speedPenaltyAfter
  }
  fine = 0.05f; coarse = 1.0f;
}

} // anonymous namespace

void AITool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;

  // `N` adds the ai block + infection block if absent (they're
  // adjacent in tooling but two distinct sidecar sections).
  if (IsKeyPressed(KEY_N)) {
    bool changed = false;
    if (!v.aiPresent) { v.aiPresent = true; changed = true; }
    if (!v.infectionPresent) {
      v.infectionPresent = true;
      changed = true;
    }
    if (changed) m_dirty = true;
  }
  if (!v.aiPresent && !v.infectionPresent) return;

  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

  if (m_focus == 0 && (up || down)) {
    // Cycle the profile enum.
    int cur = indexOf(kProfiles, kNumProfiles, v.aiProfile);
    if (cur < 0) cur = 0;
    cur = (cur + (up ? 1 : kNumProfiles - 1)) % kNumProfiles;
    v.aiProfile = kProfiles[cur];
    if (!v.aiPresent) { v.aiPresent = true; }
    m_dirty = true;
  } else if (m_focus == 1 && (up || down)) {
    int cur = indexOf(kTargetPrefs, kNumTargetPrefs, v.targetPref);
    if (cur < 0) cur = 0;
    cur = (cur + (up ? 1 : kNumTargetPrefs - 1)) % kNumTargetPrefs;
    v.targetPref = kTargetPrefs[cur];
    if (!v.aiPresent) { v.aiPresent = true; }
    m_dirty = true;
  } else if (m_focus == 6) {
    // canBeInfected — toggle on Space / Enter / ↑↓.
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) ||
        up || down) {
      v.canBeInfected = !v.canBeInfected;
      if (!v.infectionPresent) v.infectionPresent = true;
      m_dirty = true;
    }
  } else if (up || down) {
    if (float *f = floatFieldAt(v, m_focus)) {
      float fine, coarse;
      stepValues(m_focus, fine, coarse);
      *f += (up ? (shift ? coarse : fine) : -(shift ? coarse : fine));
      if (*f < 0.0f) *f = 0.0f;
      // 0..1 fields clamp.
      if (m_focus == 4 || m_focus == 5 || m_focus == 8) {
        if (*f > 1.0f) *f = 1.0f;
      }
      // The edited field implies a present flag — flip if needed.
      if (m_focus <= 5 && !v.aiPresent) v.aiPresent = true;
      if (m_focus >= 7 && !v.infectionPresent) v.infectionPresent = true;
      m_dirty = true;
    }
  }
}

void AITool::render3D(const Inspector &insp) const {
  const ProfileView &v = insp.profile().view;
  if (!v.aiPresent) return;

  Vector3 c = v.pivot;
  // Two wireframe rings on the XZ plane (Y axis). Orange = detection,
  // red = attack. DrawCircle3D needs a rotation axis + angle around it;
  // axis = +Y rotates a Z-aligned circle into the XZ plane.
  if (v.detectionRange > 0.0f) {
    DrawCircle3D(c, v.detectionRange, {0, 1, 0}, 90.0f,
                 {255, 180, 80, 180});
  }
  if (v.attackRange > 0.0f) {
    DrawCircle3D(c, v.attackRange, {0, 1, 0}, 90.0f,
                 {255, 100, 100, 220});
  }
}

void AITool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  if (!v.aiPresent && !v.infectionPresent) {
    drawText("ai (no ai/infection sections  |  press N to add)",
             10, yCursor, 14, {200, 180, 140, 240});
    yCursor += 22;
    return;
  }

  drawText("ai (./, cycle  |  ↑/↓ adjust or cycle enum  |  "
           "Shift = coarse  |  Space toggles bool)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  char buf[256];
  auto strRow = [&](int idx, const std::string &val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %s",
                  focused ? "►" : "  ", fieldLabel(idx),
                  val.empty() ? "(unset)" : val.c_str());
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto fRow = [&](int idx, float val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(idx), val);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto bRow = [&](int idx, bool val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %s",
                  focused ? "►" : "  ", fieldLabel(idx),
                  val ? "true" : "false");
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };

  strRow(0, v.aiProfile);
  strRow(1, v.targetPref);
  fRow(2, v.detectionRange);
  fRow(3, v.attackRange);
  fRow(4, v.evadeAtHPFrac);
  fRow(5, v.retreatAtHPFrac);
  bRow(6, v.canBeInfected);
  fRow(7, v.rebootDuration);
  fRow(8, v.speedPenaltyAfter);
}

bool AITool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[AITool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void AITool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
