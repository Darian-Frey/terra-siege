#pragma once

#include "Tool.hpp"
#include "mesh/ObjLoader.hpp"
#include "raylib.h"

#include <filesystem>
#include <memory>
#include <vector>

// ====================================================================
// Inspector — in-engine OBJ vertex editor (3d_assets.md §8).
//
// Owns the loaded mesh + camera + GPU model. Delegates feature-
// specific behavior to a list of Tool subclasses; TAB cycles between
// them. The vertex editor is the first tool; further tools (forward-
// axis assignment, weapon mount points, collision capsules, …) plug
// in by implementing the Tool interface and being pushed into m_tools.
// ====================================================================
namespace tsmesh {

class Inspector {
public:
  Inspector();

  // True if load+upload succeeded.
  bool load(const std::filesystem::path &path);

  // Mainloop. Blocks until the user presses Q or closes the window.
  int run();

  // Accessors for tools.
  Mesh3D &mesh() { return m_mesh; }
  const Mesh3D &mesh() const { return m_mesh; }
  const Camera3D &camera() const { return m_camera; }
  const std::filesystem::path &path() const { return m_path; }
  float boundsRadius() const { return m_boundsR; }
  float vertexSphereRadius() const { return m_vertSphereR; }

  // Called by tools after they mutate vertex data; re-uploads the
  // GPU model so the next frame renders the edit.
  void rebuildModel();

  // True if any tool reports unsaved edits.
  bool anyDirty() const;

private:
  void handleInput();
  void render();
  void renderHud();

  void computeBoundingSphere();
  void switchTool(size_t idx);
  void doSave();
  void doReload();

  std::filesystem::path m_path;
  Mesh3D m_mesh{};
  Model m_model{};
  bool m_modelOwned = false;

  Camera3D m_camera{};
  float m_vertSphereR = 0.1f;
  float m_boundsR = 1.0f;

  std::vector<std::unique_ptr<Tool>> m_tools;
  size_t m_currentTool = 0;
};

} // namespace tsmesh
