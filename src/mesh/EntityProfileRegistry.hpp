#pragma once

#include "SidecarProfile.hpp"
#include "entity/Entity.hpp"

#include <array>
#include <filesystem>

// ====================================================================
// EntityProfileRegistry — startup-loaded sidecar profiles indexed by
// EntityType. Parallel to MeshRegistry: same scan-once-at-init shape,
// same EntityType-as-key, same has() / get() accessor surface.
//
// game-side use (F.2 milestone): EntityManager::allocEnemy reads
// hull / shield HP / regen / delay / collisionRadius from the
// profile when present, falling back to the existing Config::*_HP /
// HIT_RADIUS_* constants when not. Migration is per-entity-type
// opt-in — adding the registry doesn't force a flag day.
//
// Missing sidecar is fine — has() returns false, get() returns
// nullptr, callers fall back to defaults. Warnings from each load
// are forwarded to stderr so authoring bugs surface early.
// ====================================================================
namespace tsmesh {

class EntityProfileRegistry {
public:
  EntityProfileRegistry() = default;
  EntityProfileRegistry(const EntityProfileRegistry &) = delete;
  EntityProfileRegistry &operator=(const EntityProfileRegistry &) = delete;

  // Scan meshesDir for `<entity>.meta.json` files matching every
  // known EntityType. Safe to call before InitWindow — pure CPU.
  void loadAll(const std::filesystem::path &meshesDir);

  bool has(EntityType type) const;
  const EntityProfile *get(EntityType type) const;

private:
  // Sidecar filename for each entity type. Mirrors MeshRegistry's
  // filenameFor() but with the ".meta.json" extension. Empty string
  // = no sidecar expected for this type.
  static const char *filenameFor(EntityType type);

  static constexpr size_t kSlotCount = 16;
  std::array<EntityProfile, kSlotCount> m_profiles{};
  std::array<bool, kSlotCount> m_loaded{};
};

} // namespace tsmesh
