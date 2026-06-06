#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.2 mini-tool — shields model picker + HP / regen / delay form.
// `N` toggles the shields-block present flag. `./,` cycles focus,
// ↑/↓ adjusts numeric values OR cycles the model enum when its row
// is focused. 3D viz draws a translucent blue wireframe sphere
// (omni), four 90° pies (4-sector), or face highlights (per-face)
// at the pivot — sized to the hull collision radius if present,
// otherwise to a default bubble radius.
class ShieldsTool : public Tool {
public:
  const char *name() const override { return "shields"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  int m_focus = 0; // 0=model, 1=hp, 2=regen, 3=delay
  bool m_dirty = false;
};

} // namespace tsmesh
