#include "MeshRegistry.hpp"
#include "ObjLoader.hpp"
#include "raymath.h" // static inline here: no RAYMATH_IMPLEMENTATION in the mesh lib (AV-013)

#include <cmath>
#include <cstdio>

namespace tsmesh {

namespace {
// Axis and angle (degrees) that turn a model's +Z onto dir: axis = Z × dir.
void rotationOntoDir(Vector3 dir, Vector3 &axis, float &angleDeg) {
  axis = {0.0f, 1.0f, 0.0f};
  angleDeg = 0.0f;
  float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (len <= 1e-4f) return;
  Vector3 d = {dir.x / len, dir.y / len, dir.z / len};
  Vector3 a = {-d.y, d.x, 0.0f};
  float al = sqrtf(a.x * a.x + a.y * a.y);
  float c = fminf(fmaxf(d.z, -1.0f), 1.0f);
  if (al > 1e-5f) {
    axis = {a.x / al, a.y / al, 0.0f};
    angleDeg = acosf(c) * (180.0f / 3.14159265f);
  } else if (c < 0.0f) {
    angleDeg = 180.0f; // pointing straight back along −Z
  }
}
} // namespace

const char *MeshRegistry::filenameFor(EntityType type) {
  // Empty string = no mesh yet, fall through to the procedural
  // render path. Names match the OBJ files in assets/meshes/.
  switch (type) {
  case EntityType::Drone:        return "drone.obj";
  case EntityType::Seeder:       return "seeder.obj";
  case EntityType::Fighter:      return "fighter.obj";
  case EntityType::Bomber:       return "bomber.obj";
  case EntityType::Carrier:      return "carrier.obj";
  case EntityType::Lander:       return "lander.obj";
  case EntityType::GroundTurret: return "tank.obj";
  case EntityType::Collector:    return "collector.obj";
  case EntityType::RepairStation:return "repair_station.obj";
  case EntityType::RadarBooster: return "radar_booster.obj";
  case EntityType::Base:         return "base.obj";
  case EntityType::RadarStation: return "radar_station.obj";
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
  m_missileLoaded = loadInto("missile.obj", m_missileModel);
  m_turretLoaded = loadInto("base_turret.obj", m_turretModel);
  m_gunsLoaded = loadInto("base_turret_guns.obj", m_gunsModel);
  m_dishLoaded = loadInto("radar_booster_dish.obj", m_dishModel);
  static const char *const kPropFiles[kPropCount] = {
      "bolt.obj", "orb.obj", "depth_charge.obj", "base_platform.obj",
      "base_ramp.obj"};
  for (int i = 0; i < kPropCount; ++i)
    m_propLoaded[i] = loadInto(kPropFiles[i], m_propModel[i]);
  m_cabLoaded[0] = loadInto("collector_cab_empty.obj", m_cabModel[0]);
  m_cabLoaded[1] = loadInto("collector_cab_loaded.obj", m_cabModel[1]);
}

BoundingBox MeshRegistry::propBounds(Prop p) const {
  const int i = static_cast<int>(p);
  return m_propLoaded[i] ? GetModelBoundingBox(m_propModel[i]) : BoundingBox{};
}

void MeshRegistry::drawProp(Prop p, Vector3 position, Vector3 dir,
                            Vector3 scale, Color tint) const {
  const int i = static_cast<int>(p);
  if (!m_propLoaded[i]) return;
  Vector3 axis;
  float angleDeg;
  rotationOntoDir(dir, axis, angleDeg);
  DrawModelEx(m_propModel[i], position, axis, angleDeg, scale, tint);
}

void MeshRegistry::drawBoosterDish(Vector3 position, float yawRad,
                                   Color tint) const {
  if (!m_dishLoaded) return;
  float yawDeg = yawRad * (180.0f / 3.14159265f);
  DrawModelEx(m_dishModel, position, {0.0f, 1.0f, 0.0f}, yawDeg,
              {1.0f, 1.0f, 1.0f}, tint);
}

void MeshRegistry::drawCollectorCab(Vector3 position, float yawRad,
                                    bool loaded, Color tint) const {
  const int i = loaded ? 1 : 0;
  if (!m_cabLoaded[i]) return;
  float yawDeg = yawRad * (180.0f / 3.14159265f);
  DrawModelEx(m_cabModel[i], position, {0.0f, 1.0f, 0.0f}, yawDeg,
              {1.0f, 1.0f, 1.0f}, tint);
}

void MeshRegistry::drawBaseTurret(Vector3 axisPos, float yawRad, float scale,
                                  Color tint) const {
  if (!m_turretLoaded) return;
  float yawDeg = yawRad * (180.0f / 3.14159265f);
  DrawModelEx(m_turretModel, axisPos, {0.0f, 1.0f, 0.0f}, yawDeg,
              {scale, scale, scale}, tint);
}

void MeshRegistry::drawBaseTurretGuns(Vector3 pivotPos, float yawRad,
                                      float pitchRad, float scale) const {
  if (!m_gunsLoaded) return;
  // Scale, pitch (rotating +Z up needs a negative X rotation), yaw, then
  // move to the hinge — applied in that order (see AV-013 on
  // MatrixMultiply).
  Matrix m = MatrixMultiply(
      MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale),
                                    MatrixRotateX(-pitchRad)),
                     MatrixRotateY(yawRad)),
      MatrixTranslate(pivotPos.x, pivotPos.y, pivotPos.z));
  DrawMesh(m_gunsModel.meshes[0], m_gunsModel.materials[0], m);
}

void MeshRegistry::drawMissile(Vector3 position, Vector3 dir, float scale,
                               Color tint) const {
  if (!m_missileLoaded) return;
  Vector3 axis;
  float angleDeg;
  rotationOntoDir(dir, axis, angleDeg);
  DrawModelEx(m_missileModel, position, axis, angleDeg, {scale, scale, scale},
              tint);
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
  if (m_missileLoaded) {
    UnloadModel(m_missileModel);
    m_missileLoaded = false;
  }
  if (m_turretLoaded) {
    UnloadModel(m_turretModel);
    m_turretLoaded = false;
  }
  if (m_gunsLoaded) {
    UnloadModel(m_gunsModel);
    m_gunsLoaded = false;
  }
  for (int i = 0; i < kPropCount; ++i) {
    if (m_propLoaded[i]) {
      UnloadModel(m_propModel[i]);
      m_propLoaded[i] = false;
    }
  }
  if (m_dishLoaded) {
    UnloadModel(m_dishModel);
    m_dishLoaded = false;
  }
  for (int i = 0; i < 2; ++i) {
    if (m_cabLoaded[i]) {
      UnloadModel(m_cabModel[i]);
      m_cabLoaded[i] = false;
    }
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
