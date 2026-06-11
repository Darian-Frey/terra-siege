#include "TopologyTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/ObjLoader.hpp"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cstdio>

namespace tsmesh {

namespace {

// Vertex pick threshold — how far (in screen pixels) the cursor can
// be from a vert sphere's projected centre and still count as a hit.
constexpr float kVertPickPx = 14.0f;

} // anonymous namespace

// ====================================================================
// Picks
// ====================================================================
int TopologyTool::pickVertex(const Inspector &insp) const {
  Vector2 mouse = GetMousePosition();
  const auto &verts = insp.mesh().vertices;
  int best = -1;
  float bestPx2 = kVertPickPx * kVertPickPx;
  for (size_t i = 0; i < verts.size(); ++i) {
    Vector2 sp = GetWorldToScreen(verts[i], insp.camera());
    float dx = sp.x - mouse.x;
    float dy = sp.y - mouse.y;
    float d2 = dx * dx + dy * dy;
    if (d2 < bestPx2) {
      bestPx2 = d2;
      best = static_cast<int>(i);
    }
  }
  return best;
}

int TopologyTool::pickFace(const Inspector &insp) const {
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());
  const auto &m = insp.mesh();
  size_t triCount = m.indices.size() / 3;
  int best = -1;
  float bestDist = 1e30f;
  for (size_t t = 0; t < triCount; ++t) {
    Vector3 a = m.vertices[m.indices[t * 3 + 0]];
    Vector3 b = m.vertices[m.indices[t * 3 + 1]];
    Vector3 c = m.vertices[m.indices[t * 3 + 2]];
    RayCollision hit = GetRayCollisionTriangle(ray, a, b, c);
    if (hit.hit && hit.distance < bestDist) {
      bestDist = hit.distance;
      best = static_cast<int>(t);
    }
  }
  return best;
}

Vector3 TopologyTool::cursorOnCameraPlane(const Inspector &insp) const {
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());
  // Plane: y = m_camTarget.y (the inspector's orbit pivot). For a
  // typical mesh centred around origin, this puts the new vertex on
  // the y=0 plane near the camera focus.
  float planeY = insp.camera().target.y;
  // Ray-plane intersection. Guard the degenerate near-zero divide.
  if (std::abs(ray.direction.y) < 1e-5f) {
    // Looking dead horizontal — fall back to camera target distance.
    float d = Vector3Distance(insp.camera().position, insp.camera().target);
    return Vector3Add(ray.position, Vector3Scale(ray.direction, d));
  }
  float t = (planeY - ray.position.y) / ray.direction.y;
  if (t < 0) {
    // Plane is behind us — fall back to camera target distance.
    float d = Vector3Distance(insp.camera().position, insp.camera().target);
    return Vector3Add(ray.position, Vector3Scale(ray.direction, d));
  }
  return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

// ====================================================================
// Mutations
// ====================================================================
void TopologyTool::addVertex(Inspector &insp, Vector3 pos) {
  // First-press seed: if there's no mesh yet, bootstrap so we have
  // somewhere to land the vertex.
  if (!insp.hasMesh()) insp.initEmptyMesh();
  insp.pushUndo();
  insp.mesh().vertices.push_back(pos);
  insp.rebuildModel();
  m_dirty = true;
  // Add the new vertex to the selection so the user can immediately
  // press F to build a face if it's the 3rd.
  m_vertSel.push_back(static_cast<int>(insp.mesh().vertices.size()) - 1);
}

void TopologyTool::deleteSelectedVertex(Inspector &insp) {
  if (m_vertSel.size() != 1) return;
  int victim = m_vertSel[0];
  if (victim < 0 || victim >= static_cast<int>(insp.mesh().vertices.size()))
    return;
  insp.pushUndo();
  auto &mesh = insp.mesh();

  // Walk the index/normal/palette arrays and drop any triangle that
  // references the victim. Then re-number remaining indices to close
  // the gap left by erasing the victim from the vertex array.
  std::vector<int32_t> newIndices;
  std::vector<Vector3> newNormals;
  std::vector<int> newPalette;
  newIndices.reserve(mesh.indices.size());
  newNormals.reserve(mesh.faceNormals.size());
  newPalette.reserve(mesh.facePalette.size());
  size_t triCount = mesh.indices.size() / 3;
  for (size_t t = 0; t < triCount; ++t) {
    int32_t a = mesh.indices[t * 3 + 0];
    int32_t b = mesh.indices[t * 3 + 1];
    int32_t c = mesh.indices[t * 3 + 2];
    if (a == victim || b == victim || c == victim) continue;
    if (a > victim) --a;
    if (b > victim) --b;
    if (c > victim) --c;
    newIndices.push_back(a);
    newIndices.push_back(b);
    newIndices.push_back(c);
    newNormals.push_back(mesh.faceNormals[t]);
    newPalette.push_back(mesh.facePalette[t]);
  }
  mesh.indices = std::move(newIndices);
  mesh.faceNormals = std::move(newNormals);
  mesh.facePalette = std::move(newPalette);
  mesh.vertices.erase(mesh.vertices.begin() + victim);

  insp.rebuildModel();
  m_dirty = true;
  m_vertSel.clear();
  m_faceSel = -1;
}

void TopologyTool::addFaceFromSelection(Inspector &insp) {
  if (m_vertSel.size() != 3) return;
  int a = m_vertSel[0];
  int b = m_vertSel[1];
  int c = m_vertSel[2];
  auto &mesh = insp.mesh();
  int nv = static_cast<int>(mesh.vertices.size());
  if (a < 0 || a >= nv || b < 0 || b >= nv || c < 0 || c >= nv) return;
  if (a == b || b == c || a == c) return; // degenerate
  insp.pushUndo();
  mesh.indices.push_back(a);
  mesh.indices.push_back(b);
  mesh.indices.push_back(c);
  Vector3 e1 = Vector3Subtract(mesh.vertices[b], mesh.vertices[a]);
  Vector3 e2 = Vector3Subtract(mesh.vertices[c], mesh.vertices[a]);
  Vector3 n = Vector3CrossProduct(e1, e2);
  float L = Vector3Length(n);
  if (L > 1e-6f) n = Vector3Scale(n, 1.0f / L);
  else n = Vector3{0, 1, 0};
  mesh.faceNormals.push_back(n);
  mesh.facePalette.push_back(0); // first palette slot
  insp.rebuildModel();
  m_dirty = true;
  m_vertSel.clear();
}

void TopologyTool::deleteSelectedFace(Inspector &insp) {
  if (m_faceSel < 0) return;
  auto &mesh = insp.mesh();
  size_t triCount = mesh.indices.size() / 3;
  if (m_faceSel >= static_cast<int>(triCount)) return;
  insp.pushUndo();
  mesh.indices.erase(mesh.indices.begin() + m_faceSel * 3,
                     mesh.indices.begin() + m_faceSel * 3 + 3);
  mesh.faceNormals.erase(mesh.faceNormals.begin() + m_faceSel);
  mesh.facePalette.erase(mesh.facePalette.begin() + m_faceSel);
  insp.rebuildModel();
  m_dirty = true;
  m_faceSel = -1;
}

void TopologyTool::flipSelectedFaceWinding(Inspector &insp) {
  if (m_faceSel < 0) return;
  auto &mesh = insp.mesh();
  size_t triCount = mesh.indices.size() / 3;
  if (m_faceSel >= static_cast<int>(triCount)) return;
  insp.pushUndo();
  // Swap b and c (indices[t*3+1] and [t*3+2]) to flip winding.
  std::swap(mesh.indices[m_faceSel * 3 + 1],
            mesh.indices[m_faceSel * 3 + 2]);
  // Recompute the face normal from the new winding.
  Vector3 a = mesh.vertices[mesh.indices[m_faceSel * 3 + 0]];
  Vector3 b = mesh.vertices[mesh.indices[m_faceSel * 3 + 1]];
  Vector3 c = mesh.vertices[mesh.indices[m_faceSel * 3 + 2]];
  Vector3 e1 = Vector3Subtract(b, a);
  Vector3 e2 = Vector3Subtract(c, a);
  Vector3 n = Vector3CrossProduct(e1, e2);
  float L = Vector3Length(n);
  mesh.faceNormals[m_faceSel] =
      (L > 1e-6f) ? Vector3Scale(n, 1.0f / L) : Vector3{0, 1, 0};
  insp.rebuildModel();
  m_dirty = true;
}

void TopologyTool::recomputeAllNormals(Inspector &insp) {
  auto &mesh = insp.mesh();
  size_t triCount = mesh.indices.size() / 3;
  if (mesh.faceNormals.size() != triCount)
    mesh.faceNormals.assign(triCount, Vector3{0, 1, 0});
  insp.pushUndo();
  for (size_t t = 0; t < triCount; ++t) {
    Vector3 a = mesh.vertices[mesh.indices[t * 3 + 0]];
    Vector3 b = mesh.vertices[mesh.indices[t * 3 + 1]];
    Vector3 c = mesh.vertices[mesh.indices[t * 3 + 2]];
    Vector3 e1 = Vector3Subtract(b, a);
    Vector3 e2 = Vector3Subtract(c, a);
    Vector3 n = Vector3CrossProduct(e1, e2);
    float L = Vector3Length(n);
    mesh.faceNormals[t] =
        (L > 1e-6f) ? Vector3Scale(n, 1.0f / L) : Vector3{0, 1, 0};
  }
  insp.rebuildModel();
  m_dirty = true;
}

// ====================================================================
// Input
// ====================================================================
void TopologyTool::handleInput(Inspector &insp) {
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

  // ---- Hover updates ----
  m_hoverVert = insp.hasMesh() ? pickVertex(insp) : -1;
  // Only check faces if no vertex is under the cursor — vertex picks win.
  m_hoverFace = (insp.hasMesh() && m_hoverVert < 0) ? pickFace(insp) : -1;

  // ---- V: add vertex at cursor ----
  if (IsKeyPressed(KEY_V)) {
    Vector3 pos = cursorOnCameraPlane(insp);
    addVertex(insp, pos);
    return;
  }

  // ---- F: face build from 3-vert selection (Shift+F = flip winding) ----
  if (IsKeyPressed(KEY_F)) {
    if (shift) flipSelectedFaceWinding(insp);
    else addFaceFromSelection(insp);
    return;
  }

  // ---- N: recompute normals ----
  if (IsKeyPressed(KEY_N) && insp.hasMesh()) {
    recomputeAllNormals(insp);
    return;
  }

  // ---- Del: delete selected face OR vertex ----
  if (IsKeyPressed(KEY_DELETE) ||
      (IsKeyPressed(KEY_BACKSPACE) && !shift)) {
    if (m_faceSel >= 0) deleteSelectedFace(insp);
    else if (m_vertSel.size() == 1) deleteSelectedVertex(insp);
    return;
  }

  // ---- ESC: clear selections ----
  if (IsKeyPressed(KEY_ESCAPE)) {
    m_vertSel.clear();
    m_faceSel = -1;
    return;
  }

  // ---- Mouse: vertex or face click ----
  if (insp.hasMesh() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    if (m_hoverVert >= 0) {
      // Toggle into ordered vert selection.
      auto it = std::find(m_vertSel.begin(), m_vertSel.end(), m_hoverVert);
      if (it != m_vertSel.end()) m_vertSel.erase(it);
      else m_vertSel.push_back(m_hoverVert);
      m_faceSel = -1; // vert selection clears face selection
    } else if (m_hoverFace >= 0) {
      m_faceSel = m_hoverFace;
      m_vertSel.clear();
    } else {
      // Clicked empty space — clear both.
      m_vertSel.clear();
      m_faceSel = -1;
    }
  }
}

// ====================================================================
// 3D render
// ====================================================================
void TopologyTool::render3D(const Inspector &insp) const {
  if (!insp.hasMesh()) return;
  const auto &m = insp.mesh();
  float sphR = insp.vertexSphereRadius();

  // Draw every vertex as a small sphere — gives the user something
  // to click. Picking ignores this (we use screen-space distance).
  for (size_t i = 0; i < m.vertices.size(); ++i) {
    Color col{200, 220, 240, 220};
    if (static_cast<int>(i) == m_hoverVert) col = Color{255, 255, 255, 240};
    DrawSphere(m.vertices[i], sphR * 0.6f, col);
  }
  // Selected verts: bright yellow + selection-order number rendered in HUD.
  for (int idx : m_vertSel) {
    if (idx < 0 || idx >= static_cast<int>(m.vertices.size())) continue;
    DrawSphere(m.vertices[idx], sphR, Color{255, 220, 60, 240});
  }
  // Hover face: cyan outline.
  if (m_hoverFace >= 0 &&
      m_hoverFace * 3 + 2 < static_cast<int>(m.indices.size())) {
    Color hoverCol{120, 220, 255, 220};
    Vector3 a = m.vertices[m.indices[m_hoverFace * 3 + 0]];
    Vector3 b = m.vertices[m.indices[m_hoverFace * 3 + 1]];
    Vector3 c = m.vertices[m.indices[m_hoverFace * 3 + 2]];
    DrawLine3D(a, b, hoverCol);
    DrawLine3D(b, c, hoverCol);
    DrawLine3D(c, a, hoverCol);
  }
  // Selected face: green outline + small normal hint.
  if (m_faceSel >= 0 &&
      m_faceSel * 3 + 2 < static_cast<int>(m.indices.size())) {
    Color selCol{80, 240, 120, 240};
    Vector3 a = m.vertices[m.indices[m_faceSel * 3 + 0]];
    Vector3 b = m.vertices[m.indices[m_faceSel * 3 + 1]];
    Vector3 c = m.vertices[m.indices[m_faceSel * 3 + 2]];
    DrawLine3D(a, b, selCol);
    DrawLine3D(b, c, selCol);
    DrawLine3D(c, a, selCol);
    // Normal arrow — centroid + small jab outward.
    Vector3 mid = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
    Vector3 nrm = (m_faceSel < static_cast<int>(m.faceNormals.size()))
                      ? m.faceNormals[m_faceSel]
                      : Vector3{0, 1, 0};
    Vector3 tip = Vector3Add(mid, Vector3Scale(nrm, sphR * 6.0f));
    DrawLine3D(mid, tip, Color{80, 240, 120, 220});
  }

  // Cursor preview: where would V place a new vertex?
  Vector3 ghostPos = cursorOnCameraPlane(insp);
  DrawSphere(ghostPos, sphR * 0.45f, Color{255, 180, 60, 140});
}

// ====================================================================
// HUD
// ====================================================================
void TopologyTool::renderHud(const Inspector &insp, int &yCursor) const {
  char buf[256];
  drawText("topology (V add vert  |  click verts then F = face  |  "
           "Shift+F flip  |  Del delete  |  N normals  |  ESC clear)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  if (!insp.hasMesh()) {
    drawText("(no mesh — press V to drop the first vertex)", 14, yCursor,
             14, {200, 180, 140, 240});
    yCursor += 20;
  } else {
    std::snprintf(buf, sizeof(buf),
                  "verts %zu  |  tris %zu  |  selected verts %zu  |  "
                  "selected face %s",
                  insp.mesh().vertices.size(),
                  insp.mesh().indices.size() / 3, m_vertSel.size(),
                  m_faceSel >= 0 ? "yes" : "—");
    drawText(buf, 14, yCursor, 14, {220, 230, 250, 240});
    yCursor += 18;
  }

  // Selection-order list — shows the user which order their picks
  // happened so they can predict the winding F will produce.
  for (size_t i = 0; i < m_vertSel.size() && i < 6; ++i) {
    std::snprintf(buf, sizeof(buf), "  %zu: v%d", i + 1, m_vertSel[i]);
    drawText(buf, 14, yCursor, 13, {255, 220, 60, 240});
    yCursor += 16;
  }
  if (m_vertSel.size() == 3) {
    drawText("  → press F to build face (CCW)", 14, yCursor, 13,
             {120, 220, 160, 240});
    yCursor += 16;
  } else if (m_vertSel.size() > 3) {
    drawText("  too many — only 3 verts can make a triangle", 14, yCursor,
             13, {240, 150, 120, 240});
    yCursor += 16;
  }
}

// ====================================================================
// Save / reload
// ====================================================================
bool TopologyTool::save(Inspector &insp) {
  if (!saveObjMesh(insp.path(), insp.mesh().vertices, insp.mesh().indices,
                   insp.mesh().facePalette, m_dirty)) {
    std::fprintf(stderr, "[TopologyTool] save failed\n");
    return false;
  }
  m_dirty = false;
  return true;
}

void TopologyTool::onReload(Inspector & /*insp*/) {
  m_vertSel.clear();
  m_faceSel = -1;
  m_hoverVert = -1;
  m_hoverFace = -1;
  m_dirty = false;
}

} // namespace tsmesh
