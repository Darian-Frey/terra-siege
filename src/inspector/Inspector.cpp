#include "Inspector.hpp"

#include "VertexTool.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {

// Mouse sensitivity for the orbit camera. Tuned by feel — feels
// natural at 1080p; users with very high-DPI mice may want lower.
constexpr float ORBIT_RAD_PER_PX = 0.005f;
constexpr float PAN_FRAC_PER_PX = 0.0015f; // scaled by distance
constexpr float ZOOM_IN_FACTOR = 0.85f;
constexpr float ZOOM_OUT_FACTOR = 1.18f;
constexpr float PITCH_LIMIT = 1.55334f; // ~89° — keep off the poles

} // anonymous namespace

Inspector::Inspector() {
  // Register the default tool set here. New tools are added by
  // pushing them into this vector — TAB will pick them up.
  m_tools.push_back(std::make_unique<VertexTool>());
}

bool Inspector::load(const std::filesystem::path &path) {
  m_path = path;
  LoadResult lr = loadMesh(path);
  if (!lr.ok()) {
    std::fprintf(stderr, "[Inspector] failed to load %s\n",
                 path.string().c_str());
    return false;
  }
  m_mesh = std::move(lr.mesh);
  computeBoundingSphere();
  m_vertSphereR = std::max(0.05f, m_boundsR * 0.012f);
  resetCamera();
  for (auto &t : m_tools) t->onReload(*this);
  return true;
}

void Inspector::resetCamera() {
  m_camera.up = {0, 1, 0};
  m_camera.fovy = 45.0f;
  m_camera.projection = CAMERA_PERSPECTIVE;
  m_camTarget = {0, 0, 0};
  m_camYaw = PI * 0.25f;       // 45° around Y
  m_camPitch = PI * 0.18f;     // ~32° above XZ plane
  m_camDistance = m_boundsR * 3.5f;
  updateCameraTransform();
}

void Inspector::updateCameraTransform() {
  float cp = cosf(m_camPitch);
  float sp = sinf(m_camPitch);
  float cy = cosf(m_camYaw);
  float sy = sinf(m_camYaw);
  m_camera.position = {
      m_camTarget.x + m_camDistance * cp * sy,
      m_camTarget.y + m_camDistance * sp,
      m_camTarget.z + m_camDistance * cp * cy,
  };
  m_camera.target = m_camTarget;
}

void Inspector::computeBoundingSphere() {
  m_boundsR = 1.0f;
  for (const Vector3 &v : m_mesh.vertices) {
    float r = Vector3Length(v);
    if (r > m_boundsR) m_boundsR = r;
  }
}

void Inspector::rebuildModel() {
  if (m_modelOwned) {
    UnloadModel(m_model);
    m_modelOwned = false;
  }
  m_model = uploadModel(m_mesh);
  m_modelOwned = (m_model.meshCount > 0);
}

bool Inspector::anyDirty() const {
  for (const auto &t : m_tools)
    if (t->isDirty()) return true;
  return false;
}

void Inspector::switchTool(size_t idx) {
  if (m_tools.empty()) return;
  size_t next = idx % m_tools.size();
  if (next == m_currentTool) return;
  if (m_currentTool < m_tools.size())
    m_tools[m_currentTool]->onDeactivate(*this);
  m_currentTool = next;
  m_tools[m_currentTool]->onActivate(*this);
}

void Inspector::doSave() {
  bool ok = true;
  for (auto &t : m_tools)
    if (t->isDirty() && !t->save(*this)) ok = false;
  if (!ok)
    std::fprintf(stderr, "[Inspector] one or more tools failed to save\n");
}

void Inspector::doReload() {
  load(m_path);
  rebuildModel();
}

int Inspector::run() {
  rebuildModel();

  while (!WindowShouldClose()) {
    handleInput();
    if (IsKeyPressed(KEY_Q)) break;

    BeginDrawing();
    ClearBackground({28, 32, 40, 255});
    render();
    renderHud();
    EndDrawing();
  }

  if (m_modelOwned) {
    UnloadModel(m_model);
    m_modelOwned = false;
  }
  return 0;
}

void Inspector::handleInput() {
  if (IsKeyPressed(KEY_S)) doSave();
  if (IsKeyPressed(KEY_R)) doReload();
  if (IsKeyPressed(KEY_TAB)) switchTool(m_currentTool + 1);
  if (IsKeyPressed(KEY_F)) resetCamera(); // Blender-style "frame view"

  if (m_currentTool < m_tools.size())
    m_tools[m_currentTool]->handleInput(*this);

  // Orbit camera — only moves while RMB is held. No raylib
  // UpdateCamera() because it reads mouse delta unconditionally and
  // would orbit on every stray cursor motion.
  Vector2 md = GetMouseDelta();
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  bool dirty = false;

  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
      (md.x != 0.0f || md.y != 0.0f)) {
    if (shift) {
      // Pan: slide the orbit target on the camera's view plane.
      Vector3 fwd = Vector3Normalize(
          Vector3Subtract(m_camTarget, m_camera.position));
      Vector3 right = Vector3Normalize(
          Vector3CrossProduct(fwd, Vector3{0, 1, 0}));
      Vector3 up = Vector3CrossProduct(right, fwd);
      float scale = m_camDistance * PAN_FRAC_PER_PX;
      m_camTarget = Vector3Add(
          m_camTarget,
          Vector3Add(Vector3Scale(right, -md.x * scale),
                     Vector3Scale(up, md.y * scale)));
    } else {
      m_camYaw -= md.x * ORBIT_RAD_PER_PX;
      m_camPitch -= md.y * ORBIT_RAD_PER_PX;
      if (m_camPitch > PITCH_LIMIT) m_camPitch = PITCH_LIMIT;
      if (m_camPitch < -PITCH_LIMIT) m_camPitch = -PITCH_LIMIT;
    }
    dirty = true;
  }

  float wheel = GetMouseWheelMove();
  if (wheel != 0.0f) {
    m_camDistance *= (wheel > 0.0f) ? ZOOM_IN_FACTOR : ZOOM_OUT_FACTOR;
    float minD = m_boundsR * 0.5f;
    float maxD = m_boundsR * 30.0f;
    if (m_camDistance < minD) m_camDistance = minD;
    if (m_camDistance > maxD) m_camDistance = maxD;
    dirty = true;
  }

  if (dirty) updateCameraTransform();
}

void Inspector::render() {
  BeginMode3D(m_camera);

  if (m_modelOwned) DrawModel(m_model, {0, 0, 0}, 1.0f, WHITE);

  DrawGrid(20, m_boundsR * 0.25f);

  float L = m_boundsR * 1.4f;
  DrawLine3D({-L, 0, 0}, {L, 0, 0}, {220, 100, 100, 200}); // X
  DrawLine3D({0, -L, 0}, {0, L, 0}, {100, 220, 100, 200}); // Y
  DrawLine3D({0, 0, -L}, {0, 0, L}, {100, 140, 240, 200}); // Z

  if (m_currentTool < m_tools.size())
    m_tools[m_currentTool]->render3D(*this);

  EndMode3D();
}

void Inspector::renderHud() {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s  |  %zu verts",
                m_path.filename().string().c_str(), m_mesh.vertices.size());
  DrawText(buf, 10, 10, 16, {220, 230, 250, 255});

  bool dirty = anyDirty();
  const char *dirtyStr = dirty ? "● MODIFIED  (S to save, R to reload)"
                               : "(clean)";
  DrawText(dirtyStr, 10, 30, 14,
           dirty ? Color{255, 200, 80, 240} : Color{160, 180, 200, 200});

  const char *toolName = (m_currentTool < m_tools.size())
                             ? m_tools[m_currentTool]->name()
                             : "(none)";
  std::snprintf(buf, sizeof(buf), "tool: %s  (TAB to cycle, %zu available)",
                toolName, m_tools.size());
  DrawText(buf, 10, 50, 14, {180, 220, 255, 240});

  int yCursor = 72;
  if (m_currentTool < m_tools.size())
    m_tools[m_currentTool]->renderHud(*this, yCursor);

  int sh = GetScreenHeight();
  DrawText("RMB drag: orbit  |  Shift+RMB: pan  |  wheel: zoom  |  "
           "F: frame view  |  TAB: tool  |  S: save  |  R: reload  |  Q: quit",
           10, sh - 22, 12, {180, 200, 220, 200});
}

} // namespace tsmesh
