#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.3 mini-tool — named hardpoint mount points. Each hardpoint
// stores pos / dir / fireArcDeg / weapon-name-ref + a label.
// `A` adds a new hardpoint at the pivot. `Shift+Del` deletes the
// selected one. `[/]` browses; `./,` cycles focus; ↑/↓ edits the
// focused field.
//
// 3D viz overlays the F.1 default hardpoint icons with a yellow
// "selected" highlight (larger sphere + brighter direction arrow +
// brighter fire-arc circle) so the active row is visible in 3D.
class HardpointsTool : public Tool {
public:
  const char *name() const override { return "hardpoints"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // Selected hardpoint index. -1 = list empty.
  int m_sel = -1;
  // Focused field within the selected hardpoint.
  //   0 = name, 1..3 = pos.x/y/z, 4..6 = dir.x/y/z,
  //   7 = fireArcDeg, 8 = weapon (text + cycle through Weapon list)
  int m_focus = 0;
  bool m_dirty = false;
};

} // namespace tsmesh
