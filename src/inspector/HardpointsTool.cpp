#include "HardpointsTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/SidecarProfile.hpp"
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

constexpr int kNumFields = 9;

const char *fieldLabel(int idx) {
  switch (idx) {
  case 0: return "name      ";
  case 1: return "pos.x     ";
  case 2: return "pos.y     ";
  case 3: return "pos.z     ";
  case 4: return "dir.x     ";
  case 5: return "dir.y     ";
  case 6: return "dir.z     ";
  case 7: return "fireArcDeg";
  case 8: return "weapon    ";
  }
  return "?";
}

bool isPos(int idx) { return idx >= 1 && idx <= 3; }
bool isDir(int idx) { return idx >= 4 && idx <= 6; }

float *posField(ProfileView::Hardpoint &hp, int idx) {
  switch (idx) {
  case 1: return &hp.pos.x;
  case 2: return &hp.pos.y;
  case 3: return &hp.pos.z;
  }
  return nullptr;
}

float *dirField(ProfileView::Hardpoint &hp, int idx) {
  switch (idx) {
  case 4: return &hp.dir.x;
  case 5: return &hp.dir.y;
  case 6: return &hp.dir.z;
  }
  return nullptr;
}

} // anonymous namespace

void HardpointsTool::handleInput(Inspector &insp) {
  ProfileView &v = insp.profile().view;
  int n = static_cast<int>(v.hardpoints.size());
  if (m_sel >= n) m_sel = n - 1;
  if (n > 0 && m_sel < 0) m_sel = 0;

  // Add at pivot / delete selected.
  if (IsKeyPressed(KEY_A)) {
    ProfileView::Hardpoint hp;
    hp.name = "hp-" + std::to_string(v.hardpoints.size());
    hp.pos = v.pivot;
    hp.dir = {0.0f, 0.0f, 1.0f};
    hp.fireArcDeg = 8.0f;
    v.hardpoints.push_back(std::move(hp));
    m_sel = static_cast<int>(v.hardpoints.size()) - 1;
    m_focus = 0;
    m_dirty = true;
    return;
  }
  if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
      IsKeyPressed(KEY_DELETE) && m_sel >= 0 && m_sel < n) {
    v.hardpoints.erase(v.hardpoints.begin() + m_sel);
    if (m_sel >= static_cast<int>(v.hardpoints.size()))
      m_sel = static_cast<int>(v.hardpoints.size()) - 1;
    m_dirty = true;
    return;
  }

  if (n > 0) {
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) m_sel = (m_sel + 1) % n;
    if (IsKeyPressed(KEY_LEFT_BRACKET)) m_sel = (m_sel + n - 1) % n;
  }

  if (m_sel < 0 || m_sel >= n) return;
  ProfileView::Hardpoint &hp = v.hardpoints[m_sel];

  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % kNumFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + kNumFields - 1) % kNumFields;

  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

  if (m_focus == 0) {
    // Name — text input.
    int c;
    while ((c = GetCharPressed()) > 0) {
      if (c >= 32 && c < 127 && c != '.' && c != ',' &&
          c != '[' && c != ']') {
        hp.name.push_back(static_cast<char>(c));
        m_dirty = true;
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !hp.name.empty()) {
      hp.name.pop_back();
      m_dirty = true;
    }
  } else if (m_focus == 8) {
    // Weapon ref — cycle through the profile's defined weapon names
    // with ↑/↓, OR type to set an arbitrary string. Cycling is
    // usually what the user wants; typing is the escape hatch.
    int c;
    while ((c = GetCharPressed()) > 0) {
      if (c >= 32 && c < 127 && c != '.' && c != ',' &&
          c != '[' && c != ']') {
        hp.weapon.push_back(static_cast<char>(c));
        m_dirty = true;
      }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !hp.weapon.empty()) {
      hp.weapon.pop_back();
      m_dirty = true;
    }
    if (up || down) {
      const auto &ws = v.weapons;
      if (!ws.empty()) {
        // Find current index; -1 if no match. Cycle forwards/back.
        int idx = -1;
        for (size_t i = 0; i < ws.size(); ++i)
          if (ws[i].name == hp.weapon) { idx = static_cast<int>(i); break; }
        int nn = static_cast<int>(ws.size());
        idx = (idx + (up ? 1 : nn - 1) + nn) % nn;
        hp.weapon = ws[idx].name;
        m_dirty = true;
      }
    }
  } else if (isPos(m_focus) && (up || down)) {
    if (float *f = posField(hp, m_focus)) {
      float step = shift ? 1.0f : 0.05f;
      *f += (up ? step : -step);
      m_dirty = true;
    }
  } else if (isDir(m_focus) && (up || down)) {
    if (float *f = dirField(hp, m_focus)) {
      float step = shift ? 0.5f : 0.05f;
      *f += (up ? step : -step);
      // Renormalise so the cap pole + arrow stay unit-length.
      float L = Vector3Length(hp.dir);
      if (L > 1e-4f) hp.dir = Vector3Scale(hp.dir, 1.0f / L);
      m_dirty = true;
    }
  } else if (m_focus == 7 && (up || down)) {
    float step = shift ? 10.0f : 1.0f;
    hp.fireArcDeg += (up ? step : -step);
    if (hp.fireArcDeg < 0.0f) hp.fireArcDeg = 0.0f;
    if (hp.fireArcDeg > 180.0f) hp.fireArcDeg = 180.0f;
    m_dirty = true;
  }
}

void HardpointsTool::render3D(const Inspector &insp) const {
  const ProfileView &v = insp.profile().view;
  if (m_sel < 0 || m_sel >= static_cast<int>(v.hardpoints.size())) return;

  const ProfileView::Hardpoint &hp = v.hardpoints[m_sel];
  // Highlight ring + larger sphere over the F.1 default icon so the
  // active row is unmistakable in 3D.
  float r = insp.boundsRadius();
  Color hl = {255, 220, 80, 255};
  DrawSphereEx(hp.pos, r * 0.07f, 6, 8, hl);
  Vector3 dn = hp.dir;
  float dl = sqrtf(dn.x * dn.x + dn.y * dn.y + dn.z * dn.z);
  if (dl > 1e-4f) {
    dn.x /= dl; dn.y /= dl; dn.z /= dl;
    float L = r * 0.55f;
    Vector3 tip = {hp.pos.x + dn.x * L, hp.pos.y + dn.y * L,
                   hp.pos.z + dn.z * L};
    DrawLine3D(hp.pos, tip, hl);
  }
}

void HardpointsTool::renderHud(const Inspector &insp, int &yCursor) const {
  const ProfileView &v = insp.profile().view;

  drawText("hardpoints (A add  |  Shift+Del delete  |  [/] browse  |  "
           "./, field  |  ↑/↓ edit)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  if (v.hardpoints.empty()) {
    drawText("(no hardpoints — press A to add one at the pivot)",
             14, yCursor, 14, {200, 180, 140, 240});
    yCursor += 20;
    return;
  }

  char buf[256];
  int n = static_cast<int>(v.hardpoints.size());
  std::snprintf(buf, sizeof(buf), "hardpoint %d / %d",
                m_sel + 1, n);
  drawText(buf, 14, yCursor, 14, {200, 220, 240, 230});
  yCursor += 18;

  if (m_sel < 0 || m_sel >= n) return;
  const ProfileView::Hardpoint &hp = v.hardpoints[m_sel];

  auto strRow = [&](int idx, const std::string &val) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %s%s",
                  focused ? "►" : "  ", fieldLabel(idx),
                  val.empty() ? "(empty)" : val.c_str(),
                  focused ? "_" : "");
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  auto fRow = [&](int idx, float value) {
    bool focused = (idx == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    std::snprintf(buf, sizeof(buf), "%s %s = %.3f",
                  focused ? "►" : "  ", fieldLabel(idx), value);
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  };
  strRow(0, hp.name);
  fRow(1, hp.pos.x); fRow(2, hp.pos.y); fRow(3, hp.pos.z);
  fRow(4, hp.dir.x); fRow(5, hp.dir.y); fRow(6, hp.dir.z);
  fRow(7, hp.fireArcDeg);
  strRow(8, hp.weapon);
}

bool HardpointsTool::save(Inspector &insp) {
  if (!m_dirty) return true;
  std::filesystem::path side = sidecarPathFor(insp.path());
  if (!saveProfile(side, insp.profile())) {
    std::fprintf(stderr, "[HardpointsTool] save failed: %s\n",
                 side.string().c_str());
    return false;
  }
  m_dirty = false;
  return true;
}

void HardpointsTool::onReload(Inspector &insp) {
  int n = static_cast<int>(insp.profile().view.hardpoints.size());
  m_sel = (n > 0) ? 0 : -1;
  m_focus = 0;
  m_dirty = false;
}

} // namespace tsmesh
