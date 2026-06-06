#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.1 mini-tool — forward / scale / pivot form fields. Pulls/pushes
// the values through Inspector::profile().view; dirty flag flips when
// any of them are edited, which the Inspector save flow flushes via
// saveProfile() on Ctrl+S.
//
// Tab cycles between the 7 numeric fields (3 forward, 1 scale, 3 pivot).
// Up/Down adjust the focused field by ±0.05; Shift+Up/Down by ±1.0.
// R reverts the focused field to the last-saved value.
class ProfileTool : public Tool {
public:
  const char *name() const override { return "profile"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  int m_focus = 0;       // 0..6 — 0..2 forward, 3 scale, 4..6 pivot
  bool m_dirty = false;
};

} // namespace tsmesh
