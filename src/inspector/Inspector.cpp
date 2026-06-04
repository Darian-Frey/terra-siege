#include "Inspector.hpp"

#include "VertexTool.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace tsmesh {

namespace {

// Mouse sensitivity for the orbit camera. Tuned by feel — feels
// natural at 1080p; users with very high-DPI mice may want lower.
constexpr float ORBIT_RAD_PER_PX = 0.005f;
constexpr float PAN_FRAC_PER_PX = 0.0015f; // scaled by distance
constexpr float ZOOM_IN_FACTOR = 0.85f;
constexpr float ZOOM_OUT_FACTOR = 1.18f;
constexpr float PITCH_LIMIT = 1.55334f; // ~89° — keep off the poles

constexpr int MODAL_W = 560;
constexpr int MODAL_LIST_H = 360;
constexpr int MODAL_BG_ALPHA = 235;

bool ctrlDown() {
  return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}
bool shiftDown() {
  return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

bool hasObjExtension(const std::filesystem::path &p) {
  std::string ext = p.extension().string();
  for (auto &c : ext) if (c >= 'A' && c <= 'Z') c += 32;
  return ext == ".obj";
}

// Bytewise copy. Used by Save As so the destination file inherits the
// source's comments/materials/faces before saveObjVertices rewrites
// its `v` lines.
bool copyFileBytes(const std::filesystem::path &src,
                   const std::filesystem::path &dst) {
  std::ifstream in(src, std::ios::binary);
  if (!in.is_open()) return false;
  std::error_code ec;
  std::filesystem::create_directories(dst.parent_path(), ec);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out << in.rdbuf();
  return out.good();
}

} // anonymous namespace

Inspector::Inspector() {
  m_tools.push_back(std::make_unique<VertexTool>());
  m_cfgPath = InspectorConfig::defaultPath();
  m_cfg.load(m_cfgPath); // missing file is fine, defaults stand
}

bool Inspector::load(const std::filesystem::path &path) {
  LoadResult lr = loadMesh(path);
  if (!lr.ok()) {
    std::fprintf(stderr, "[Inspector] failed to load %s\n",
                 path.string().c_str());
    return false;
  }
  if (m_modelOwned) { UnloadModel(m_model); m_modelOwned = false; }
  m_mesh = std::move(lr.mesh);
  m_path = path;
  m_hasMesh = true;
  computeBoundingSphere();
  m_vertSphereR = std::max(0.05f, m_boundsR * 0.012f);
  resetCamera();
  for (auto &t : m_tools) t->onReload(*this);
  rebuildModel();

  // MRU bookkeeping — promote and persist.
  m_cfg.pushRecent(m_path);
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(m_path, ec);
  if (!ec) m_cfg.lastDirectory = abs.parent_path();
  m_cfg.save(m_cfgPath);
  return true;
}

void Inspector::closeFile() {
  if (m_modelOwned) { UnloadModel(m_model); m_modelOwned = false; }
  m_mesh = Mesh3D{};
  m_path.clear();
  m_hasMesh = false;
  m_boundsR = 1.0f;
  m_vertSphereR = 0.1f;
  for (auto &t : m_tools) t->onReload(*this);
  resetCamera();
}

void Inspector::resetCamera() {
  m_camera.up = {0, 1, 0};
  m_camera.fovy = 45.0f;
  m_camera.projection = CAMERA_PERSPECTIVE;
  m_camTarget = {0, 0, 0};
  m_camYaw = PI * 0.25f;
  m_camPitch = PI * 0.18f;
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
  if (m_modelOwned) { UnloadModel(m_model); m_modelOwned = false; }
  if (!m_hasMesh) return;
  m_model = uploadModel(m_mesh);
  m_modelOwned = (m_model.meshCount > 0);
}

bool Inspector::anyDirty() const {
  if (!m_hasMesh) return false;
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

bool Inspector::doSave() {
  if (!m_hasMesh) return false;
  bool ok = true;
  for (auto &t : m_tools)
    if (t->isDirty() && !t->save(*this)) ok = false;
  if (!ok)
    std::fprintf(stderr, "[Inspector] one or more tools failed to save\n");
  return ok;
}

bool Inspector::doSaveAs(const std::filesystem::path &dest) {
  if (!m_hasMesh) return false;
  if (dest.empty()) return false;

  // Copy the source file so the destination inherits comments/materials/
  // faces, then point m_path at it and run the normal save path. We
  // mark every dirty tool dirty (no-op if already), so saveObjVertices
  // actually writes — its dirty short-circuit (T-06) would skip an
  // untouched file.
  std::error_code ec;
  if (std::filesystem::equivalent(dest, m_path, ec)) {
    return doSave();
  }
  if (!copyFileBytes(m_path, dest)) {
    std::fprintf(stderr, "[Inspector] save-as: copy failed: %s -> %s\n",
                 m_path.string().c_str(), dest.string().c_str());
    return false;
  }

  std::filesystem::path prev = m_path;
  m_path = dest;
  bool ok = true;
  for (auto &t : m_tools)
    if (!t->save(*this)) ok = false;
  if (!ok) {
    std::fprintf(stderr, "[Inspector] save-as failed for %s\n",
                 dest.string().c_str());
    m_path = prev;
    return false;
  }

  m_cfg.pushRecent(m_path);
  std::error_code ec2;
  std::filesystem::path abs = std::filesystem::absolute(m_path, ec2);
  if (!ec2) m_cfg.lastDirectory = abs.parent_path();
  m_cfg.save(m_cfgPath);
  return true;
}

void Inspector::doReload() {
  if (m_hasMesh) load(m_path);
}

// ====================================================================
// Modal / dirty-guard plumbing
// ====================================================================
void Inspector::openModal(Modal m) {
  m_modal = m;
  if (m == Modal::Open) {
    if (m_dirPath.empty()) {
      m_dirPath = !m_cfg.lastDirectory.empty()
                      ? m_cfg.lastDirectory
                      : defaultMeshDirectory();
    }
    refreshDirListing();
  } else if (m == Modal::SaveAs) {
    m_saveAsBuf = m_path.string();
  }
}

void Inspector::closeModal() {
  m_modal = Modal::None;
}

bool Inspector::guardDirty(PendingAction next) {
  if (!anyDirty()) {
    m_pending = next;
    executePending();
    return true;
  }
  m_pending = next;
  openModal(Modal::ConfirmUnsaved);
  return false;
}

void Inspector::executePending() {
  PendingAction p = m_pending;
  m_pending = PendingAction::None;
  switch (p) {
    case PendingAction::None: break;
    case PendingAction::OpenPath:
      load(m_pendingPath);
      m_pendingPath.clear();
      break;
    case PendingAction::OpenDialog:
      openModal(Modal::Open);
      break;
    case PendingAction::Close:
      closeFile();
      break;
    case PendingAction::Quit:
      m_shouldQuit = true;
      break;
  }
}

// ====================================================================
// File-picker support
// ====================================================================
std::filesystem::path Inspector::defaultMeshDirectory() const {
  const char *candidates[] = {
      "assets/meshes",
      "../assets/meshes",
      "../../assets/meshes",
  };
  for (const char *c : candidates) {
    std::filesystem::path p = c;
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) return p;
  }
  return std::filesystem::current_path();
}

void Inspector::refreshDirListing() {
  m_dirEntries.clear();
  m_dirSel = 0;
  m_dirScroll = 0;
  std::error_code ec;
  if (!std::filesystem::is_directory(m_dirPath, ec)) return;

  for (auto it = std::filesystem::directory_iterator(m_dirPath, ec);
       !ec && it != std::filesystem::end(it); it.increment(ec)) {
    if (it->is_regular_file(ec) && hasObjExtension(it->path()))
      m_dirEntries.push_back(it->path());
  }
  std::sort(m_dirEntries.begin(), m_dirEntries.end());
}

// ====================================================================
// Mainloop
// ====================================================================
int Inspector::run() {
  // We handle our own confirm-on-close flow; raylib's default
  // ESC-closes-window would bypass it.
  SetExitKey(0);

  while (!m_shouldQuit) {
    // raylib X-button or any other window-close signal: treat as quit
    // request, gated through the dirty guard. The flag can't be
    // unset, so accept the quit on the next pass through the guard.
    if (WindowShouldClose() && m_pending != PendingAction::Quit &&
        m_modal != Modal::ConfirmUnsaved) {
      guardDirty(PendingAction::Quit);
    }

    handleInput();

    BeginDrawing();
    ClearBackground({28, 32, 40, 255});
    render();
    renderHud();
    if (m_modal == Modal::Open) renderOpenModal();
    else if (m_modal == Modal::SaveAs) renderSaveAsModal();
    else if (m_modal == Modal::ConfirmUnsaved) renderConfirmUnsavedModal();
    EndDrawing();

    if (m_shouldQuit) break;
    // If the user-X has been clicked and the user has approved (no
    // dirty state, or chose Discard), the dirty guard already set
    // m_shouldQuit. Otherwise WindowShouldClose() will keep firing and
    // re-enter the guard each frame — which is fine, the user can
    // cancel the modal as many times as they like.
  }

  if (m_modelOwned) {
    UnloadModel(m_model);
    m_modelOwned = false;
  }
  return 0;
}

// ====================================================================
// Input dispatch
// ====================================================================
void Inspector::handleInput() {
  // Drag-and-drop — works any time, modal-free. Opens a dropped OBJ
  // (the first one) through the dirty guard.
  if (IsFileDropped()) {
    FilePathList drop = LoadDroppedFiles();
    std::filesystem::path picked;
    for (unsigned i = 0; i < drop.count; ++i) {
      std::filesystem::path p = drop.paths[i];
      if (hasObjExtension(p)) { picked = p; break; }
    }
    UnloadDroppedFiles(drop);
    if (!picked.empty()) {
      m_pendingPath = picked;
      guardDirty(PendingAction::OpenPath);
    }
  }

  // Modal-mode input — eats everything else.
  if (m_modal == Modal::Open) {
    int n = static_cast<int>(m_dirEntries.size()) +
            static_cast<int>(m_cfg.recentFiles.size());
    if (IsKeyPressed(KEY_ESCAPE)) closeModal();
    else if (IsKeyPressed(KEY_DOWN) && n > 0)
      m_dirSel = (m_dirSel + 1) % n;
    else if (IsKeyPressed(KEY_UP) && n > 0)
      m_dirSel = (m_dirSel + n - 1) % n;
    else if (IsKeyPressed(KEY_ENTER) && n > 0) {
      std::filesystem::path pick;
      int recN = static_cast<int>(m_cfg.recentFiles.size());
      if (m_dirSel < recN) pick = m_cfg.recentFiles[m_dirSel];
      else pick = m_dirEntries[m_dirSel - recN];
      closeModal();
      m_pendingPath = pick;
      // We already passed the dirty guard to open this dialog, so
      // load directly.
      load(m_pendingPath);
      m_pendingPath.clear();
    }
    return;
  }

  if (m_modal == Modal::SaveAs) {
    int c;
    while ((c = GetCharPressed()) > 0) {
      if (c >= 32 && c < 127) m_saveAsBuf.push_back(static_cast<char>(c));
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !m_saveAsBuf.empty())
      m_saveAsBuf.pop_back();
    if (IsKeyPressed(KEY_ESCAPE)) closeModal();
    else if (IsKeyPressed(KEY_ENTER)) {
      if (!m_saveAsBuf.empty()) {
        std::filesystem::path dest = m_saveAsBuf;
        if (doSaveAs(dest)) closeModal();
      }
    }
    return;
  }

  if (m_modal == Modal::ConfirmUnsaved) {
    if (IsKeyPressed(KEY_S)) {
      if (doSave()) { closeModal(); executePending(); }
    } else if (IsKeyPressed(KEY_D)) {
      closeModal();
      executePending();
    } else if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_ESCAPE)) {
      closeModal();
      m_pending = PendingAction::None;
    }
    return;
  }

  // Normal (no modal) keys
  if (ctrlDown() && IsKeyPressed(KEY_S)) {
    if (shiftDown()) openModal(Modal::SaveAs);
    else doSave();
  }
  if (ctrlDown() && IsKeyPressed(KEY_W))
    guardDirty(PendingAction::Close);
  if (IsKeyPressed(KEY_O))
    guardDirty(PendingAction::OpenDialog);
  if (IsKeyPressed(KEY_R)) doReload();
  if (IsKeyPressed(KEY_TAB)) switchTool(m_currentTool + 1);
  if (IsKeyPressed(KEY_F)) resetCamera();
  if (IsKeyPressed(KEY_Q)) guardDirty(PendingAction::Quit);

  if (m_hasMesh && m_currentTool < m_tools.size())
    m_tools[m_currentTool]->handleInput(*this);

  // Orbit camera — only moves while RMB is held.
  Vector2 md = GetMouseDelta();
  bool shift = shiftDown();
  bool dirty = false;

  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) &&
      (md.x != 0.0f || md.y != 0.0f)) {
    if (shift) {
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

// ====================================================================
// 3D render
// ====================================================================
void Inspector::render() {
  BeginMode3D(m_camera);

  if (m_hasMesh && m_modelOwned) DrawModel(m_model, {0, 0, 0}, 1.0f, WHITE);

  DrawGrid(20, m_boundsR * 0.25f);

  float L = m_boundsR * 1.4f;
  DrawLine3D({-L, 0, 0}, {L, 0, 0}, {220, 100, 100, 200}); // X
  DrawLine3D({0, -L, 0}, {0, L, 0}, {100, 220, 100, 200}); // Y
  DrawLine3D({0, 0, -L}, {0, 0, L}, {100, 140, 240, 200}); // Z

  if (m_hasMesh && m_currentTool < m_tools.size())
    m_tools[m_currentTool]->render3D(*this);

  EndMode3D();
}

// ====================================================================
// HUD — empty-workspace overlay, tool HUD, and bottom status bar.
// ====================================================================
void Inspector::renderHud() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();

  if (!m_hasMesh) {
    const char *line1 = "no file loaded";
    const char *line2 = "press O to open  |  drag an .obj onto the window  "
                        "|  Q to quit";
    int w1 = MeasureText(line1, 36);
    int w2 = MeasureText(line2, 16);
    DrawText(line1, (sw - w1) / 2, sh / 2 - 40, 36, {200, 220, 240, 220});
    DrawText(line2, (sw - w2) / 2, sh / 2 + 8, 16, {160, 180, 200, 220});
  } else {
    // Tool HUD — same vertical region as before, just no longer the
    // path / dirty / tool-cycle text since the status bar owns that.
    int yCursor = 12;
    if (m_currentTool < m_tools.size())
      m_tools[m_currentTool]->renderHud(*this, yCursor);
  }

  // ----- Status bar (bottom strip) -----
  const int barH = 26;
  DrawRectangle(0, sh - barH, sw, barH, {18, 22, 30, 235});
  DrawRectangle(0, sh - barH, sw, 1, {60, 80, 110, 255});

  char left[512];
  if (m_hasMesh) {
    bool d = anyDirty();
    std::snprintf(left, sizeof(left), "%s%s  |  %zu verts  |  tool: %s",
                  m_path.string().c_str(),
                  d ? "  ●" : "",
                  m_mesh.vertices.size(),
                  (m_currentTool < m_tools.size())
                      ? m_tools[m_currentTool]->name()
                      : "(none)");
  } else {
    std::snprintf(left, sizeof(left), "(empty workspace)");
  }
  DrawText(left, 8, sh - barH + 6, 14, {210, 220, 235, 240});

  const char *help =
      "O: open  Ctrl+S: save  Ctrl+Shift+S: save-as  Ctrl+W: close  "
      "TAB: tool  F: frame  Q: quit";
  int hw = MeasureText(help, 12);
  DrawText(help, sw - hw - 8, sh - barH + 8, 12, {150, 170, 195, 220});
}

// ====================================================================
// Modals
// ====================================================================
void Inspector::renderOpenModal() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 160});

  int w = MODAL_W;
  int rowH = 22;
  int recN = static_cast<int>(m_cfg.recentFiles.size());
  int dirN = static_cast<int>(m_dirEntries.size());
  int sectionTitles = (recN > 0 ? 1 : 0) + 1;
  int contentRows = recN + dirN + sectionTitles;
  int h = std::min(MODAL_LIST_H, 80 + contentRows * rowH);
  int x = (sw - w) / 2;
  int y = (sh - h) / 2;

  DrawRectangle(x, y, w, h, {32, 38, 50, MODAL_BG_ALPHA});
  DrawRectangleLines(x, y, w, h, {120, 150, 190, 255});
  DrawText("Open OBJ", x + 14, y + 10, 18, {220, 230, 250, 240});
  DrawText(m_dirPath.string().c_str(), x + 14, y + 32, 12,
           {160, 180, 200, 220});

  int listY = y + 56;
  int maxRows = (h - 70) / rowH;
  int total = recN + dirN;
  if (m_dirSel < m_dirScroll) m_dirScroll = m_dirSel;
  if (m_dirSel >= m_dirScroll + maxRows)
    m_dirScroll = m_dirSel - maxRows + 1;
  if (m_dirScroll < 0) m_dirScroll = 0;

  int drawn = 0;
  // Combined virtual list: [recents...][dir entries...]
  for (int i = m_dirScroll; i < total && drawn < maxRows; ++i, ++drawn) {
    bool isRecent = (i < recN);
    std::filesystem::path p =
        isRecent ? m_cfg.recentFiles[i] : m_dirEntries[i - recN];
    int row = listY + drawn * rowH;
    if (i == m_dirSel)
      DrawRectangle(x + 8, row - 2, w - 16, rowH, {60, 90, 130, 220});
    Color col = isRecent ? Color{220, 200, 140, 240} : Color{220, 230, 245, 240};
    char label[400];
    if (isRecent)
      std::snprintf(label, sizeof(label), "★ %s", p.string().c_str());
    else
      std::snprintf(label, sizeof(label), "  %s", p.filename().string().c_str());
    DrawText(label, x + 14, row, 14, col);
  }

  if (total == 0) {
    DrawText("(no .obj files in directory and no recent files)",
             x + 14, listY, 13, {180, 180, 180, 220});
  }

  const char *hint = "Enter: open  |  Esc: cancel  |  ↑/↓: navigate";
  DrawText(hint, x + 14, y + h - 22, 12, {180, 200, 220, 220});
}

void Inspector::renderSaveAsModal() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 160});

  int w = MODAL_W;
  int h = 140;
  int x = (sw - w) / 2;
  int y = (sh - h) / 2;
  DrawRectangle(x, y, w, h, {32, 38, 50, MODAL_BG_ALPHA});
  DrawRectangleLines(x, y, w, h, {120, 150, 190, 255});

  DrawText("Save As", x + 14, y + 10, 18, {220, 230, 250, 240});
  DrawText("destination path:", x + 14, y + 40, 14, {180, 200, 220, 220});

  int boxX = x + 14;
  int boxY = y + 60;
  int boxW = w - 28;
  int boxH = 32;
  DrawRectangle(boxX, boxY, boxW, boxH, {20, 24, 32, 230});
  DrawRectangleLines(boxX, boxY, boxW, boxH, {100, 130, 170, 255});
  DrawText(m_saveAsBuf.c_str(), boxX + 6, boxY + 8, 16, {230, 240, 250, 255});

  // Caret
  int caretX = boxX + 6 + MeasureText(m_saveAsBuf.c_str(), 16);
  DrawRectangle(caretX + 1, boxY + 6, 2, boxH - 12, {230, 240, 250, 220});

  DrawText("Enter: save  |  Esc: cancel", x + 14, y + h - 22, 12,
           {180, 200, 220, 220});
}

void Inspector::renderConfirmUnsavedModal() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 160});

  int w = 460;
  int h = 150;
  int x = (sw - w) / 2;
  int y = (sh - h) / 2;
  DrawRectangle(x, y, w, h, {32, 38, 50, MODAL_BG_ALPHA});
  DrawRectangleLines(x, y, w, h, {200, 160, 80, 255});

  DrawText("Unsaved changes", x + 14, y + 10, 18, {255, 220, 120, 240});
  DrawText("The current file has unsaved edits.", x + 14, y + 44, 14,
           {230, 235, 245, 240});

  DrawText("[S] Save", x + 14, y + 80, 18, {200, 230, 200, 240});
  DrawText("[D] Discard", x + 140, y + 80, 18, {230, 180, 180, 240});
  DrawText("[C] Cancel", x + 296, y + 80, 18, {200, 215, 235, 240});

  DrawText("Esc cancels.", x + 14, y + h - 22, 12, {180, 200, 220, 220});
}

} // namespace tsmesh
