#include "Inspector.hpp"

#include "VertexTool.hpp"
#include "raymath.h"

#include <algorithm>
#include <cstdio>

namespace tsmesh {

namespace {

// Start at a sensible orbit distance keyed off the bounding sphere.
Camera3D defaultCamera(float boundsRadius) {
  Camera3D c{};
  float d = boundsRadius * 3.5f;
  c.position = {d * 0.7f, d * 0.5f, d * 0.7f};
  c.target = {0, 0, 0};
  c.up = {0, 1, 0};
  c.fovy = 45.0f;
  c.projection = CAMERA_PERSPECTIVE;
  return c;
}

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
  m_camera = defaultCamera(m_boundsR);
  for (auto &t : m_tools) t->onReload(*this);
  return true;
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

  if (m_currentTool < m_tools.size())
    m_tools[m_currentTool]->handleInput(*this);

  UpdateCamera(&m_camera, CAMERA_THIRD_PERSON);
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
  DrawText("RMB+mouse: orbit  |  wheel: zoom  |  TAB: tool  |  "
           "S: save  |  R: reload  |  Q: quit",
           10, sh - 22, 12, {180, 200, 220, 200});
}

} // namespace tsmesh
