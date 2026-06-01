#pragma once

#include "entity/Entity.hpp"
#include "raylib.h"
#include <array>
#include <cstdint>
#include <filesystem>

// ====================================================================
// MeshRegistry — startup-loaded raylib Models indexed by EntityType.
//
// At init() time, scans assets/meshes/ and uploads one Model per
// gameplay entity (player + every enemy + every friendly + Base +
// each projectile kind). Render code calls draw() / drawAt() with an
// EntityType and pose; the registry resolves the Model and issues a
// single DrawModelEx call.
//
// Yaw rotation is handled natively by DrawModelEx — the rlPushMatrix
// workaround used for the Carrier in the procedural render path is
// no longer needed for mesh-based entities.
//
// Missing meshes are not fatal: draw() falls back to a debug-magenta
// DrawCubeV so the player gets a visible "missing asset" marker
// rather than a silent gap. Migration is incremental — entities not
// yet converted to OBJ keep their procedural render path.
// ====================================================================
namespace tsmesh {

class MeshRegistry {
public:
  MeshRegistry() = default;
  MeshRegistry(const MeshRegistry &) = delete;
  MeshRegistry &operator=(const MeshRegistry &) = delete;

  // Scan the meshes/ directory and load every OBJ that matches a
  // known entity-type filename. Safe to call before InitWindow has
  // built a GL context — actual GPU upload happens here, so call
  // AFTER InitWindow.
  void loadAll(const std::filesystem::path &meshesDir);
  void unloadAll();

  // True if this entity type has a mesh loaded. Render code uses
  // this to decide whether to call draw() or fall through to the
  // procedural path for entities not yet migrated.
  bool has(EntityType type) const;

  // Draw the mesh for `type` at the given world pose. yawRad is in
  // radians (matches the rest of the codebase); converted to degrees
  // for raylib's DrawModelEx. Scale is a uniform multiplier.
  // tint blends with the per-vertex palette colour baked into the
  // mesh (use WHITE for no tint, or e.g. a damage-flash white-out).
  void draw(EntityType type, Vector3 position, float yawRad,
            float scale, Color tint) const;

  // Player mesh is a separate slot — the Player class isn't an entry
  // in EntityType (yet), so it needs its own storage. Has-check +
  // raw Model pointer so Player::render can apply its own full
  // roll/pitch/yaw matrix via DrawMesh (rather than the simpler
  // yaw-only DrawModelEx the other entities use).
  bool hasPlayer() const { return m_playerLoaded; }
  const Model *playerModel() const {
    return m_playerLoaded ? &m_playerModel : nullptr;
  }

private:
  // Filename for each entity type, looked up under meshesDir/. Empty
  // string = type has no mesh yet (use procedural fallback).
  static const char *filenameFor(EntityType type);

  // Indexed by EntityType (cast to size_t). Static array sized one
  // larger than the largest enum value so growth doesn't need a rehash.
  static constexpr size_t kSlotCount = 16;
  std::array<Model, kSlotCount> m_models{};
  std::array<bool, kSlotCount> m_loaded{};
  // Player mesh sits outside the EntityType-indexed array because
  // the Player class doesn't live in the entity pool.
  Model m_playerModel{};
  bool m_playerLoaded = false;
};

} // namespace Mesh
