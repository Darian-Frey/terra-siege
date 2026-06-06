#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.2 mini-tool — hull HP / collisionRadius / mass / wreckage yield.
// `N` toggles the hull-block present flag (lets the user add a hull
// section to a sidecar that didn't have one). Numeric fields use the
// ProfileTool conventions: `./,` cycles focus, ↑/↓ adjusts ±0.5,
// Shift+↑/↓ adjusts ±10.
//
// 3D viz — translucent yellow wireframe sphere at the pivot with
// the configured collisionRadius. Drawn only while this tool is
// active so the other tools' overlays don't fight for attention.
class HullTool : public Tool {
public:
  const char *name() const override { return "hull"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  int m_focus = 0; // 0=hp, 1=radius, 2=mass, 3=metal, 4=bio
  bool m_dirty = false;
};

} // namespace tsmesh
