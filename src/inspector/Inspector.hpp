#pragma once

#include "InspectorConfig.hpp"
#include "MenuBar.hpp"
#include "Tool.hpp"
#include "mesh/ObjLoader.hpp"
#include "mesh/SidecarProfile.hpp"
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

  // Entity-sidecar profile (Inspector roadmap F.1). Populated by
  // load() — empty defaults when no sidecar exists. F.* tools edit
  // through the typed view; save flushes through saveProfile().
  EntityProfile &profile() { return m_profile; }
  const EntityProfile &profile() const { return m_profile; }

  // ---- Menu-bar action surface ----
  // Public so MenuBar callbacks can drive the inspector without
  // friending into the private input handlers. Each action mirrors
  // the equivalent keyboard shortcut.
  void actionOpenDialog();           // O
  void actionOpenPath(const std::filesystem::path &p); // recents list
  bool actionSave();                 // Ctrl+S — returns true on success
  void actionSaveAsDialog();         // Ctrl+Shift+S
  void actionClose();                // Ctrl+W (dirty-guarded)
  void actionQuit();                 // Q (dirty-guarded)
  void actionFrameView();            // F
  void actionSwitchTool(size_t idx); // Tool menu picks
  void actionToggleControlsHelp();   // Help → Controls

  size_t currentToolIndex() const { return m_currentTool; }
  size_t toolCount() const { return m_tools.size(); }
  const char *toolNameAt(size_t i) const;
  const InspectorConfig &cfg() const { return m_cfg; }

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

  // Read-only sidecar viewer overlay (F.1) — forward arrow,
  // hardpoint icons, fire arcs, AI rings. Drawn inside Mode3D after
  // the mesh + axes so it composites over everything else but stays
  // independent from whichever Tool is active.
  void renderProfileOverlay() const;
  // HUD strip showing identity + smoke threshold + warning count.
  void renderProfileHud(int &yCursor) const;

  // Help → Controls overlay (centred modal-ish panel listing every
  // keyboard shortcut + mouse gesture; toggled via the Help menu).
  void renderControlsOverlay();

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

  // Sidecar profile for the current OBJ (F.1). Loaded by load(),
  // cleared by closeFile(); always present in default state when no
  // file is open (so tool accessors never null-deref).
  EntityProfile m_profile;

  // Top-of-screen menu bar — discoverable surface for every action
  // that's also bound to a keyboard shortcut. Built once at ctor.
  MenuBar m_menubar;

  // Requested-quit flag, set after the modal flow approves it.
  bool m_shouldQuit = false;

  // Help → Controls overlay (toggled from the menu bar). Scroll
  // offset is in pixels so wheel/drag can move at sub-row precision.
  // m_controlsJustOpened suppresses the click-outside-to-dismiss
  // logic for one frame — without it, the same LMB-pressed that
  // fired the menu action would also dismiss the overlay (since the
  // menu click lands outside the not-yet-drawn panel).
  bool m_controlsOverlay = false;
  bool m_controlsJustOpened = false;
  float m_controlsScrollY = 0.0f;
};

} // namespace tsmesh
