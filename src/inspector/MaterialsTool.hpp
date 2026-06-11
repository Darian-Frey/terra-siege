#pragma once

#include "Tool.hpp"
#include "raylib.h"

#include <vector>

namespace tsmesh {

// Inspector Phase C — materials / palette tool.
//
//   * Right edge displays the 32-colour palette as a 4×8 swatch grid
//   * Hover a swatch to see its material name (cNN); click to pick
//     (becomes the "paint" colour, highlighted in the panel)
//   * Click a face in 3D to assign the paint colour to that triangle
//   * Shift+click stacks faces into a multi-selection — the next
//     palette click paints all of them at once
//   * E key while hovering a face → eyedropper: copies that face's
//     palette to the paint slot
//   * Unused palette indices are dimmed so authors see which colours
//     the current mesh actually references
//
// Save path goes through ObjLoader::saveObjMaterials — rewrites the
// face section (usemtl directives + face indices) when dirty.
// Vertex edits from VertexTool flow through the existing
// saveObjVertices path; the two are orthogonal.
class MaterialsTool : public Tool {
public:
  const char *name() const override { return "materials"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // Ray-pick the triangle under the mouse cursor. Returns triangle
  // index (0-based into mesh.indices/3) or -1 if no hit.
  int pickFace(const Inspector &insp) const;

  // Compute the swatch's screen rectangle for palette index `idx`.
  Rectangle swatchRect(int idx, int screenW, int screenH) const;

  // Is the current mesh using this palette index anywhere?
  bool isUsedInMesh(const Inspector &insp, int paletteIdx) const;

  int m_paintIdx = 0;             // currently selected palette index
  int m_hoverFace = -1;           // triangle under cursor (-1 = none)
  int m_hoverSwatch = -1;         // swatch under cursor (-1 = none)
  std::vector<int> m_selection;   // selected triangle indices (for multi-paint)
  bool m_dirty = false;
};

} // namespace tsmesh
