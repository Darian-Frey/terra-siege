#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.3 mini-tool — named weapon stat blocks. The sidecar's `weapons`
// array is a list of `ProfileView::Weapon` records; this tool lets
// the user browse the list with `[`/`]`, edit fields of the focused
// row with `./,` + ↑/↓, add a new blank weapon with `A`, and delete
// the focused weapon with `Shift+Del`.
//
// No 3D viz — weapons are pure metadata referenced by hardpoints.
class WeaponsTool : public Tool {
public:
  const char *name() const override { return "weapons"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // Selected weapon index in the profile's weapons list. -1 = list
  // is empty (no rows to edit).
  int m_sel = -1;
  // Focused field within the selected weapon. Field layout:
  //   0 = name, 1 = type, 2 = fireRate, 3 = damage,
  //   4 = projSpeed, 5 = range, 6 = ammo
  int m_focus = 0;
  bool m_dirty = false;
};

} // namespace tsmesh
