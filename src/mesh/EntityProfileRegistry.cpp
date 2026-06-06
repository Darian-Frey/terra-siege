#include "EntityProfileRegistry.hpp"

#include <cstdio>

namespace tsmesh {

const char *EntityProfileRegistry::filenameFor(EntityType type) {
  // Names match the .meta.json files in assets/meshes/. Mirrors
  // MeshRegistry::filenameFor exactly, swapping the extension.
  switch (type) {
  case EntityType::Drone:         return "drone.meta.json";
  case EntityType::Seeder:        return "seeder.meta.json";
  case EntityType::Fighter:       return "fighter.meta.json";
  case EntityType::Bomber:        return "bomber.meta.json";
  case EntityType::Carrier:       return "carrier.meta.json";
  case EntityType::GroundTurret:  return "tank.meta.json";
  case EntityType::Collector:     return "collector.meta.json";
  case EntityType::RepairStation: return "repair_station.meta.json";
  case EntityType::RadarBooster:  return "radar_booster.meta.json";
  case EntityType::Base:          return "base.meta.json";
  default: return "";
  }
}

void EntityProfileRegistry::loadAll(const std::filesystem::path &meshesDir) {
  for (size_t i = 0; i < kSlotCount; ++i) {
    m_loaded[i] = false;
    m_profiles[i] = EntityProfile{};
  }

  // Walk the EntityType enum slots. Missing files are silent; parse
  // failures surface as stderr warnings so authoring bugs aren't
  // hidden.
  for (size_t i = 0; i < kSlotCount; ++i) {
    EntityType type = static_cast<EntityType>(i);
    const char *fn = filenameFor(type);
    if (!fn || !*fn) continue;
    std::filesystem::path p = meshesDir / fn;
    if (!std::filesystem::exists(p)) continue;

    if (loadProfile(p, m_profiles[i])) {
      m_loaded[i] = true;
      for (const auto &w : m_profiles[i].warnings) {
        std::fprintf(stderr, "[EntityProfileRegistry] %s: %s\n",
                     fn, w.c_str());
      }
    } else {
      // Load failed (parse error). Warnings already populated.
      for (const auto &w : m_profiles[i].warnings) {
        std::fprintf(stderr, "[EntityProfileRegistry] %s: %s\n",
                     fn, w.c_str());
      }
    }
  }
}

bool EntityProfileRegistry::has(EntityType type) const {
  size_t idx = static_cast<size_t>(type);
  if (idx >= kSlotCount) return false;
  return m_loaded[idx];
}

const EntityProfile *
EntityProfileRegistry::get(EntityType type) const {
  size_t idx = static_cast<size_t>(type);
  if (idx >= kSlotCount || !m_loaded[idx]) return nullptr;
  return &m_profiles[idx];
}

} // namespace tsmesh
