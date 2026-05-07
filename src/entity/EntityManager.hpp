#pragma once

#include "Entity.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include <array>
#include <cstdint>
#include <vector>

class Planet;
class Player;
class ParticleSystem;

// ====================================================================
// EntityManager — pre-allocated pools for enemies/friendlies and for
// projectiles, plus a simple 2D spatial grid (cell size
// SPATIAL_CELL_SIZE) for O(1) nearest-entity queries.
//
// Why two pools: projectiles update at higher cadence and have a
// completely different update body (no AI, just position + lifetime
// + collision). Splitting them keeps the entity update tight and
// avoids type-dispatch on every iteration.
//
// All pools are flat std::array — no heap allocation in the hot path.
// Round-robin allocator with oldest-first recycling on overflow.
// ====================================================================

class EntityManager {
public:
  EntityManager() = default;

  void clear();

  // Dev-only — kill every alive enemy without triggering kill bursts
  // or scoring. Used by the F7 skip-wave hotkey to advance through the
  // wave table quickly during development. Projectiles are left alone
  // so any in-flight cannon shots still complete their travel.
  void killAllEnemies();

  // Spawn helpers — return pointer to the slot, or nullptr if pool is
  // full and no slot can be recycled (rare with current sizes).
  Entity *spawnEnemy(EntityType type, Vector3 pos);
  Entity *spawnProjectile(Vector3 pos, Vector3 vel, float damage,
                          float range, float speed, ProjectileOwner owner);

  void update(float dt, const Planet &planet, Player &player,
              ParticleSystem &particles);
  void render() const;

  // Debug / HUD
  int liveEnemyCount() const { return m_liveEnemies; }
  int liveProjectileCount() const { return m_liveProjectiles; }

  // Visual feedback emitters — public so GameState can fire the
  // player's final death explosion at the crash site.
  void emitHitBurst(Vector3 pos, ParticleSystem &particles);
  void emitKillExplosion(Vector3 pos, ParticleSystem &particles);

  // Entity iteration for camera threat scoring etc. — read-only.
  const std::array<Entity, Config::ENTITY_POOL_SIZE> &entities() const {
    return m_entities;
  }

private:
  std::array<Entity, Config::ENTITY_POOL_SIZE> m_entities{};
  std::array<Entity, Config::PROJECTILE_POOL_SIZE> m_projectiles{};
  size_t m_nextEntity = 0;
  size_t m_nextProjectile = 0;
  int m_liveEnemies = 0;
  int m_liveProjectiles = 0;
  uint32_t m_nextId = 1;

  // Per-entity update bodies
  void updateFighter(Entity &e, float dt, const Planet &planet,
                     const Player &player, ParticleSystem &particles);
  void fireFighterShot(Entity &e, const Player &player);

  // Drone — boids flocking + pursuit + kamikaze contact damage.
  void updateDrone(Entity &e, float dt, const Planet &planet,
                   Player &player, ParticleSystem &particles);

  // Bomber — heavier, slower Fighter variant. Same state machine,
  // different fire rhythm + handling.
  void updateBomber(Entity &e, float dt, const Planet &planet,
                    const Player &player, ParticleSystem &particles);
  void fireBomberShot(Entity &e, const Player &player);

  // Seeder — slow drift + periodic drone deployment.
  void updateSeeder(Entity &e, float dt, const Planet &planet,
                    const Player &player);

  // Ground Turret — stationary, rotates toward player, fires in cone.
  void updateGroundTurret(Entity &e, float dt, const Planet &planet,
                          const Player &player);
  void fireTurretShot(Entity &e, const Player &player);

  // Projectile update + collision
  void updateProjectile(Entity &p, float dt, const Planet &planet,
                        Player &player, ParticleSystem &particles);

  // Damage application — handles shield → hull routing and timeSinceHit.
  // Emits a kill burst when the target dies.
  void applyDamage(Entity &target, float damage, ParticleSystem &particles);

  // Helpers
  Entity *allocEnemy();
  Entity *allocProjectile();

  // Render helpers
  void renderEnemy(const Entity &e) const;
  void renderProjectile(const Entity &p) const;
};
