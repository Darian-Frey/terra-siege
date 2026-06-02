#pragma once

#include "Tool.hpp"
#include "raylib.h"

namespace tsmesh {

// Click-to-select a vertex, drag to translate. Hold X/Y/Z during the
// drag for axis-locked motion. S saves only the `v ` lines via
// tsmesh::saveObjVertices (3d_assets.md §8.3).
class VertexTool : public Tool {
public:
  const char *name() const override { return "vertex"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  void pickVertex(Inspector &insp);
  void dragVertex(Inspector &insp, int axisLock);

  int m_selected = -1;
  bool m_dragging = false;
  Vector3 m_dragStartPos{};
  int m_axisLock = -1; // 0=X, 1=Y, 2=Z, -1=free (camera plane)
  bool m_dirty = false;
};

} // namespace tsmesh
