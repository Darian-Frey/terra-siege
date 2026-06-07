#include "IdentityTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"

#include <cstdio>
#include <cstring>

namespace tsmesh {

namespace {

// Canonical entity-class strings — IdentityTool's only enum field.
constexpr const char *kClasses[] = {"static", "ground", "hover", "flyer",
                                    "projectile", "ship-flyer"};
constexpr int kNumClasses =
    static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));

int classIndexOf(const std::string &s) {
  for (int i = 0; i < kNumClasses; ++i)
    if (s == kClasses[i]) return i;
  return -1;
}

} // anonymous namespace

void IdentityTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;

  // Focus cycle — `.` next, `,` prev. Chosen-character chars are
  // intentionally not consumed into text fields by the GetCharPressed
  // loop below so they reliably drive focus.
  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % 3;
  if (IsKeyPressed(KEY_COMMA))  m_focus = (m_focus + 2) % 3;

  // Numeric/enum nudge keys.
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
  if (m_focus == 1 && (up || down)) {
    int cur = classIndexOf(v.entityClass);
    if (cur < 0) cur = 0;
    cur = (cur + (up ? 1 : kNumClasses - 1)) % kNumClasses;
    v.entityClass = kClasses[cur];
    m_dirty = true;
  }

  // Text input for displayName + faction. Skip `.` and `,` so they
  // remain the focus-cycle keys.
  std::string *textField = nullptr;
  if (m_focus == 0) textField = &v.displayName;
  else if (m_focus == 2) textField = &v.faction;
  if (textField) {
    int c;
    while ((c = GetCharPressed()) > 0) {
      if (c >= 32 && c < 127 && c != '.' && c != ',') {
        textField->push_back(static_cast<char>(c));
        m_dirty = true;
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !textField->empty()) {
      textField->pop_back();
      m_dirty = true;
    }
  }
}

void IdentityTool::render3D(const Inspector & /*insp*/) const {
  // No 3D overlay — identity is text-only metadata.
}

void IdentityTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;
  drawText("identity (./, to cycle  |  type to edit text  |  "
           "↑/↓ cycles class)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  char buf[256];
  auto row = [&](int idx, const char *label, const std::string &value) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %s%s",
                  focused ? "►" : "  ", label,
                  value.empty() ? "(empty)" : value.c_str(),
                  focused ? "_" : "");
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  row(0, "displayName", v.displayName);
  row(1, "class      ", v.entityClass);
  row(2, "faction    ", v.faction);
}

bool IdentityTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[IdentityTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void IdentityTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
