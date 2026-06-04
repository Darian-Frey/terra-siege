#pragma once

#include "InspectorConfig.hpp"
#include "Tool.hpp"
#include "mesh/ObjLoader.hpp"
#include "raylib.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ====================================================================
// Inspector — in-engine OBJ vertex editor (3d_assets.md §8).
//
// Owns the loaded mesh + camera + GPU model. Delegates feature-
// specific behavior to a list of Tool subclasses; TAB cycles between
// them. The vertex editor is the first tool; further tools (forward-
// axis assignment, weapon mount points, collision capsules, …) plug
// in by implementing the Tool interface and being pushed into m_tools.
//
// Phase A — boots with no file. O opens a picker over assets/meshes/*.obj,
// drag-and-drop loads a dropped path, Ctrl+S saves, Ctrl+Shift+S saves
// to a new path, Ctrl+W closes back to the empty workspace, and an
// unsaved-changes modal gates Open / Close / Quit so edits aren't lost.
// ====================================================================
namespace tsmesh {

class Inspector {
public:
  Inspector();

  // True if load+upload succeeded. After a failed load the inspector
  // stays in the empty-workspace state (no mesh, no model).
  bool load(const std::filesystem::path &path);

  // Mainloop. Blocks until the user quits.
  int run();

  // Accessors for tools.
  Mesh3D &mesh() { return m_mesh; }
  const Mesh3D &mesh() const { return m_mesh; }
  const Camera3D &camera() const { return m_camera; }
  const std::filesystem::path &path() const { return m_path; }
  float boundsRadius() const { return m_boundsR; }
  float vertexSphereRadius() const { return m_vertSphereR; }
  bool hasMesh() const { return m_hasMesh; }

  // Called by tools after they mutate vertex data; re-uploads the
  // GPU model so the next frame renders the edit.
  void rebuildModel();

  // True if any tool reports unsaved edits.
  bool anyDirty() const;

private:
  // -------- Mainloop helpers --------
  void handleInput();
  void render();
  void renderHud();

  // -------- Mesh lifecycle --------
  void computeBoundingSphere();
  void switchTool(size_t idx);
  bool doSave();
  bool doSaveAs(const std::filesystem::path &dest);
  void doReload();
  void closeFile(); // drops mesh+model, returns to empty workspace
  void resetCamera();
  void updateCameraTransform();

  // -------- Modal / file dialog --------
  enum class Modal { None, Open, SaveAs, ConfirmUnsaved };
  // Action gated by the unsaved-changes prompt — fired iff the user
  // picks "Discard" (or "Save" succeeds).
  enum class PendingAction {
    None,
    OpenPath,    // open m_pendingPath (file)
    OpenDialog,  // open the file picker
    Close,
    Quit,
  };

  void openModal(Modal m);
  void closeModal();
  bool guardDirty(PendingAction next);  // returns true if action ran now
  void executePending();

  // File dialog
  void refreshDirListing();
  void renderOpenModal();
  void renderSaveAsModal();
  void renderConfirmUnsavedModal();

  // Discovery: assets/meshes/ candidate, falls back to CWD.
  std::filesystem::path defaultMeshDirectory() const;

  // -------- State --------
  std::filesystem::path m_path;
  Mesh3D m_mesh{};
  Model m_model{};
  bool m_modelOwned = false;
  bool m_hasMesh = false;

  Camera3D m_camera{};
  Vector3 m_camTarget{0, 0, 0};
  float m_camYaw = 0.0f;
  float m_camPitch = 0.0f;
  float m_camDistance = 1.0f;

  float m_vertSphereR = 0.1f;
  float m_boundsR = 1.0f;

  std::vector<std::unique_ptr<Tool>> m_tools;
  size_t m_currentTool = 0;

  // Modal state
  Modal m_modal = Modal::None;
  PendingAction m_pending = PendingAction::None;
  std::filesystem::path m_pendingPath;

  // File picker (Open + SaveAs share the listing for context)
  std::filesystem::path m_dirPath;
  std::vector<std::filesystem::path> m_dirEntries;
  int m_dirSel = 0;
  int m_dirScroll = 0;

  // SaveAs text input
  std::string m_saveAsBuf;

  // Persisted config (recent files + last directory)
  InspectorConfig m_cfg;
  std::filesystem::path m_cfgPath;

  // Requested-quit flag, set after the modal flow approves it.
  bool m_shouldQuit = false;
};

} // namespace tsmesh
