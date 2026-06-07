#include "PrimitivesTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/ObjLoader.hpp"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

constexpr float kPI = 3.14159265358979f;
constexpr float kTwoPI = 2.0f * kPI;

// Push one triangle into the mesh with auto face-normal + default
// palette index 0. Indices are post-base so callers pass local vert
// indices relative to the start of their primitive's vertices.
void pushTri(Mesh3D &m, size_t baseVert, int32_t a, int32_t b, int32_t c) {
  int32_t ia = static_cast<int32_t>(baseVert) + a;
  int32_t ib = static_cast<int32_t>(baseVert) + b;
  int32_t ic = static_cast<int32_t>(baseVert) + c;
  m.indices.push_back(ia);
  m.indices.push_back(ib);
  m.indices.push_back(ic);
  // Face normal from the cross product of two edges.
  Vector3 e1 = Vector3Subtract(m.vertices[ib], m.vertices[ia]);
  Vector3 e2 = Vector3Subtract(m.vertices[ic], m.vertices[ia]);
  Vector3 n = Vector3CrossProduct(e1, e2);
  float L = Vector3Length(n);
  if (L > 1e-6f) n = Vector3Scale(n, 1.0f / L);
  else n = Vector3{0, 1, 0};
  m.faceNormals.push_back(n);
  m.facePalette.push_back(0); // first palette slot
}

} // anonymous namespace

// ====================================================================
// Field bookkeeping per Kind
// ====================================================================
int PrimitivesTool::fieldCount(Kind k) const {
  // Common: 3 center fields, then primitive-specific.
  switch (k) {
  case Kind::Cube:     return 3 + 1; // size
  case Kind::Plane:    return 3 + 1; // size
  case Kind::Cylinder: return 3 + 3; // radius / height / segments
  case Kind::Cone:     return 3 + 3; // radius / height / segments
  case Kind::Sphere:   return 3 + 2; // radius / subdivisions
  default: return 3;
  }
}

const char *PrimitivesTool::fieldLabel(Kind k, int idx) const {
  if (idx == 0) return "center.x";
  if (idx == 1) return "center.y";
  if (idx == 2) return "center.z";
  switch (k) {
  case Kind::Cube:
  case Kind::Plane:
    if (idx == 3) return "size";
    break;
  case Kind::Cylinder:
  case Kind::Cone:
    if (idx == 3) return "radius";
    if (idx == 4) return "height";
    if (idx == 5) return "segments";
    break;
  case Kind::Sphere:
    if (idx == 3) return "radius";
    if (idx == 4) return "subdivisions";
    break;
  default: break;
  }
  return "?";
}

void PrimitivesTool::adjustField(int idx, bool up, bool shift) {
  // Center fields: ±0.1 fine / ±1 coarse.
  if (idx >= 0 && idx <= 2) {
    float step = shift ? 1.0f : 0.1f;
    float d = up ? step : -step;
    if (idx == 0) m_center.x += d;
    else if (idx == 1) m_center.y += d;
    else m_center.z += d;
    return;
  }
  // Primitive-specific.
  switch (m_kind) {
  case Kind::Cube:
  case Kind::Plane:
    if (idx == 3) {
      float step = shift ? 1.0f : 0.1f;
      m_size += up ? step : -step;
      if (m_size < 0.05f) m_size = 0.05f;
    }
    break;
  case Kind::Cylinder:
  case Kind::Cone:
    if (idx == 3) {
      float step = shift ? 0.5f : 0.05f;
      m_radius += up ? step : -step;
      if (m_radius < 0.05f) m_radius = 0.05f;
    } else if (idx == 4) {
      float step = shift ? 1.0f : 0.1f;
      m_height += up ? step : -step;
      if (m_height < 0.05f) m_height = 0.05f;
    } else if (idx == 5) {
      int step = shift ? 4 : 1;
      m_segments += up ? step : -step;
      if (m_segments < 3) m_segments = 3;
      if (m_segments > 64) m_segments = 64;
    }
    break;
  case Kind::Sphere:
    if (idx == 3) {
      float step = shift ? 0.5f : 0.05f;
      m_radius += up ? step : -step;
      if (m_radius < 0.05f) m_radius = 0.05f;
    } else if (idx == 4) {
      int step = 1;
      m_subdivisions += up ? step : -step;
      if (m_subdivisions < 0) m_subdivisions = 0;
      if (m_subdivisions > 3) m_subdivisions = 3;
    }
    break;
  default: break;
  }
}

// ====================================================================
// Input
// ====================================================================
void PrimitivesTool::handleInput(Inspector &insp) {
  // N — bootstrap an empty mesh if none exists. Lets the user start
  // a fresh authoring session straight into PrimitivesTool.
  if (!insp.hasMesh() && IsKeyPressed(KEY_N)) {
    insp.initEmptyMesh();
  }

  // Cycle primitive type with [/].
  if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
    m_kind = static_cast<Kind>(
        (static_cast<int>(m_kind) + 1) % static_cast<int>(Kind::Count));
    if (m_focus >= fieldCount(m_kind)) m_focus = fieldCount(m_kind) - 1;
  }
  if (IsKeyPressed(KEY_LEFT_BRACKET)) {
    m_kind = static_cast<Kind>(
        (static_cast<int>(m_kind) + static_cast<int>(Kind::Count) - 1) %
        static_cast<int>(Kind::Count));
    if (m_focus >= fieldCount(m_kind)) m_focus = fieldCount(m_kind) - 1;
  }

  // Cycle focused field with ./,
  int nFields = fieldCount(m_kind);
  if (IsKeyPressed(KEY_PERIOD)) m_focus = (m_focus + 1) % nFields;
  if (IsKeyPressed(KEY_COMMA))
    m_focus = (m_focus + nFields - 1) % nFields;

  // ↑/↓ adjusts the focused field. Shift = coarse step.
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
  bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);
  if (up || down) adjustField(m_focus, up, shift);

  // Enter — insert (and bootstrap mesh if needed).
  if (IsKeyPressed(KEY_ENTER)) {
    if (!insp.hasMesh()) insp.initEmptyMesh();
    insert(insp);
  }
}

// ====================================================================
// 3D preview — outline of the primitive at the current center, sized
// to the current params. Drawn whether or not a mesh exists so the
// user sees what they're about to insert.
// ====================================================================
void PrimitivesTool::render3D(const Inspector & /*insp*/) const {
  Color preview{255, 220, 80, 220};
  Vector3 c = m_center;
  switch (m_kind) {
  case Kind::Cube:
    DrawCubeWires(c, m_size, m_size, m_size, preview);
    break;
  case Kind::Plane: {
    float h = m_size * 0.5f;
    Vector3 a{c.x - h, c.y, c.z - h};
    Vector3 b{c.x + h, c.y, c.z - h};
    Vector3 d{c.x + h, c.y, c.z + h};
    Vector3 e{c.x - h, c.y, c.z + h};
    DrawLine3D(a, b, preview); DrawLine3D(b, d, preview);
    DrawLine3D(d, e, preview); DrawLine3D(e, a, preview);
    break;
  }
  case Kind::Cylinder: {
    float halfH = m_height * 0.5f;
    Vector3 top{c.x, c.y + halfH, c.z};
    Vector3 bot{c.x, c.y - halfH, c.z};
    DrawCircle3D(top, m_radius, {1, 0, 0}, 90.0f, preview);
    DrawCircle3D(bot, m_radius, {1, 0, 0}, 90.0f, preview);
    // Four vertical edges as visual hint.
    for (int i = 0; i < 4; ++i) {
      float a = (i * kTwoPI) / 4.0f;
      Vector3 t{c.x + cosf(a) * m_radius, c.y + halfH, c.z + sinf(a) * m_radius};
      Vector3 b{c.x + cosf(a) * m_radius, c.y - halfH, c.z + sinf(a) * m_radius};
      DrawLine3D(t, b, preview);
    }
    break;
  }
  case Kind::Cone: {
    float halfH = m_height * 0.5f;
    Vector3 apex{c.x, c.y + halfH, c.z};
    Vector3 baseC{c.x, c.y - halfH, c.z};
    DrawCircle3D(baseC, m_radius, {1, 0, 0}, 90.0f, preview);
    for (int i = 0; i < 4; ++i) {
      float a = (i * kTwoPI) / 4.0f;
      Vector3 b{c.x + cosf(a) * m_radius, c.y - halfH, c.z + sinf(a) * m_radius};
      DrawLine3D(apex, b, preview);
    }
    break;
  }
  case Kind::Sphere:
    DrawSphereWires(c, m_radius, 6, 8, preview);
    break;
  default: break;
  }
}

// ====================================================================
// HUD
// ====================================================================
void PrimitivesTool::renderHud(const Inspector &insp, int &yCursor) const {
  char buf[256];
  static const char *kKindName[] = {"Cube", "Plane", "Cylinder", "Cone",
                                    "Sphere"};
  drawText("primitives ([/] cycle type  |  ./, field  |  ↑/↓ adjust  |  "
           "Enter inserts)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  if (!insp.hasMesh()) {
    drawText("(no mesh — press N to start a fresh empty one, then Enter "
             "to insert)",
             14, yCursor, 14, {200, 180, 140, 240});
    yCursor += 20;
  }

  std::snprintf(buf, sizeof(buf), "type     = %s",
                kKindName[static_cast<int>(m_kind)]);
  drawText(buf, 14, yCursor, 14, {200, 220, 240, 240});
  yCursor += 18;

  int nFields = fieldCount(m_kind);
  for (int i = 0; i < nFields; ++i) {
    bool focused = (i == m_focus);
    Color col = focused ? Color{255, 220, 80, 255}
                        : Color{220, 230, 250, 230};
    const char *label = fieldLabel(m_kind, i);
    if (i >= 0 && i <= 2) {
      float v = (i == 0) ? m_center.x : (i == 1) ? m_center.y : m_center.z;
      std::snprintf(buf, sizeof(buf), "%s %-8s = %.3f",
                    focused ? "►" : "  ", label, v);
    } else if (m_kind == Kind::Cube || m_kind == Kind::Plane) {
      std::snprintf(buf, sizeof(buf), "%s %-8s = %.3f",
                    focused ? "►" : "  ", label, m_size);
    } else if (m_kind == Kind::Cylinder || m_kind == Kind::Cone) {
      if (i == 3) std::snprintf(buf, sizeof(buf), "%s %-8s = %.3f",
                                focused ? "►" : "  ", label, m_radius);
      else if (i == 4) std::snprintf(buf, sizeof(buf), "%s %-8s = %.3f",
                                     focused ? "►" : "  ", label, m_height);
      else std::snprintf(buf, sizeof(buf), "%s %-8s = %d",
                         focused ? "►" : "  ", label, m_segments);
    } else if (m_kind == Kind::Sphere) {
      if (i == 3) std::snprintf(buf, sizeof(buf), "%s %-8s = %.3f",
                                focused ? "►" : "  ", label, m_radius);
      else std::snprintf(buf, sizeof(buf), "%s %-8s = %d",
                         focused ? "►" : "  ", label, m_subdivisions);
    } else {
      std::snprintf(buf, sizeof(buf), "%s %-8s", focused ? "►" : "  ", label);
    }
    drawText(buf, 14, yCursor, 14, col);
    yCursor += 18;
  }
}

void PrimitivesTool::onReload(Inspector & /*insp*/) {
  m_focus = 0;
  // Geometry params kept across reloads — they're the user's
  // authoring preference, not per-mesh state.
}

// ====================================================================
// Insertion entry point
// ====================================================================
void PrimitivesTool::insert(Inspector &insp) {
  insp.pushUndo();
  switch (m_kind) {
  case Kind::Cube:     emitCube(insp); break;
  case Kind::Plane:    emitPlane(insp); break;
  case Kind::Cylinder: emitCylinder(insp); break;
  case Kind::Cone:     emitCone(insp); break;
  case Kind::Sphere:   emitSphere(insp); break;
  default: break;
  }
  insp.rebuildModel();
}

// ====================================================================
// Cube — 8 verts, 12 tris (6 faces × 2). Outward winding.
// ====================================================================
void PrimitivesTool::emitCube(Inspector &insp) {
  Mesh3D &m = insp.mesh();
  size_t base = m.vertices.size();
  float h = m_size * 0.5f;
  Vector3 c = m_center;
  // 8 corners — front face (z = +h) first, back face (z = -h) second.
  Vector3 corners[8] = {
      {c.x - h, c.y - h, c.z + h}, {c.x + h, c.y - h, c.z + h},
      {c.x + h, c.y + h, c.z + h}, {c.x - h, c.y + h, c.z + h},
      {c.x - h, c.y - h, c.z - h}, {c.x + h, c.y - h, c.z - h},
      {c.x + h, c.y + h, c.z - h}, {c.x - h, c.y + h, c.z - h},
  };
  for (auto &v : corners) m.vertices.push_back(v);
  // 6 faces × 2 tris. CCW winding when viewed from outside.
  // Front (+Z)
  pushTri(m, base, 0, 1, 2); pushTri(m, base, 0, 2, 3);
  // Back (-Z)
  pushTri(m, base, 5, 4, 7); pushTri(m, base, 5, 7, 6);
  // Right (+X)
  pushTri(m, base, 1, 5, 6); pushTri(m, base, 1, 6, 2);
  // Left (-X)
  pushTri(m, base, 4, 0, 3); pushTri(m, base, 4, 3, 7);
  // Top (+Y)
  pushTri(m, base, 3, 2, 6); pushTri(m, base, 3, 6, 7);
  // Bottom (-Y)
  pushTri(m, base, 4, 5, 1); pushTri(m, base, 4, 1, 0);
}

// ====================================================================
// Plane — 4 verts, 2 tris. Lies on XZ plane (Y = center.y).
// ====================================================================
void PrimitivesTool::emitPlane(Inspector &insp) {
  Mesh3D &m = insp.mesh();
  size_t base = m.vertices.size();
  float h = m_size * 0.5f;
  Vector3 c = m_center;
  m.vertices.push_back({c.x - h, c.y, c.z - h});
  m.vertices.push_back({c.x + h, c.y, c.z - h});
  m.vertices.push_back({c.x + h, c.y, c.z + h});
  m.vertices.push_back({c.x - h, c.y, c.z + h});
  // Up-facing (+Y normal) winding.
  pushTri(m, base, 0, 2, 1);
  pushTri(m, base, 0, 3, 2);
}

// ====================================================================
// Cylinder — N segments. 2*N side verts (one ring top, one bot) + 2
// cap centers = 2N + 2 verts. N quads (= 2N tris) on side + 2 fans of
// N tris on each cap = 4N tris.
// ====================================================================
void PrimitivesTool::emitCylinder(Inspector &insp) {
  Mesh3D &m = insp.mesh();
  size_t base = m.vertices.size();
  int N = m_segments;
  float halfH = m_height * 0.5f;
  Vector3 c = m_center;

  // top ring (0..N-1), bot ring (N..2N-1), top center (2N), bot center (2N+1).
  for (int i = 0; i < N; ++i) {
    float a = (i * kTwoPI) / N;
    m.vertices.push_back({c.x + cosf(a) * m_radius, c.y + halfH,
                          c.z + sinf(a) * m_radius});
  }
  for (int i = 0; i < N; ++i) {
    float a = (i * kTwoPI) / N;
    m.vertices.push_back({c.x + cosf(a) * m_radius, c.y - halfH,
                          c.z + sinf(a) * m_radius});
  }
  int32_t topCenter = N * 2;
  int32_t botCenter = N * 2 + 1;
  m.vertices.push_back({c.x, c.y + halfH, c.z});
  m.vertices.push_back({c.x, c.y - halfH, c.z});

  // Sides — outward-facing.
  for (int i = 0; i < N; ++i) {
    int32_t t0 = i;
    int32_t t1 = (i + 1) % N;
    int32_t b0 = N + i;
    int32_t b1 = N + ((i + 1) % N);
    pushTri(m, base, b0, b1, t1);
    pushTri(m, base, b0, t1, t0);
  }
  // Top cap — fan from top center, +Y normal.
  for (int i = 0; i < N; ++i) {
    int32_t a = i;
    int32_t b = (i + 1) % N;
    pushTri(m, base, topCenter, a, b);
  }
  // Bottom cap — fan from bot center, -Y normal (opposite winding).
  for (int i = 0; i < N; ++i) {
    int32_t a = N + i;
    int32_t b = N + ((i + 1) % N);
    pushTri(m, base, botCenter, b, a);
  }
}

// ====================================================================
// Cone — N segments. Apex + N base ring verts + 1 base center =
// N + 2 verts. N side tris (from apex) + N base tris = 2N tris.
// ====================================================================
void PrimitivesTool::emitCone(Inspector &insp) {
  Mesh3D &m = insp.mesh();
  size_t base = m.vertices.size();
  int N = m_segments;
  float halfH = m_height * 0.5f;
  Vector3 c = m_center;

  int32_t apex = 0;
  m.vertices.push_back({c.x, c.y + halfH, c.z});
  // ring 1..N
  for (int i = 0; i < N; ++i) {
    float a = (i * kTwoPI) / N;
    m.vertices.push_back({c.x + cosf(a) * m_radius, c.y - halfH,
                          c.z + sinf(a) * m_radius});
  }
  int32_t baseCenter = N + 1;
  m.vertices.push_back({c.x, c.y - halfH, c.z});

  // Sides — apex to ring i to ring i+1.
  for (int i = 0; i < N; ++i) {
    int32_t a = 1 + i;
    int32_t b = 1 + ((i + 1) % N);
    pushTri(m, base, apex, b, a);
  }
  // Base cap — fan, -Y normal.
  for (int i = 0; i < N; ++i) {
    int32_t a = 1 + i;
    int32_t b = 1 + ((i + 1) % N);
    pushTri(m, base, baseCenter, b, a);
  }
}

// ====================================================================
// Icosphere — start from an icosahedron (12v, 20t) and subdivide. Each
// subdivision step splits every triangle into 4 by edge-midpoints,
// then projects the new midpoints onto the sphere surface. Keeps the
// vertex spread roughly uniform (vs UV-sphere poles).
//
// subdivisions caps at 3 (642v / 1280t) — beyond that flat-shaded
// faceting starts to look like a smooth sphere, defeating the look.
// ====================================================================
void PrimitivesTool::emitSphere(Inspector &insp) {
  Mesh3D &m = insp.mesh();
  size_t base = m.vertices.size();
  Vector3 c = m_center;
  float r = m_radius;

  // Icosahedron golden-ratio constants.
  const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
  // 12 base vertices (normalised to unit sphere; scaled + offset later).
  std::vector<Vector3> verts = {
      {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
      { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
      { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
  };
  for (auto &v : verts) {
    float L = Vector3Length(v);
    v = Vector3Scale(v, 1.0f / L);
  }
  // 20 triangles.
  std::vector<std::array<int, 3>> tris = {
      {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
      {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
      {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
  };

  // Subdivide. midCache dedupes the new midpoint vertices so adjacent
  // triangles share them (otherwise we'd double the vertex count
  // unnecessarily and break the loader's face-normal recomputation
  // expectations).
  auto subdivide = [&]() {
    std::vector<std::array<int, 3>> next;
    next.reserve(tris.size() * 4);
    // Edge key → mid-vertex index.
    std::vector<std::pair<uint64_t, int>> midCache;
    auto edgeKey = [](int a, int b) -> uint64_t {
      uint64_t lo = std::min(a, b), hi = std::max(a, b);
      return (hi << 32) | lo;
    };
    auto getMid = [&](int a, int b) -> int {
      uint64_t k = edgeKey(a, b);
      for (auto &p : midCache) if (p.first == k) return p.second;
      Vector3 m_ = Vector3Add(verts[a], verts[b]);
      float L = Vector3Length(m_);
      if (L > 1e-6f) m_ = Vector3Scale(m_, 1.0f / L);
      int idx = static_cast<int>(verts.size());
      verts.push_back(m_);
      midCache.push_back({k, idx});
      return idx;
    };
    for (auto &tri : tris) {
      int a = tri[0], b = tri[1], cc = tri[2];
      int ab = getMid(a, b), bc = getMid(b, cc), ca = getMid(cc, a);
      next.push_back({a, ab, ca});
      next.push_back({b, bc, ab});
      next.push_back({cc, ca, bc});
      next.push_back({ab, bc, ca});
    }
    tris = std::move(next);
  };
  for (int i = 0; i < m_subdivisions; ++i) subdivide();

  // Emit into the mesh. Scale + offset to (center, radius).
  for (auto &v : verts) m.vertices.push_back({c.x + v.x * r,
                                              c.y + v.y * r,
                                              c.z + v.z * r});
  for (auto &tri : tris) pushTri(m, base, tri[0], tri[1], tri[2]);
}

} // namespace tsmesh
