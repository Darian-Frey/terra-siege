#include "MeshRegistry.hpp"
#include "ObjLoader.hpp"

#include <cstdio>

namespace tsmesh {

const char *MeshRegistry::filenameFor(EntityType type) {
  // Empty string = no mesh yet, fall through to the procedural
  // render path. Names match the OBJ files in assets/meshes/.
  switch (type) {
  case EntityType::Drone:        return "drone.obj";
  case EntityType::Seeder:       return "seeder.obj";
  case EntityType::Fighter:      return "fighter.obj";
  case EntityType::Bomber:       return "bomber.obj";
  case EntityType::Carrier:      return "carrier.obj";
  case EntityType::GroundTurret: return "tank.obj";
  case EntityType::Collector:    return "collector.obj";
  case EntityType::RepairStation:return "repair_station.obj";
  case EntityType::RadarBooster: return "radar_booster.obj";
  case EntityType::Base:         return "base.obj";
  // EntityType::Projectile is shared across kinds; the registry
  // doesn't try to disambiguate (cannon vs plasma vs missile vs ...)
  // because that requires the projectile kind, not just EntityType.
  // Projectile meshes are looked up separately in renderProjectile.
  default: return "";
  }
}

void MeshRegistry::loadAll(const std::filesystem::path &meshesDir) {
  for (size_t i = 0; i < kSlotCount; ++i) {
    m_loaded[i] = false;
  }
  m_playerLoaded = false;

  // Helper — load a single named OBJ into the given Model slot, or
  // log + return false if the file's missing or the load fails.
  auto loadInto = [&meshesDir](const char *filename, Model &outModel) {
    std::filesystem::path p = meshesDir / filename;
    if (!std::filesystem::exists(p)) {
      std::fprintf(stderr,
                   "[MeshRegistry] %s not found, using procedural\n",
                   p.string().c_str());
      return false;
    }
    Model m = loadModel(p);
    if (m.meshCount == 0) {
      std::fprintf(stderr, "[MeshRegistry] %s failed to load\n",
                   p.string().c_str());
      return false;
    }
    outModel = m;
    return true;
  };

  // Entity-typed meshes — one per EntityType that has a filename.
  for (size_t i = 0; i < kSlotCount; ++i) {
    EntityType type = static_cast<EntityType>(i);
    const char *fn = filenameFor(type);
    if (!fn || !*fn) continue;
    if (loadInto(fn, m_models[i])) {
      m_loaded[i] = true;
    }
  }

  // Player mesh — dedicated slot since the Player class isn't in
  // the EntityType-indexed entity pool.
  m_playerLoaded = loadInto("hovercraft.obj", m_playerModel);
}

void MeshRegistry::unloadAll() {
  for (size_t i = 0; i < kSlotCount; ++i) {
    if (m_loaded[i]) {
      UnloadModel(m_models[i]);
      m_loaded[i] = false;
    }
  }
  if (m_playerLoaded) {
    UnloadModel(m_playerModel);
    m_playerLoaded = false;
  }
}

bool MeshRegistry::has(EntityType type) const {
  size_t i = static_cast<size_t>(type);
  if (i >= kSlotCount) return false;
  return m_loaded[i];
}

void MeshRegistry::draw(EntityType type, Vector3 position, float yawRad,
                        float scale, Color tint) const {
  size_t i = static_cast<size_t>(type);
  if (i >= kSlotCount || !m_loaded[i]) {
    // Missing-mesh fallback — small magenta marker. Caller can
    // suppress by checking has() first, but rendering a visible
    // cue is better than silently hiding the entity.
    DrawCubeV(position, {2.0f, 2.0f, 2.0f}, {255, 0, 255, 255});
    return;
  }
  // DrawModelEx takes Euler rotation around an axis + a degrees angle.
  // We rotate around world-up (+Y) by yawRad converted to degrees.
  float yawDeg = yawRad * (180.0f / 3.14159265f);
  DrawModelEx(m_models[i], position, {0.0f, 1.0f, 0.0f}, yawDeg,
              {scale, scale, scale}, tint);
}

} // namespace Mesh
