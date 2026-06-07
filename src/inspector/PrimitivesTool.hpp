#pragma once

#include "Tool.hpp"
#include "raylib.h"

namespace tsmesh {

// Inspector Phase E — primitive insertion. Authors a new mesh from
// scratch by appending parametric primitives (cube / cylinder / cone /
// icosphere / plane) into the current mesh's vertex + index arrays.
//
//   * `[` / `]` cycles the primitive type
//   * `./,` cycles the focused parameter field
//   * ↑/↓ adjusts the focused field (Shift = coarse)
//   * Enter inserts the primitive at the current center
//   * N (only when no mesh is loaded) creates a fresh empty mesh so
//     the very first primitive has somewhere to live
//
// Insertion APPENDS to the mesh — no separate object concept. New
// vertices are added at the end of mesh.vertices; new triangles
// reference them via base-offset indices. Face normals are computed
// per-triangle so the loader's flat-shaded look survives.
//
// Each primitive emits triangles directly (matches the loader's
// triangulated expectations) so no Triangulate-quads pass is needed
// for first cut.
class PrimitivesTool : public Tool {
public:
  enum class Kind { Cube, Plane, Cylinder, Cone, Sphere, Count };

  const char *name() const override { return "primitives"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  void onReload(Inspector &insp) override;
  bool canRunWithoutMesh() const override { return true; }

private:
  // Parameter ranges differ per primitive type. We keep a single bag
  // of parameters and only edit / display the ones relevant to the
  // current Kind.
  Kind m_kind = Kind::Cube;
  Vector3 m_center{0, 0, 0};
  float m_size = 1.0f;       // cube / plane edge length
  float m_radius = 0.5f;     // cylinder / cone / sphere
  float m_height = 1.0f;     // cylinder / cone
  int m_segments = 12;       // cylinder / cone azimuthal segments
  int m_subdivisions = 1;    // icosphere subdivision count (0..3)

  // Focused field index — what `./,` cycles + ↑/↓ adjusts. Numbering
  // varies per Kind so the cycle skips fields that don't apply.
  int m_focus = 0;

  // Emit helpers. Each pushes new vertices + indices + faceNormals +
  // facePalette into the mesh and recomputes the bounding sphere.
  void insert(Inspector &insp);
  void emitCube(Inspector &insp);
  void emitPlane(Inspector &insp);
  void emitCylinder(Inspector &insp);
  void emitCone(Inspector &insp);
  void emitSphere(Inspector &insp);

  // Field-set bookkeeping per Kind.
  int fieldCount(Kind k) const;
  const char *fieldLabel(Kind k, int idx) const;
  void adjustField(int idx, bool up, bool shift);
};

} // namespace tsmesh
