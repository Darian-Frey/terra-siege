#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.5 mini-tool — smoke threshold + death explosion scale + engine
// glow RGB tint. `N` adds an fx block if absent. No 3D viz on its
// own (the smoke threshold visibly affects the running game once
// the entity is in the world; the glow color is for the engine
// emitter system which is a future-tool wire-up).
class FXTool : public Tool {
public:
  const char *name() const override { return "fx"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // 0=smokeAtHPFrac, 1=deathExplosionScale, 2=glowR, 3=glowG, 4=glowB
  int m_focus = 0;
  bool m_dirty = false;
};

} // namespace tsmesh
