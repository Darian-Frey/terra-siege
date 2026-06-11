#include "Inspector.hpp"

#include "AITool.hpp"
#include "FXTool.hpp"
#include "HardpointsTool.hpp"
#include "InspectorFont.hpp"
#include "HullTool.hpp"
#include "IdentityTool.hpp"
#include "MaterialsTool.hpp"
#include "PrimitivesTool.hpp"
#include "ProfileTool.hpp"
#include "ShieldsTool.hpp"
#include "TopologyTool.hpp"
#include "VertexTool.hpp"
#include "WeaponsTool.hpp"
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
  m_tools.push_back(std::make_unique<PrimitivesTool>());  // E
  m_tools.push_back(std::make_unique<MaterialsTool>());   // C
  m_tools.push_back(std::make_unique<TopologyTool>());    // D
  m_tools.push_back(std::make_unique<ProfileTool>());     // F.1
  m_tools.push_back(std::make_unique<IdentityTool>());    // F.2
  m_tools.push_back(std::make_unique<HullTool>());        // F.2
  m_tools.push_back(std::make_unique<ShieldsTool>());     // F.2
  m_tools.push_back(std::make_unique<WeaponsTool>());     // F.3
  m_tools.push_back(std::make_unique<HardpointsTool>());  // F.3
  m_tools.push_back(std::make_unique<AITool>());          // F.4
  m_tools.push_back(std::make_unique<FXTool>());          // F.5
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

  // Sidecar profile (F.1). Reset to defaults, then try to load the
  // sibling *.meta.json — missing file is fine, defaults stand and
  // m_profile.loaded stays false (the viewer overlay branches on
  // section-present flags, not on loaded itself).
  m_profile = EntityProfile{};
  loadProfile(sidecarPathFor(m_path), m_profile);
  for (const auto &w : m_profile.warnings)
    std::fprintf(stderr, "[Inspector] sidecar: %s\n", w.c_str());

  // Hard reset — fresh mesh, fresh undo history.
  clearUndoHistory();
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
  m_profile = EntityProfile{}; // drop the sidecar with the mesh
  clearUndoHistory();
  for (auto &t : m_tools) t->onReload(*this);
  resetCamera();
}

// ====================================================================
// Undo / redo — shared across tools (Phase B + E). Snapshots the full
// Mesh3D so topology changes (primitive insertion, future weld/split)
// round-trip cleanly. Vertex-only edits would happily snapshot just
// the vertex array, but the cost saving isn't worth the divergence.
// ====================================================================
void Inspector::pushUndo() {
  if (!m_hasMesh) return;
  m_undoStack.push_back(m_mesh);
  if (static_cast<int>(m_undoStack.size()) > kMaxUndoHistory)
    m_undoStack.erase(m_undoStack.begin());
  m_redoStack.clear();
}

void Inspector::undoMesh() {
  if (m_undoStack.empty()) return;
  m_redoStack.push_back(m_mesh);
  if (static_cast<int>(m_redoStack.size()) > kMaxUndoHistory)
    m_redoStack.erase(m_redoStack.begin());
  m_mesh = std::move(m_undoStack.back());
  m_undoStack.pop_back();
  computeBoundingSphere();
  m_vertSphereR = std::max(0.05f, m_boundsR * 0.012f);
  rebuildModel();
}

void Inspector::redoMesh() {
  if (m_redoStack.empty()) return;
  m_undoStack.push_back(m_mesh);
  if (static_cast<int>(m_undoStack.size()) > kMaxUndoHistory)
    m_undoStack.erase(m_undoStack.begin());
  m_mesh = std::move(m_redoStack.back());
  m_redoStack.pop_back();
  computeBoundingSphere();
  m_vertSphereR = std::max(0.05f, m_boundsR * 0.012f);
  rebuildModel();
}

void Inspector::clearUndoHistory() {
  m_undoStack.clear();
  m_redoStack.clear();
}

void Inspector::initEmptyMesh() {
  if (m_hasMesh) return;
  m_mesh = Mesh3D{};
  m_hasMesh = true;
  m_boundsR = 1.0f;
  m_vertSphereR = 0.1f;
  m_path.clear(); // no source file yet — Save As will prompt
  for (auto &t : m_tools) t->onReload(*this);
  resetCamera();
  rebuildModel();
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

// ====================================================================
// Public menu-action surface — thin wrappers around the private flow
// so the MenuBar can drive the inspector without friending into the
// internals. Each mirrors the equivalent keyboard hotkey.
// ====================================================================
void Inspector::actionOpenDialog() {
  guardDirty(PendingAction::OpenDialog);
}
void Inspector::actionOpenPath(const std::filesystem::path &p) {
  m_pendingPath = p;
  guardDirty(PendingAction::OpenPath);
}
bool Inspector::actionSave() { return doSave(); }
void Inspector::actionSaveAsDialog() {
  if (m_hasMesh) openModal(Modal::SaveAs);
}
void Inspector::actionClose() { guardDirty(PendingAction::Close); }
void Inspector::actionQuit() { guardDirty(PendingAction::Quit); }
void Inspector::actionFrameView() { resetCamera(); }
void Inspector::actionSwitchTool(size_t idx) { switchTool(idx); }
void Inspector::actionToggleControlsHelp() {
  m_controlsOverlay = !m_controlsOverlay;
  // Fired by the menu bar click; the same LMB-pressed that ran us
  // would otherwise reach the overlay's "click outside to dismiss"
  // check this frame and close it immediately. Grace flag swallows
  // exactly that frame.
  if (m_controlsOverlay) m_controlsJustOpened = true;
}
const char *Inspector::toolNameAt(size_t i) const {
  return (i < m_tools.size()) ? m_tools[i]->name() : "(none)";
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
    // Menu bar drawn AFTER tool/HUD so it stays on top, but BEFORE
    // the controls overlay so the overlay can opt to cover it.
    m_menubar.render(*this);
    if (m_controlsOverlay) renderControlsOverlay();
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

  // Controls overlay owns wheel + click + key input while it's up
  // (renderControlsOverlay handles its own scroll + dismiss).
  // Returning early here also stops the orbit camera + tool input
  // from running so wheel doesn't double-bind to zoom.
  if (m_controlsOverlay) return;

  // Menu bar input — owns its strip + dropdowns. handle() returns
  // true if the menu consumed input this frame; in that case skip
  // tool / camera / hotkey processing so a click on a dropdown
  // doesn't fall through to the vertex picker behind it. The bar
  // is suppressed while a modal is up (modals take priority).
  if (m_modal == Modal::None) {
    if (m_menubar.handle(*this)) return;
  }

  // Modal-mode input — eats everything else.
  if (m_modal == Modal::Open) {
    int recN = static_cast<int>(m_cfg.recentFiles.size());
    int dirN = static_cast<int>(m_dirEntries.size());
    int n = recN + dirN;

    // Mirror the geometry computed in renderOpenModal() so a click
    // lands on the same row the user sees highlighted. Kept inline
    // because the layout is small; if it grows, extract into a
    // shared helper.
    int rowH = 22;
    int sectionTitles = (recN > 0 ? 1 : 0) + 1;
    int contentRows = recN + dirN + sectionTitles;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int w = MODAL_W;
    int h = std::min(MODAL_LIST_H, 80 + contentRows * rowH);
    int x = (sw - w) / 2;
    int y = (sh - h) / 2;
    int listY = y + 56;
    int maxRows = (h - 70) / rowH;

    // Mouse hover → highlight; mouse wheel → scroll; click → open.
    Vector2 mp = GetMousePosition();
    int hoverRow = -1;
    for (int drawn = 0; drawn < maxRows; ++drawn) {
      int idx = m_dirScroll + drawn;
      if (idx >= n) break;
      Rectangle r{static_cast<float>(x + 8),
                  static_cast<float>(listY + drawn * rowH - 2),
                  static_cast<float>(w - 16),
                  static_cast<float>(rowH)};
      if (CheckCollisionPointRec(mp, r)) { hoverRow = idx; break; }
    }
    if (hoverRow >= 0) m_dirSel = hoverRow;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && n > 0) {
      m_dirScroll -= static_cast<int>(wheel);
      if (m_dirScroll < 0) m_dirScroll = 0;
      int maxScroll = std::max(0, n - maxRows);
      if (m_dirScroll > maxScroll) m_dirScroll = maxScroll;
    }

    if (IsKeyPressed(KEY_ESCAPE)) { closeModal(); return; }

    bool committed = false;
    if (IsKeyPressed(KEY_DOWN) && n > 0)
      m_dirSel = (m_dirSel + 1) % n;
    else if (IsKeyPressed(KEY_UP) && n > 0)
      m_dirSel = (m_dirSel + n - 1) % n;
    else if (IsKeyPressed(KEY_ENTER) && n > 0)
      committed = true;
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hoverRow >= 0)
      committed = true;

    if (committed && n > 0) {
      std::filesystem::path pick;
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

  // Inspector-level undo / redo (Phase B + E). Intercepted here so the
  // hotkeys work regardless of which tool is active.
  if (ctrlDown() && IsKeyPressed(KEY_Z)) { undoMesh(); return; }
  if (ctrlDown() && IsKeyPressed(KEY_Y)) { redoMesh(); return; }

  // Tool input — runs when a mesh is loaded OR when the active tool
  // declares canRunWithoutMesh() (PrimitivesTool overrides). The
  // start-from-empty path: user has no mesh, switches to Primitives,
  // inserts a cube, mesh now exists.
  if (m_currentTool < m_tools.size() &&
      (m_hasMesh || m_tools[m_currentTool]->canRunWithoutMesh())) {
    m_tools[m_currentTool]->handleInput(*this);
  }

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

  if (m_currentTool < m_tools.size() &&
      (m_hasMesh || m_tools[m_currentTool]->canRunWithoutMesh()))
    m_tools[m_currentTool]->render3D(*this);

  // F.1 read-only viewer — drawn for any open mesh regardless of
  // which tool is active. Cheap (a few line + ring primitives) so
  // we always emit; ProfileView's `present` flags gate individual
  // pieces so an empty sidecar shows nothing.
  if (m_hasMesh) renderProfileOverlay();

  EndMode3D();
}

// ====================================================================
// renderProfileOverlay — sidecar viewer (F.1). All drawn in Mode3D.
//
//   forward arrow — yellow line from pivot along view.forward,
//                   length scaled to bounds radius for legibility
//   hardpoints    — small spheres at each hardpoint.pos with a short
//                   arrow along hardpoint.dir, plus a translucent
//                   fire-arc cone (DrawCircle3D) at the hardpoint
//                   tip if fireArcDeg > 0
//   AI rings      — concentric circles on the XZ plane at the
//                   detectionRange + attackRange radii (worldspace
//                   units divided by the mesh's typical scale of 1)
//
// Everything is read-only; ProfileTool's edits flow back through
// the typed view, not through this renderer.
// ====================================================================
void Inspector::renderProfileOverlay() const {
  const ProfileView &v = m_profile.view;
  const Vector3 pivot = v.pivot;

  // ---- forward arrow ----
  // Yellow line from pivot along view.forward. Length scaled with
  // the mesh bounds so a tiny ship and a huge ship both read.
  float aLen = m_boundsR * 1.1f;
  Vector3 fwdN = v.forward;
  float flen = sqrtf(fwdN.x * fwdN.x + fwdN.y * fwdN.y + fwdN.z * fwdN.z);
  if (flen > 1e-4f) {
    fwdN.x /= flen; fwdN.y /= flen; fwdN.z /= flen;
    Vector3 tip = {pivot.x + fwdN.x * aLen,
                   pivot.y + fwdN.y * aLen,
                   pivot.z + fwdN.z * aLen};
    DrawLine3D(pivot, tip, {255, 220, 60, 255});
    // Arrowhead — small sphere at the tip; flat-shaded look is fine
    // for a marker.
    DrawSphereEx(tip, m_boundsR * 0.04f, 5, 7, {255, 220, 60, 255});
  }

  // ---- pivot marker ----
  // Small red sphere at the pivot itself so the user can see when
  // pivot has been shifted off-origin.
  DrawSphereEx(pivot, m_boundsR * 0.025f, 5, 7, {255, 100, 100, 255});

  // ---- hardpoints ----
  for (const ProfileView::Hardpoint &hp : v.hardpoints) {
    Color hpCol = {120, 220, 255, 255};
    DrawSphereEx(hp.pos, m_boundsR * 0.045f, 5, 8, hpCol);
    // Direction arrow — short line + tip dot.
    Vector3 dn = hp.dir;
    float dl = sqrtf(dn.x * dn.x + dn.y * dn.y + dn.z * dn.z);
    if (dl > 1e-4f) {
      dn.x /= dl; dn.y /= dl; dn.z /= dl;
      float L2 = m_boundsR * 0.35f;
      Vector3 dt = {hp.pos.x + dn.x * L2,
                    hp.pos.y + dn.y * L2,
                    hp.pos.z + dn.z * L2};
      DrawLine3D(hp.pos, dt, hpCol);
    }
    // Fire-arc cone — translucent disc at the muzzle perpendicular
    // to dir, radius scaled by fireArcDeg. Cheap visual stub for
    // F.1; F.3 will replace with a proper cone when hardpoints
    // become editable.
    if (hp.fireArcDeg > 0.0f && dl > 1e-4f) {
      float arcRad = hp.fireArcDeg * (PI / 180.0f);
      float r = m_boundsR * 0.35f * tanf(arcRad);
      if (r > m_boundsR * 0.005f) {
        Vector3 axisN = {dn.x, dn.y, dn.z};
        // raylib DrawCircle3D wants a rotation angle + axis. Build
        // a rotation that takes +Y onto the hardpoint direction.
        Vector3 up = {0, 1, 0};
        Vector3 axis = Vector3CrossProduct(up, axisN);
        float aLen2 = sqrtf(axis.x * axis.x + axis.y * axis.y +
                            axis.z * axis.z);
        float angDeg = 0.0f;
        if (aLen2 > 1e-4f) {
          axis.x /= aLen2; axis.y /= aLen2; axis.z /= aLen2;
          float c = up.x * axisN.x + up.y * axisN.y + up.z * axisN.z;
          if (c > 1.0f) c = 1.0f;
          if (c < -1.0f) c = -1.0f;
          angDeg = acosf(c) * (180.0f / PI);
        } else {
          axis = {1, 0, 0};
        }
        Vector3 tipPos = {hp.pos.x + axisN.x * m_boundsR * 0.35f,
                          hp.pos.y + axisN.y * m_boundsR * 0.35f,
                          hp.pos.z + axisN.z * m_boundsR * 0.35f};
        DrawCircle3D(tipPos, r, axis, angDeg, {120, 220, 255, 180});
      }
    }
  }

  // ---- AI rings ----
  // Concentric circles on the XZ plane at detectionRange + attackRange.
  // Skipped when the rings would be massively larger than the mesh
  // (e.g. 350m detection vs 1m ship) by scaling them DOWN so they
  // stay readable — the inspector is a scale-agnostic viewer.
  if (v.aiPresent) {
    Vector3 axisY = {0, 1, 0};
    float refScale = m_boundsR * 6.0f; // viewport-friendly max ring radius
    auto drawRing = [&](float worldRange, Color c) {
      if (worldRange <= 0.0f) return;
      float r = (worldRange > refScale) ? refScale : worldRange;
      DrawCircle3D(pivot, r, axisY, 90.0f, c);
    };
    drawRing(v.detectionRange, {255, 130, 130, 140});
    drawRing(v.attackRange, {255, 200, 100, 200});
  }
}

void Inspector::renderProfileHud(int &yCursor) const {
  const ProfileView &v = m_profile.view;
  char buf[256];
  bool any = !v.displayName.empty() || !v.entityClass.empty() ||
             v.aiPresent || v.fxPresent || !v.hardpoints.empty();
  if (!any && !m_profile.loaded) return;

  if (!v.displayName.empty() || !v.entityClass.empty()) {
    std::snprintf(buf, sizeof(buf), "sidecar: %s%s%s",
                  v.displayName.empty() ? "(unnamed)" : v.displayName.c_str(),
                  v.entityClass.empty() ? "" : "  · ",
                  v.entityClass.c_str());
    drawText(buf, 10, yCursor, 14, {180, 220, 255, 240});
    yCursor += 18;
  }
  if (v.fxPresent && v.smokeAtHPFrac > 0.0f) {
    std::snprintf(buf, sizeof(buf), "smoke at hp ≤ %.0f%%",
                  v.smokeAtHPFrac * 100.0f);
    drawText(buf, 10, yCursor, 13, {200, 200, 220, 220});
    yCursor += 16;
  }
  if (v.aiPresent) {
    std::snprintf(buf, sizeof(buf),
                  "ai: detect=%.0f  attack=%.0f  evade=%.0f%%  retreat=%.0f%%",
                  v.detectionRange, v.attackRange,
                  v.evadeAtHPFrac * 100.0f, v.retreatAtHPFrac * 100.0f);
    drawText(buf, 10, yCursor, 13, {200, 200, 220, 220});
    yCursor += 16;
  }
  if (!v.hardpoints.empty()) {
    std::snprintf(buf, sizeof(buf), "hardpoints: %zu", v.hardpoints.size());
    drawText(buf, 10, yCursor, 13, {200, 200, 220, 220});
    yCursor += 16;
  }
  if (!m_profile.warnings.empty()) {
    std::snprintf(buf, sizeof(buf), "sidecar warnings: %zu",
                  m_profile.warnings.size());
    drawText(buf, 10, yCursor, 13, {255, 180, 100, 240});
    yCursor += 16;
  }
}

// ====================================================================
// HUD — empty-workspace overlay, tool HUD, and bottom status bar.
// ====================================================================
void Inspector::renderHud() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();

  // Empty-workspace overlay is suppressed when the active tool can
  // run without a mesh — it'd otherwise occlude PrimitivesTool's HUD.
  bool toolWantsEmptyWorkspace =
      m_currentTool < m_tools.size() &&
      m_tools[m_currentTool]->canRunWithoutMesh();

  if (!m_hasMesh && !toolWantsEmptyWorkspace) {
    const char *line1 = "no file loaded";
    const char *line2 = "press O to open  |  drag an .obj onto the window  "
                        "|  Q to quit";
    int w1 = measureText(line1, 36);
    int w2 = measureText(line2, 16);
    drawText(line1, (sw - w1) / 2, sh / 2 - 40, 36, {200, 220, 240, 220});
    drawText(line2, (sw - w2) / 2, sh / 2 + 8, 16, {160, 180, 200, 220});
  } else if (m_currentTool < m_tools.size()) {
    // Tool HUD + sidecar HUD (F.1). Tool draws first, sidecar info
    // appended below so the active tool's state stays at the top.
    // y starts below the menu bar (kBarHeight + a few px).
    int yCursor = MenuBar::kBarHeight + 8;
    m_tools[m_currentTool]->renderHud(*this, yCursor);
    if (m_hasMesh) {
      yCursor += 6;
      renderProfileHud(yCursor);
    }
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
  drawText(left, 8, sh - barH + 6, 14, {210, 220, 235, 240});

  // Bottom-right hint — the discoverable surface is now the menu bar,
  // so this is just a single nudge to Help → Controls for the full
  // hotkey list.
  const char *help = "Help → Controls for shortcuts";
  int hw = measureText(help, 12);
  drawText(help, sw - hw - 8, sh - barH + 8, 12, {150, 170, 195, 220});
}

// ====================================================================
// renderControlsOverlay — full keyboard + mouse cheat sheet, toggled
// by Help → Controls (or Esc / click outside to dismiss). Wider
// translucent panel with a scrollable content region — mouse wheel,
// PageUp/PageDown, arrow keys, or drag the scroll thumb. Sections
// are grouped (File / View / Mouse / Vertex tool / Profile tool) so
// new shortcuts from F.2+ can be slotted in without rebuilding.
// ====================================================================
void Inspector::renderControlsOverlay() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 160});

  // Panel — generous width so descriptions fit without truncation.
  // Height clamps to 80% of the screen so it stays usable on small
  // windows.
  int w = 760;
  int h = std::min(560, sh - 80);
  int x = (sw - w) / 2;
  int y = (sh - h) / 2;
  DrawRectangle(x, y, w, h, {32, 38, 50, 240});
  DrawRectangleLines(x, y, w, h, {120, 150, 190, 255});

  // Header (fixed; sits above the scroll region).
  drawText("Controls", x + 14, y + 12, 20, {220, 230, 250, 240});
  drawText("Esc to close", x + w - measureText("Esc to close", 12) - 14,
           y + 18, 12, {160, 180, 200, 220});

  // Content layout — single column with section headers. Each entry
  // is either a section header (k empty, d is the header) or a row
  // (k = shortcut, d = description). The renderer uses Scissor mode
  // to clip content to the visible region so partial rows don't
  // bleed outside the panel.
  struct Row { const char *k; const char *d; };
  static const Row rows[] = {
      {"", "File"},
      {"O",             "Open file"},
      {"Ctrl+S",        "Save"},
      {"Ctrl+Shift+S",  "Save As..."},
      {"Ctrl+W",        "Close current file"},
      {"Q",             "Quit"},
      {"", "View"},
      {"F",             "Frame view (reset orbit)"},
      {"TAB",           "Cycle tools"},
      {"", "Mouse"},
      {"RMB drag",      "Orbit camera"},
      {"Shift+RMB",     "Pan camera"},
      {"Wheel",         "Zoom"},
      {"", "Vertex tool"},
      {"LMB",           "Pick + drag vertex"},
      {"X / Y / Z",     "Axis-lock during drag"},
      {"R",             "Reload from disk"},
      {"", "Profile tool"},
      {". / ,",         "Cycle focused field (next / prev)"},
      {"↑ / ↓",         "Adjust focused field by ±0.05"},
      {"Shift+↑ / ↓",   "Adjust focused field by ±1.0"},
      {"", "Open file dialog"},
      {"click / Enter", "Open hovered row"},
      {"↑ / ↓",         "Move selection"},
      {"Wheel",         "Scroll list"},
      {"Esc",           "Cancel"},
  };
  const int nRows = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
  const int rowH = 22;
  const int sectionGapTop = 8;      // extra gap above each section header
  const int sectionGapBottom = 4;   // extra gap below each section header

  // Compute total content height so the scroll range is right.
  int totalH = 0;
  for (int i = 0; i < nRows; ++i) {
    if (rows[i].k[0] == '\0') totalH += sectionGapTop + rowH + sectionGapBottom;
    else totalH += rowH;
  }

  // Scrollable viewport — between the header and the bottom hint.
  const int headerH = 44;
  const int hintH = 24;
  int contentX = x + 12;
  int contentY = y + headerH;
  int contentW = w - 24 - 14; // leave room for scrollbar gutter
  int contentH = h - headerH - hintH;

  // Clamp scroll to valid range.
  int maxScroll = std::max(0, totalH - contentH);
  if (m_controlsScrollY < 0.0f) m_controlsScrollY = 0.0f;
  if (m_controlsScrollY > maxScroll) m_controlsScrollY = static_cast<float>(maxScroll);

  // Wheel + key scrolling. Only honour input while the overlay is
  // up; the modal-priority gate above already prevents game input
  // from running while this is open.
  float wheel = GetMouseWheelMove();
  if (wheel != 0.0f) m_controlsScrollY -= wheel * 30.0f;
  if (IsKeyPressed(KEY_PAGE_DOWN)) m_controlsScrollY += contentH * 0.8f;
  if (IsKeyPressed(KEY_PAGE_UP))   m_controlsScrollY -= contentH * 0.8f;
  if (IsKeyDown(KEY_DOWN))         m_controlsScrollY += 2.0f;
  if (IsKeyDown(KEY_UP))           m_controlsScrollY -= 2.0f;
  if (m_controlsScrollY < 0.0f) m_controlsScrollY = 0.0f;
  if (m_controlsScrollY > maxScroll) m_controlsScrollY = static_cast<float>(maxScroll);

  // Draw rows inside a scissor rect so the scrolled-out portion
  // doesn't leak past the panel.
  BeginScissorMode(contentX, contentY, contentW, contentH);
  int yCursor = contentY - static_cast<int>(m_controlsScrollY);
  for (int i = 0; i < nRows; ++i) {
    if (rows[i].k[0] == '\0') {
      yCursor += sectionGapTop;
      drawText(rows[i].d, contentX, yCursor, 15, {180, 220, 255, 240});
      // thin underline so the section reads at a glance
      DrawRectangle(contentX, yCursor + 18, contentW - 8, 1,
                    {80, 110, 150, 200});
      yCursor += rowH + sectionGapBottom;
    } else {
      drawText(rows[i].k, contentX + 16, yCursor, 14, {255, 220, 120, 240});
      drawText(rows[i].d, contentX + 180, yCursor, 14, {220, 230, 250, 230});
      yCursor += rowH;
    }
  }
  EndScissorMode();

  // Scrollbar — only drawn if content is actually larger than the
  // viewport. Background track + thumb whose size + position reflect
  // the scroll fraction.
  if (totalH > contentH) {
    int trackX = x + w - 16;
    int trackY = contentY;
    int trackW = 6;
    int trackH = contentH;
    DrawRectangle(trackX, trackY, trackW, trackH, {18, 22, 30, 200});
    float frac = static_cast<float>(contentH) / static_cast<float>(totalH);
    int thumbH = std::max(20, static_cast<int>(trackH * frac));
    int thumbRange = trackH - thumbH;
    int thumbY = trackY + (maxScroll > 0
                              ? static_cast<int>(thumbRange *
                                                 (m_controlsScrollY /
                                                  static_cast<float>(maxScroll)))
                              : 0);
    DrawRectangle(trackX, thumbY, trackW, thumbH, {120, 150, 190, 240});

    // Drag the thumb to scroll. Simple: while LMB is held on the
    // thumb track region, map cursor y → scroll.
    Vector2 mp = GetMousePosition();
    Rectangle trackRect{static_cast<float>(trackX),
                        static_cast<float>(trackY),
                        static_cast<float>(trackW),
                        static_cast<float>(trackH)};
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mp, trackRect)) {
      float t = (mp.y - trackY - thumbH * 0.5f) / std::max(1.0f, static_cast<float>(thumbRange));
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      m_controlsScrollY = t * maxScroll;
    }
  }

  // Bottom hint — scroll affordance + dismissal.
  const char *hint =
      "wheel / PgUp / PgDn / arrows: scroll  |  Esc or click outside: close";
  drawText(hint, x + 14, y + h - hintH + 6, 12, {180, 200, 220, 220});

  // Dismiss: Esc, OR click outside the panel. Skip click-dismiss on
  // the frame the overlay was just opened — that LMB-press is the
  // menu click that opened us, and it lands outside the panel by
  // definition (on the Help menu strip).
  if (IsKeyPressed(KEY_ESCAPE)) m_controlsOverlay = false;
  if (!m_controlsJustOpened && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 mp = GetMousePosition();
    Rectangle panel{static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h)};
    if (!CheckCollisionPointRec(mp, panel)) m_controlsOverlay = false;
  }
  m_controlsJustOpened = false;
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
  drawText("Open OBJ", x + 14, y + 10, 18, {220, 230, 250, 240});
  drawText(m_dirPath.string().c_str(), x + 14, y + 32, 12,
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
    drawText(label, x + 14, row, 14, col);
  }

  if (total == 0) {
    drawText("(no .obj files in directory and no recent files)",
             x + 14, listY, 13, {180, 180, 180, 220});
  }

  const char *hint =
      "click or Enter: open  |  Esc: cancel  |  ↑/↓ + wheel: navigate";
  drawText(hint, x + 14, y + h - 22, 12, {180, 200, 220, 220});
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

  drawText("Save As", x + 14, y + 10, 18, {220, 230, 250, 240});
  drawText("destination path:", x + 14, y + 40, 14, {180, 200, 220, 220});

  int boxX = x + 14;
  int boxY = y + 60;
  int boxW = w - 28;
  int boxH = 32;
  DrawRectangle(boxX, boxY, boxW, boxH, {20, 24, 32, 230});
  DrawRectangleLines(boxX, boxY, boxW, boxH, {100, 130, 170, 255});
  drawText(m_saveAsBuf.c_str(), boxX + 6, boxY + 8, 16, {230, 240, 250, 255});

  // Caret
  int caretX = boxX + 6 + measureText(m_saveAsBuf.c_str(), 16);
  DrawRectangle(caretX + 1, boxY + 6, 2, boxH - 12, {230, 240, 250, 220});

  drawText("Enter: save  |  Esc: cancel", x + 14, y + h - 22, 12,
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

  drawText("Unsaved changes", x + 14, y + 10, 18, {255, 220, 120, 240});
  drawText("The current file has unsaved edits.", x + 14, y + 44, 14,
           {230, 235, 245, 240});

  drawText("[S] Save", x + 14, y + 80, 18, {200, 230, 200, 240});
  drawText("[D] Discard", x + 140, y + 80, 18, {230, 180, 180, 240});
  drawText("[C] Cancel", x + 296, y + 80, 18, {200, 215, 235, 240});

  drawText("Esc cancels.", x + 14, y + h - 22, 12, {180, 200, 220, 220});
}

} // namespace tsmesh
