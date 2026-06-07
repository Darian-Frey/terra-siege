#include "VertexTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/ObjLoader.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tsmesh {

namespace {

// Closest-approach `s` along (lineOrigin + s*lineDir) to the mouse ray.
// Reused unchanged from the original VertexTool — axis-locked drag
// projects the cursor onto an axis line through the anchor's drag-
// start position; `s` is the signed distance along that axis.
float rayLineClosestApproach(Vector3 rayOrigin, Vector3 rayDir,
                             Vector3 lineOrigin, Vector3 lineDir) {
  Vector3 u = Vector3Subtract(rayOrigin, lineOrigin);
  float a = Vector3DotProduct(rayDir, rayDir);
  float b = Vector3DotProduct(rayDir, lineDir);
  float c = Vector3DotProduct(lineDir, lineDir);
  float d = Vector3DotProduct(rayDir, u);
  float e = Vector3DotProduct(lineDir, u);
  float denom = a * c - b * b;
  if (fabsf(denom) < 1e-6f) return 0.0f;
  return (a * e - b * d) / denom;
}

// Intersect the mouse ray with the camera-perpendicular plane through
// planePoint. Used for free (unconstrained) drag — vertex slides on
// that plane so there's no depth ambiguity.
Vector3 rayCameraPlaneHit(Ray ray, Vector3 planePoint, Vector3 camForward) {
  float denom = Vector3DotProduct(ray.direction, camForward);
  if (fabsf(denom) < 1e-6f) return planePoint;
  float t = Vector3DotProduct(Vector3Subtract(planePoint, ray.position),
                              camForward) /
            denom;
  return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

bool ctrlDown() {
  return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}
bool shiftDown() {
  return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

} // anonymous namespace

constexpr float VertexTool::kSnapSteps[5];

// ====================================================================
// Selection helpers
// ====================================================================
bool VertexTool::selectionContains(int idx) const {
  return std::find(m_selection.begin(), m_selection.end(), idx) !=
         m_selection.end();
}

void VertexTool::selectionAdd(int idx) {
  if (idx < 0) return;
  if (!selectionContains(idx)) m_selection.push_back(idx);
}

void VertexTool::selectionToggle(int idx) {
  if (idx < 0) return;
  auto it = std::find(m_selection.begin(), m_selection.end(), idx);
  if (it == m_selection.end()) m_selection.push_back(idx);
  else m_selection.erase(it);
}

void VertexTool::selectionClear() { m_selection.clear(); }

// ====================================================================
// Pick / project helpers
// ====================================================================
int VertexTool::pickVertexUnderMouse(const Inspector &insp) const {
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());
  float bestT = 1e9f;
  int bestIdx = -1;
  const auto &verts = insp.mesh().vertices;
  float r = insp.vertexSphereRadius();
  for (size_t i = 0; i < verts.size(); ++i) {
    RayCollision hit = GetRayCollisionSphere(ray, verts[i], r);
    if (hit.hit && hit.distance < bestT) {
      bestT = hit.distance;
      bestIdx = static_cast<int>(i);
    }
  }
  return bestIdx;
}

Vector2 VertexTool::projectToScreen(Vector3 worldPos,
                                    const Inspector &insp) const {
  return GetWorldToScreen(worldPos, insp.camera());
}

// ====================================================================
// Snap helpers
// ====================================================================
Vector3 VertexTool::applySnapGrid(Vector3 pos) const {
  if (!m_snapEnabled) return pos;
  // Suspend snap while the user holds Shift mid-drag.
  if (shiftDown()) return pos;
  float step = kSnapSteps[m_snapStepIdx];
  auto snap = [step](float v) { return roundf(v / step) * step; };
  return Vector3{snap(pos.x), snap(pos.y), snap(pos.z)};
}

int VertexTool::snapVertexUnderCursor(const Inspector &insp) const {
  Vector2 mouse = GetMousePosition();
  const auto &verts = insp.mesh().vertices;
  int best = -1;
  float bestD2 = kVertexSnapPx * kVertexSnapPx;
  for (size_t i = 0; i < verts.size(); ++i) {
    // Skip vertices we're currently dragging — snapping onto one of
    // them would freeze the drag.
    if (selectionContains(static_cast<int>(i))) continue;
    Vector2 sp = projectToScreen(verts[i], insp);
    float dx = sp.x - mouse.x;
    float dy = sp.y - mouse.y;
    float d2 = dx * dx + dy * dy;
    if (d2 < bestD2) { bestD2 = d2; best = static_cast<int>(i); }
  }
  return best;
}

// ====================================================================
// Undo / redo
// ====================================================================
void VertexTool::pushUndoSnapshot(const Inspector &insp) {
  m_undoStack.push_back(insp.mesh().vertices);
  if (static_cast<int>(m_undoStack.size()) > kMaxHistory)
    m_undoStack.erase(m_undoStack.begin());
  // Any fresh edit invalidates the redo lineage.
  m_redoStack.clear();
}

void VertexTool::undo(Inspector &insp) {
  if (m_undoStack.empty()) return;
  m_redoStack.push_back(insp.mesh().vertices);
  if (static_cast<int>(m_redoStack.size()) > kMaxHistory)
    m_redoStack.erase(m_redoStack.begin());
  insp.mesh().vertices = std::move(m_undoStack.back());
  m_undoStack.pop_back();
  insp.rebuildModel();
  m_dirty = true;
  cancelNumericInput();
}

void VertexTool::redo(Inspector &insp) {
  if (m_redoStack.empty()) return;
  m_undoStack.push_back(insp.mesh().vertices);
  if (static_cast<int>(m_undoStack.size()) > kMaxHistory)
    m_undoStack.erase(m_undoStack.begin());
  insp.mesh().vertices = std::move(m_redoStack.back());
  m_redoStack.pop_back();
  insp.rebuildModel();
  m_dirty = true;
  cancelNumericInput();
}

void VertexTool::clearHistory() {
  m_undoStack.clear();
  m_redoStack.clear();
}

// ====================================================================
// Numeric input
// ====================================================================
void VertexTool::commitNumericInput(Inspector &insp) {
  if (m_inputAxis < 0 || m_selection.size() != 1) {
    cancelNumericInput();
    return;
  }
  if (m_inputBuf.empty()) { cancelNumericInput(); return; }
  char *endp = nullptr;
  float val = std::strtof(m_inputBuf.c_str(), &endp);
  if (endp == m_inputBuf.c_str()) { cancelNumericInput(); return; }
  pushUndoSnapshot(insp);
  Vector3 &v = insp.mesh().vertices[m_selection.front()];
  if (m_inputAxis == 0) v.x = val;
  else if (m_inputAxis == 1) v.y = val;
  else v.z = val;
  insp.rebuildModel();
  m_dirty = true;
  cancelNumericInput();
}

void VertexTool::cancelNumericInput() {
  m_inputAxis = -1;
  m_inputBuf.clear();
}

// ====================================================================
// Drag lifecycle
// ====================================================================
void VertexTool::beginDrag(Inspector &insp, int axisLock) {
  pushUndoSnapshot(insp);
  m_dragging = true;
  m_axisLock = axisLock;
  m_dragStartPositions.clear();
  m_dragStartPositions.reserve(m_selection.size());
  for (int idx : m_selection)
    m_dragStartPositions.push_back(insp.mesh().vertices[idx]);
  m_dragAnchorStart = insp.mesh().vertices[m_dragAnchor];
}

void VertexTool::updateDrag(Inspector &insp, int axisLock) {
  if (!m_dragging || m_dragAnchor < 0 ||
      m_dragStartPositions.size() != m_selection.size()) {
    return;
  }
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());

  // Compute the anchor's new desired position; subtract its start to
  // get the delta we apply to every selected vertex.
  Vector3 newAnchor;
  if (axisLock < 0) {
    Vector3 camFwd = Vector3Normalize(
        Vector3Subtract(insp.camera().target, insp.camera().position));
    newAnchor = rayCameraPlaneHit(ray, m_dragAnchorStart, camFwd);
  } else {
    Vector3 axis = {0, 0, 0};
    if (axisLock == 0) axis.x = 1;
    else if (axisLock == 1) axis.y = 1;
    else axis.z = 1;
    Vector3 rayDirN = Vector3Normalize(ray.direction);
    float s = rayLineClosestApproach(ray.position, rayDirN,
                                     m_dragAnchorStart, axis);
    newAnchor = Vector3Add(m_dragAnchorStart, Vector3Scale(axis, s));
  }

  // Snap-to-vertex (only when a single vertex is being dragged — the
  // multi-select case is ambiguous about which start to snap which
  // way). Overrides axis-lock when active.
  m_snapTargetIdx = -1;
  if (m_selection.size() == 1) {
    int snap = snapVertexUnderCursor(insp);
    if (snap >= 0) {
      newAnchor = insp.mesh().vertices[snap];
      m_snapTargetIdx = snap;
    }
  }

  // Snap-to-grid is applied to the ANCHOR's new position so the whole
  // selection translates by a snapped delta — keeps relative offsets.
  newAnchor = applySnapGrid(newAnchor);

  Vector3 delta = Vector3Subtract(newAnchor, m_dragAnchorStart);
  for (size_t i = 0; i < m_selection.size(); ++i) {
    insp.mesh().vertices[m_selection[i]] =
        Vector3Add(m_dragStartPositions[i], delta);
  }
  insp.rebuildModel();
  m_dirty = true;
}

void VertexTool::endDrag(Inspector & /*insp*/) {
  m_dragging = false;
  m_axisLock = -1;
  m_dragStartPositions.clear();
  m_dragAnchor = -1;
  m_snapTargetIdx = -1;
}

// ====================================================================
// Input
// ====================================================================
void VertexTool::handleInput(Inspector &insp) {
  // Ctrl+Z / Ctrl+Y — undo / redo. Done first so they work even mid-
  // drag (in case the user wants to bail).
  if (ctrlDown() && IsKeyPressed(KEY_Z)) { undo(insp); return; }
  if (ctrlDown() && IsKeyPressed(KEY_Y)) { redo(insp); return; }

  // Snap-step cycle: `[` smaller, `]` larger. Toggle snap with `G`.
  if (IsKeyPressed(KEY_LEFT_BRACKET) && m_snapStepIdx > 0) --m_snapStepIdx;
  if (IsKeyPressed(KEY_RIGHT_BRACKET) &&
      m_snapStepIdx < 4) ++m_snapStepIdx;
  if (IsKeyPressed(KEY_G)) m_snapEnabled = !m_snapEnabled;

  // ---- Numeric input mode (X/Y/Z while NOT dragging, one vert focused) ----
  if (m_inputAxis >= 0) {
    int c;
    while ((c = GetCharPressed()) > 0) {
      // Accept digits, sign, dot — small whitelist so X/Y/Z don't
      // sneak in as text.
      if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == '+')
        m_inputBuf.push_back(static_cast<char>(c));
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !m_inputBuf.empty())
      m_inputBuf.pop_back();
    if (IsKeyPressed(KEY_ENTER)) commitNumericInput(insp);
    if (IsKeyPressed(KEY_ESCAPE)) cancelNumericInput();
    return; // numeric mode owns the rest of input this frame
  }

  // ---- Axis lock during drag (X/Y/Z hold) ----
  int held = -1;
  if (IsKeyDown(KEY_X)) held = 0;
  else if (IsKeyDown(KEY_Y)) held = 1;
  else if (IsKeyDown(KEY_Z)) held = 2;

  // ---- Numeric input entry (X/Y/Z pressed while NOT dragging) ----
  // Pressed-not-down so we only fire once on the key transition; gate
  // on single-vertex selection so the input has a target.
  if (!m_dragging && m_selection.size() == 1) {
    int axis = -1;
    if (IsKeyPressed(KEY_X)) axis = 0;
    else if (IsKeyPressed(KEY_Y)) axis = 1;
    else if (IsKeyPressed(KEY_Z)) axis = 2;
    if (axis >= 0) {
      m_inputAxis = axis;
      m_inputBuf.clear();
      return;
    }
  }

  // ---- Hover detection (when not dragging) ----
  if (!m_dragging) {
    m_hover = pickVertexUnderMouse(insp);
  }

  // ---- LMB press ----
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    int picked = pickVertexUnderMouse(insp);
    if (picked >= 0) {
      // Hit a vertex — selection logic.
      if (shiftDown()) {
        selectionToggle(picked);
      } else if (!selectionContains(picked)) {
        selectionClear();
        selectionAdd(picked);
      }
      // Start drag if the picked vertex is in the selection.
      if (selectionContains(picked)) {
        m_dragAnchor = picked;
        beginDrag(insp, held);
      }
    } else {
      // Empty space.
      if (ctrlDown()) {
        // Start box-select.
        m_boxSelecting = true;
        m_boxStart = GetMousePosition();
      } else if (!shiftDown()) {
        selectionClear();
      }
    }
  }

  // ---- LMB held ----
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && m_dragging) {
    // Live update — pick up axis-lock changes mid-drag.
    int dragAxis = -1;
    if (IsKeyDown(KEY_X)) dragAxis = 0;
    else if (IsKeyDown(KEY_Y)) dragAxis = 1;
    else if (IsKeyDown(KEY_Z)) dragAxis = 2;
    m_axisLock = dragAxis;
    updateDrag(insp, dragAxis);
  }

  // ---- LMB release ----
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    if (m_dragging) endDrag(insp);
    if (m_boxSelecting) {
      Vector2 end = GetMousePosition();
      float minX = std::min(m_boxStart.x, end.x);
      float minY = std::min(m_boxStart.y, end.y);
      float maxX = std::max(m_boxStart.x, end.x);
      float maxY = std::max(m_boxStart.y, end.y);
      // Don't clear selection if Shift is held — additive box-select.
      if (!shiftDown()) selectionClear();
      const auto &verts = insp.mesh().vertices;
      for (size_t i = 0; i < verts.size(); ++i) {
        Vector2 sp = projectToScreen(verts[i], insp);
        if (sp.x >= minX && sp.x <= maxX &&
            sp.y >= minY && sp.y <= maxY) {
          selectionAdd(static_cast<int>(i));
        }
      }
      m_boxSelecting = false;
    }
  }
}

// ====================================================================
// 3D render
// ====================================================================
void VertexTool::render3D(const Inspector &insp) const {
  const auto &verts = insp.mesh().vertices;
  float r = insp.vertexSphereRadius();
  for (size_t i = 0; i < verts.size(); ++i) {
    bool selected = selectionContains(static_cast<int>(i));
    bool hovered = (static_cast<int>(i) == m_hover);
    Color c;
    if (selected) c = {255, 220, 80, 255};
    else if (hovered) c = {220, 240, 255, 250};
    else c = {180, 200, 220, 220};
    // Hover/selected get a slightly larger radius so they read at a
    // glance — pure colour change is hard to see at sphere sizes the
    // smallest meshes ship with.
    float sphereR = (selected || hovered) ? r * 1.4f : r;
    DrawSphereEx(verts[i], sphereR, 6, 8, c);
  }

  // Axis-lock line — drawn through the anchor's drag-start position.
  if (m_dragging && m_axisLock >= 0 && m_dragAnchor >= 0) {
    Vector3 v = m_dragAnchorStart;
    float A = insp.boundsRadius() * 1.6f;
    Vector3 a = v, b = v;
    if (m_axisLock == 0) { a.x -= A; b.x += A; }
    else if (m_axisLock == 1) { a.y -= A; b.y += A; }
    else                     { a.z -= A; b.z += A; }
    DrawLine3D(a, b, {255, 220, 80, 255});
  }

  // Snap-to-vertex target — bright ring around the destination vertex
  // so the user sees the snap before releasing.
  if (m_dragging && m_snapTargetIdx >= 0 &&
      m_snapTargetIdx < static_cast<int>(verts.size())) {
    DrawSphereWires(verts[m_snapTargetIdx], r * 2.2f, 4, 8,
                    {120, 220, 120, 255});
  }
}

// ====================================================================
// HUD
// ====================================================================
void VertexTool::renderHud(const Inspector &insp, int &yCursor) const {
  char buf[256];

  // ---- Stats overlay ----
  size_t faceCount = insp.mesh().indices.size() / 3;
  std::snprintf(buf, sizeof(buf), "verts: %zu  |  faces: %zu  |  selected: %zu",
                insp.mesh().vertices.size(), faceCount, m_selection.size());
  drawText(buf, 10, yCursor, 14, {200, 220, 240, 230});
  yCursor += 20;

  // ---- Snap + history badge ----
  std::snprintf(buf, sizeof(buf),
                "snap: %s @ %.2f  |  undo %zu / redo %zu",
                m_snapEnabled ? "ON" : "off",
                kSnapSteps[m_snapStepIdx],
                m_undoStack.size(), m_redoStack.size());
  drawText(buf, 10, yCursor, 13, {160, 180, 200, 220});
  yCursor += 18;

  // ---- Selection detail ----
  if (m_selection.size() == 1) {
    int idx = m_selection.front();
    Vector3 v = insp.mesh().vertices[idx];
    std::snprintf(buf, sizeof(buf), "v%d: (%.3f, %.3f, %.3f)",
                  idx, v.x, v.y, v.z);
    drawText(buf, 10, yCursor, 14, {220, 230, 250, 240});
    yCursor += 20;
  } else if (m_selection.size() > 1) {
    // Centroid + bounding-box extent for context.
    Vector3 sum{0, 0, 0};
    Vector3 mn{1e9f, 1e9f, 1e9f};
    Vector3 mx{-1e9f, -1e9f, -1e9f};
    for (int idx : m_selection) {
      Vector3 v = insp.mesh().vertices[idx];
      sum = Vector3Add(sum, v);
      mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
      mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
    }
    Vector3 c = Vector3Scale(sum, 1.0f / static_cast<float>(m_selection.size()));
    std::snprintf(buf, sizeof(buf),
                  "centroid: (%.3f, %.3f, %.3f)  size: %.3fx%.3fx%.3f",
                  c.x, c.y, c.z, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
    drawText(buf, 10, yCursor, 14, {220, 230, 250, 240});
    yCursor += 20;
  } else {
    drawText("click vertex to select  |  Shift+click adds  |  "
             "Ctrl+drag box-selects  |  G toggles snap",
             10, yCursor, 13, {160, 180, 200, 200});
    yCursor += 18;
  }

  // ---- Numeric input prompt ----
  if (m_inputAxis >= 0) {
    const char *axisName = (m_inputAxis == 0) ? "X"
                          : (m_inputAxis == 1) ? "Y" : "Z";
    std::snprintf(buf, sizeof(buf), "set %s = %s_   (Enter commits, Esc cancels)",
                  axisName, m_inputBuf.c_str());
    drawText(buf, 10, yCursor, 14, {255, 220, 80, 255});
    yCursor += 20;
  } else if (m_selection.size() == 1) {
    drawText("press X / Y / Z to type a new coordinate",
             10, yCursor, 12, {150, 170, 195, 210});
    yCursor += 16;
  }

  // ---- Drag axis-lock badge ----
  if (m_dragging && m_axisLock >= 0) {
    const char *axisName = (m_axisLock == 0) ? "X"
                          : (m_axisLock == 1) ? "Y" : "Z";
    std::snprintf(buf, sizeof(buf), "axis-lock: %s", axisName);
    drawText(buf, 10, yCursor, 14, {255, 220, 80, 240});
    yCursor += 20;
  }

  // ---- Box-select rectangle ----
  // Drawn here (inside HUD render path, screen-space) so the
  // translucent fill + outline overlay the 3D scene cleanly. Active
  // only while the user holds Ctrl+LMB after starting on empty space.
  if (m_boxSelecting) {
    Vector2 cur = GetMousePosition();
    float x = std::min(m_boxStart.x, cur.x);
    float y = std::min(m_boxStart.y, cur.y);
    float w = std::fabs(cur.x - m_boxStart.x);
    float h = std::fabs(cur.y - m_boxStart.y);
    DrawRectangle(static_cast<int>(x), static_cast<int>(y),
                  static_cast<int>(w), static_cast<int>(h),
                  {255, 220, 80, 40});
    DrawRectangleLines(static_cast<int>(x), static_cast<int>(y),
                       static_cast<int>(w), static_cast<int>(h),
                       {255, 220, 80, 220});
  }
}

// ====================================================================
// 2D box-select overlay — drawn as a translucent rectangle while the
// user drags. Lives outside Mode3D so it shows in screen space; called
// indirectly via the inspector's HUD render path. We just check the
// active flag here and draw if set.
// ====================================================================
// (No separate function — the box-select rectangle would normally
// belong in render3D since the rest of the tool draws there; raylib
// can't mix 2D + 3D inside BeginMode3D though. We draw the box rect
// in renderHud's tail so the box overlays over the 3D scene cleanly.)

// ====================================================================
// Save / reload
// ====================================================================
bool VertexTool::save(Inspector &insp) {
  if (!saveObjVertices(insp.path(), insp.mesh().vertices, m_dirty)) {
    std::fprintf(stderr, "[VertexTool] save failed\n");
    return false;
  }
  m_dirty = false;
  return true;
}

void VertexTool::onReload(Inspector & /*insp*/) {
  selectionClear();
  m_hover = -1;
  m_dragging = false;
  m_axisLock = -1;
  m_dragAnchor = -1;
  m_boxSelecting = false;
  m_snapTargetIdx = -1;
  cancelNumericInput();
  clearHistory();
  m_dirty = false;
}

} // namespace tsmesh
