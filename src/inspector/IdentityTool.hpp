#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.2 mini-tool — identity fields (displayName / class / faction).
// Click a row to focus; type to edit text; ↑/↓ cycles the class
// enum; `./,` cycles focus between rows. Dirty flag flushes through
// saveProfile() on Ctrl+S like the rest of the sidecar tools.
class IdentityTool : public Tool {
public:
  const char *name() const override { return "identity"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  int m_focus = 0; // 0=displayName, 1=class, 2=faction
  bool m_dirty = false;
};

} // namespace tsmesh
