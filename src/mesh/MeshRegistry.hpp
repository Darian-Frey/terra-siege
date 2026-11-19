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

  // Guided missile mesh (missile.obj), shared by the homing Missile, the
  // Cluster carrier and its submunitions. Drawn nose-first along `dir`.
  bool hasMissile() const { return m_missileLoaded; }
  void drawMissile(Vector3 position, Vector3 dir, float scale,
                   Color tint) const;

  // Base gun turret: the housing (base_turret.obj) yaws about its axis
  // on the control tower; the guns (base_turret_guns.obj) are drawn at
  // their hinge, yawed and pitched (+ = up) to the aim.
  bool hasBaseTurret() const { return m_turretLoaded && m_gunsLoaded; }
  void drawBaseTurret(Vector3 axisPos, float yawRad, float scale,
                      Color tint) const;
  void drawBaseTurretGuns(Vector3 pivotPos, float yawRad, float pitchRad,
                          float scale) const;

  // Projectile models. Bolt and Orb are white/light grey, tinted per
  // shot (and drawn again larger with a translucent tint as the glow);
  // the depth charge carries its own colours.
  enum class Prop : uint8_t { Bolt, Orb, DepthCharge, Count };
  bool hasProp(Prop p) const { return m_propLoaded[static_cast<int>(p)]; }
  // Scaled per local axis, then +Z turned onto dir (zero dir = upright,
  // unrotated).
  void drawProp(Prop p, Vector3 position, Vector3 dir, Vector3 scale,
                Color tint) const;

  // Radar Booster dish (radar_booster_dish.obj), spun about the mast
  // axis; origin at the bearing top.
  bool hasBoosterDish() const { return m_dishLoaded; }
  void drawBoosterDish(Vector3 position, float yawRad, Color tint) const;

  // Collector cab, one model per cargo state (collector_cab_empty /
  // _loaded) since the colour shows the cargo. Same frame and origin as
  // collector.obj, so it's drawn at the Collector's own transform.
  bool hasCollectorCab() const { return m_cabLoaded[0] && m_cabLoaded[1]; }
  void drawCollectorCab(Vector3 position, float yawRad, bool loaded,
                        Color tint) const;

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
  Model m_missileModel{};
  bool m_missileLoaded = false;
  Model m_turretModel{};
  bool m_turretLoaded = false;
  Model m_gunsModel{};
  bool m_gunsLoaded = false;
  static constexpr int kPropCount = static_cast<int>(Prop::Count);
  Model m_propModel[kPropCount]{};
  bool m_propLoaded[kPropCount] = {};
  Model m_dishModel{};
  bool m_dishLoaded = false;
  Model m_cabModel[2]{}; // [0] empty, [1] loaded
  bool m_cabLoaded[2] = {false, false};
};

} // namespace Mesh
