#pragma once

#include "Tool.hpp"
#include "raylib.h"

#include <vector>

namespace tsmesh {

// Inspector Phase D — topology editing.
//
// Lets authors actually *build* geometry rather than just nudge it:
// add and delete vertices, add and delete faces, flip winding,
// recompute normals.
//
// Workflow:
//   * Click a vertex sphere → toggle into the ordered selection
//     (numbered 1, 2, 3 in the HUD so you can see winding order)
//   * Click on a face (with no vertex hit) → that face becomes the
//     selected face (green outline)
//   * Click in empty space → clear selections
//   * V — add a new vertex at the cursor, projected onto the camera-
//     target plane (so the new vert lands near the visible mesh,
//     not at the floor)
//   * F — with exactly 3 vertices selected, add a triangle using the
//     selection order (CCW winding by default)
//   * Shift+F — flip the selected face's winding (b↔c)
//   * Del — delete the selected face OR the selected vertex (and
//     every face that referenced it)
//   * N — recompute every face normal from current vertex positions
//   * ESC — clear selections
//
// Save path goes through ObjLoader::saveObjMesh — a full rewrite,
// since topology changes break the byte-exact preservation contract
// that saveObjVertices and saveObjMaterials rely on. The original
// file's leading header (comments + o/g/s before geometry) and
// trailing footer are kept.
class TopologyTool : public Tool {
public:
  const char *name() const override { return "topology"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }
  // Runs without a mesh so the user can author from scratch — first
  // pressing V on an empty workspace seeds a fresh mesh just like
  // PrimitivesTool does.
  bool canRunWithoutMesh() const override { return true; }

private:
  // Mouse picks — closest hit.
  int pickVertex(const Inspector &insp) const;
  int pickFace(const Inspector &insp) const;

  // Project the mouse ray onto a plane through the inspector's
  // camera target, perpendicular to world up. Returns the world-
  // space hit point — used by "add vertex".
  Vector3 cursorOnCameraPlane(const Inspector &insp) const;

  // Topology mutations. Each pushes undo before mutating.
  void addVertex(Inspector &insp, Vector3 pos);
  void deleteSelectedVertex(Inspector &insp);
  void addFaceFromSelection(Inspector &insp);
  void deleteSelectedFace(Inspector &insp);
  void flipSelectedFaceWinding(Inspector &insp);
  void recomputeAllNormals(Inspector &insp);

  std::vector<int> m_vertSel; // ordered — for face-build winding
  int m_faceSel = -1;
  int m_hoverVert = -1;
  int m_hoverFace = -1;
  bool m_dirty = false;
};

} // namespace tsmesh
