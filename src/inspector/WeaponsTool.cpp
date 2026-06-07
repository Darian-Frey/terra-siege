#include "WeaponsTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"

#include <cstdio>

namespace tsmesh {

namespace {

constexpr int kNumFields = 7;

// Resolve focus index → mutable reference / category.
const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "name     ";
  case 1: return "type     ";
  case 2: return "fireRate ";
  case 3: return "damage   ";
  case 4: return "projSpeed";
  case 5: return "range    ";
  case 6: return "ammo     ";
  }
  return "?";
}

bool isTextField(int idx) { return idx == 0 || idx == 1; }
bool isIntField(int idx) { return idx == 6; }

float *floatFieldAt(ProfileView::Weapon &w, int idx) {
  switch (idx) {
  case 2: return &w.fireRate;
  case 3: return &w.damage;
  case 4: return &w.projSpeed;
  case 5: return &w.range;
  }
  return nullptr;
}

std::string *textFieldAt(ProfileView::Weapon &w, int idx) {
  switch (idx) {
  case 0: return &w.name;
  case 1: return &w.type;
  }
  return nullptr;
}

void stepValues(int idx, float &fine, float &coarse) {
  switch (idx) {
  case 2: fine = 0.05f; coarse = 0.5f;  return; // fireRate
  case 3: fine = 1.0f;  coarse = 10.0f; return; // damage
  case 4: fine = 5.0f;  coarse = 50.0f; return; // projSpeed
  case 5: fine = 5.0f;  coarse = 50.0f; return; // range
  case 6: fine = 1.0f;  coarse = 10.0f; return; // ammo
  }
  fine = 0.05f; coarse = 1.0f;
}

} // anonymous namespace

void WeaponsTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;
  // Keep m_sel in range as the list mutates outside our control
  // (e.g. another tool added a weapon).
  int n = static_cast<int>(v.weapons.size());
  if (m_sel >= n) m_sel = n - 1;
  if (n > 0 && m_sel < 0) m_sel = 0;

  // Add / delete.
  if (IsKeyPressed(KEY_A)) {
    ProfileView::Weapon w;
    w.name = "weapon-" + std::to_string(v.weapons.size());
    v.weapons.push_back(std::move(w));
    m_sel = static_cast<int>(v.weapons.size()) - 1;
    m_focus = 0;
    m_dirty = true;
    return; // 'a' character would otherwise leak into the name field below
  }
  if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
      IsKeyPressed(KEY_DELETE) && m_sel >= 0 && m_sel < n) {
    v.weapons.erase(v.weapons.begin() + m_sel);
    if (m_sel >= static_cast<int>(v.weapons.size()))
      m_sel = static_cast<int>(v.weapons.size()) - 1;
    m_dirty = true;
    return;
  }

  // Browse weapons.
  if (n > 0) {
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) m_sel = (m_sel + 1) % n;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) m_sel = (m_sel + n - 1) % n;
  }

  if (m_sel < 0 || m_sel >= n) return;
  ProfileView::Weapon &w = v.weapons[m_sel];

  // Cycle focus within the selected weapon.
  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  // Edit the focused field.
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

  if (isTextField(m_focus)) {
    if (std::string *s = textFieldAt(w, m_focus)) {
      int c;
      while ((c = GetCharPressed()) > 0) {
        // Exclude `.` / `,` (focus cycle) and `[` / `]` (weapon
        // browse) from typed input so they remain navigation keys.
        if (c >= 32 && c < 127 && c != '.' && c != ',' &&
            c != '[' && c != ']') {
          s->push_back(static_cast<char>(c));
          m_dirty = true;
        }
      }
      if (IsKeyPressed(KEY_BACKSPACE) && !s->empty()) {
        s->pop_back();
        m_dirty = true;
      }
    }
  } else if (isIntField(m_focus)) {
    if (up || down) {
      bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
      int step = shift ? 10 : 1;
      w.ammo += (up ? step : -step);
      if (w.ammo < -1) w.ammo = -1; // -1 = unlimited; nothing lower
      m_dirty = true;
    }
  } else if (up || down) {
    if (float *f = floatFieldAt(w, m_focus)) {
      float fine, coarse;
      stepValues(m_focus, fine, coarse);
      bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
      *f += (up ? (shift ? coarse : fine) : -(shift ? coarse : fine));
      if (*f < 0.0f) *f = 0.0f;
      m_dirty = true;
    }
  }
}

void WeaponsTool::render3D(const Inspector & /*insp*/) const {
  // Weapons are metadata — no 3D viz. Hardpoints (F.3) reference them
  // and draw the visual indicators.
}

void WeaponsTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  drawText("weapons (A add  |  Shift+Del delete  |  [/] browse  |  "
           "./, field  |  ↑/↓ edit)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  if (v.weapons.empty()) {
    drawText("(no weapons defined — press A to add one)",
             14, yCursor, 14, {200, 180, 140, 240});
    yCursor += 20;
    return;
  }

  char buf[256];
  int n = static_cast<int>(v.weapons.size());
  std::snprintf(buf, sizeof(buf), "weapon %d / %d",
                m_sel + 1, n);
  drawText(buf, 14, yCursor, 14, {200, 220, 240, 230});
  yCursor += 18;

  if (m_sel < 0 || m_sel >= n) return;
  const ProfileView::Weapon &w = v.weapons[m_sel];

  auto row = [&](int idx, const std::string &textVal) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %s%s",
                  focused ? "►" : "  ", fieldLabel(idx),
                  textVal.empty() ? "(empty)" : textVal.c_str(),
                  focused ? "_" : "");
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto frow = [&](int idx, float value) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(idx), value);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto irow = [&](int idx, int value) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    if (value < 0) {
      std::snprintf(buf, sizeof(buf), "%s %s = unlimited",
                    focused ? "►" : "  ", fieldLabel(idx));
    } else {
      std::snprintf(buf, sizeof(buf), "%s %s = %d",
                    focused ? "►" : "  ", fieldLabel(idx), value);
    }
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  row(0, w.name);
  row(1, w.type);
  frow(2, w.fireRate);
  frow(3, w.damage);
  frow(4, w.projSpeed);
  frow(5, w.range);
  irow(6, w.ammo);
}

bool WeaponsTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[WeaponsTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void WeaponsTool::onReload(Inspector &insp) {
  // Position the selector on the first weapon if any exist; otherwise
  // -1 (the HUD shows the "press A to add" hint).
  int n = static_cast<int>(insp.profile().view.weapons.size());
  m_sel = (n > 0) ? 0 : -1;
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
