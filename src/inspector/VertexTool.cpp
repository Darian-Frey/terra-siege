#include "VertexTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "mesh/ObjLoader.hpp"
#include "raymath.h"

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

// Closest-approach `s` along (lineOrigin + s*lineDir) to the mouse ray.
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

} // anonymous namespace

void VertexTool::handleInput(Inspector &insp) {
  int held = -1;
  if (IsKeyDown(KEY_X)) held = 0;
  else if (IsKeyDown(KEY_Y)) held = 1;
  else if (IsKeyDown(KEY_Z)) held = 2;

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    pickVertex(insp);
    if (m_selected >= 0) {
      m_dragging = true;
      m_dragStartPos = insp.mesh().vertices[m_selected];
      m_axisLock = held;
    }
  }
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && m_dragging) {
    m_axisLock = held;
    dragVertex(insp, m_axisLock);
  }
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    m_dragging = false;
    m_axisLock = -1;
  }
}

void VertexTool::pickVertex(Inspector &insp) {
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
  m_selected = bestIdx;
}

void VertexTool::dragVertex(Inspector &insp, int axisLock) {
  if (m_selected < 0) return;
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());

  if (axisLock < 0) {
    Vector3 camFwd = Vector3Normalize(
        Vector3Subtract(insp.camera().target, insp.camera().position));
    insp.mesh().vertices[m_selected] =
        rayCameraPlaneHit(ray, m_dragStartPos, camFwd);
  } else {
    Vector3 axis = {0, 0, 0};
    if (axisLock == 0) axis.x = 1;
    else if (axisLock == 1) axis.y = 1;
    else axis.z = 1;
    Vector3 rayDirN = Vector3Normalize(ray.direction);
    float s = rayLineClosestApproach(ray.position, rayDirN,
                                     m_dragStartPos, axis);
    insp.mesh().vertices[m_selected] =
        Vector3Add(m_dragStartPos, Vector3Scale(axis, s));
  }
  m_dirty = true;
  insp.rebuildModel();
}

void VertexTool::render3D(const Inspector &insp) const {
  const auto &verts = insp.mesh().vertices;
  float r = insp.vertexSphereRadius();
  for (size_t i = 0; i < verts.size(); ++i) {
    Color c = (static_cast<int>(i) == m_selected)
                  ? Color{255, 220, 80, 255}
                  : Color{180, 200, 220, 220};
    DrawSphereEx(verts[i], r, 6, 8, c);
  }
  if (m_dragging && m_selected >= 0 && m_axisLock >= 0) {
    Vector3 v = verts[m_selected];
    float A = insp.boundsRadius() * 1.6f;
    Vector3 a = v, b = v;
    if (m_axisLock == 0) { a.x -= A; b.x += A; }
    else if (m_axisLock == 1) { a.y -= A; b.y += A; }
    else                     { a.z -= A; b.z += A; }
    DrawLine3D(a, b, {255, 220, 80, 255});
  }
}

void VertexTool::renderHud(const Inspector &insp, int &yCursor) const {
  char buf[256];
  if (m_selected >= 0) {
    Vector3 v = insp.mesh().vertices[m_selected];
    std::snprintf(buf, sizeof(buf), "v%d: (%.3f, %.3f, %.3f)",
                  m_selected, v.x, v.y, v.z);
    drawText(buf, 10, yCursor, 14, {220, 230, 250, 240});
  } else {
    drawText("click a vertex to select  |  hold X/Y/Z while dragging "
             "to axis-lock",
             10, yCursor, 14, {160, 180, 200, 200});
  }
  yCursor += 20;

  if (m_dragging && m_axisLock >= 0) {
    const char *axisName = (m_axisLock == 0) ? "X"
                          : (m_axisLock == 1) ? "Y" : "Z";
    std::snprintf(buf, sizeof(buf), "axis-lock: %s", axisName);
    drawText(buf, 10, yCursor, 14, {255, 220, 80, 240});
    yCursor += 20;
  }
}

bool VertexTool::save(Inspector &insp) {
  if (!saveObjVertices(insp.path(), insp.mesh().vertices, m_dirty)) {
    std::fprintf(stderr, "[VertexTool] save failed\n");
    return false;
  }
  m_dirty = false;
  return true;
}

void VertexTool::onReload(Inspector & /*insp*/) {
  m_selected = -1;
  m_dragging = false;
  m_axisLock = -1;
  m_dirty = false;
}

} // namespace tsmesh
