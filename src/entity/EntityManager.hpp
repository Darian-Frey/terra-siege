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
                          float range, float speed, ProjectileOwner owner,
                          ProjectileKind kind = ProjectileKind::Cannon,
                          float splashRadius = 0.0f,
                          uint32_t seekTargetId = 0,
                          float turnRate = 0.0f);

  // Find the nearest live enemy entity within a forward cone from origin
  // (cosine threshold, dot(forward, toEnemyNormalised) >= cosHalfAngle).
  // Returns the entity id, or 0 if no target is in cone or within range.
  // Used by GameState to acquire a missile lock at fire time.
  uint32_t acquireTarget(Vector3 origin, Vector3 forward,
                         float cosHalfAngle, float maxRange) const;

  // EMP area effect — set stunTimer = duration on every live enemy
  // within radius of pos. Called by GameState when the player's EMP
  // pending flag fires; no projectile involved (instant area effect).
  void applyEMPStun(Vector3 pos, float radius, float duration);

  // Beam Laser hit resolution — raycast from origin along dir up to
  // maxRange against every alive enemy. Returns the entity id of the
  // nearest hit (0 if no hit), and writes the hit distance + impact
  // point. Applies `dps * dt` damage to the hit entity. GameState
  // draws the beam line from origin to outHitPos (or origin + dir *
  // maxRange if no hit).
  uint32_t beamRaycast(Vector3 origin, Vector3 dir, float maxRange,
                       float damageThisTick, ParticleSystem &particles,
                       Vector3 &outHitPos);

  void update(float dt, const Planet &planet, Player &player,
              ParticleSystem &particles);
  void render() const;

  // Debug / HUD
  int liveEnemyCount() const { return m_liveEnemies; }
  int liveProjectileCount() const { return m_liveProjectiles; }

  // Friendly-side queries. Used by Radar (booster range boost), Bomber
  // AI (nearest friendly target preference), and GameState (game-over
  // when alive == 0 but total > 0).
  bool anyRadarBoosterAlive() const;
  int liveFriendlyCount() const;
  // Nearest live friendly entity. Returns 0 if no friendlies alive.
  // outPos is filled with the target's world position on success.
  uint32_t nearestFriendly(Vector3 origin, Vector3 &outPos) const;
  // Nearest live Base entity (for Collector delivery loops). Returns
  // 0 if no Base alive; collectors then idle in place.
  uint32_t nearestBase(Vector3 origin, Vector3 &outPos) const;

  // Collector delivery counter. Incremented inside updateCollector
  // each time a collector returns to a Base and finishes unloading.
  // GameState reads this for the score HUD; resets via clear().
  int deliveryCount() const { return m_deliveryCount; }

  // Visual feedback emitters — public so GameState can fire the
  // player's final death explosion at the crash site.
  void emitHitBurst(Vector3 pos, ParticleSystem &particles);
  void emitKillExplosion(Vector3 pos, ParticleSystem &particles);

  // Entity iteration for camera threat scoring etc. — read-only.
  const std::array<Entity, Config::ENTITY_POOL_SIZE> &entities() const {
    return m_entities;
  }
  // Projectile iteration — used by Radar tier 2's incoming-missile
  // warning. Read-only; consumers should check `alive` per slot.
  const std::array<Entity, Config::PROJECTILE_POOL_SIZE> &
  projectiles() const {
    return m_projectiles;
  }

private:
  std::array<Entity, Config::ENTITY_POOL_SIZE> m_entities{};
  std::array<Entity, Config::PROJECTILE_POOL_SIZE> m_projectiles{};
  size_t m_nextEntity = 0;
  size_t m_nextProjectile = 0;
  int m_liveEnemies = 0;
  int m_liveProjectiles = 0;
  int m_deliveryCount = 0; // Collector deliveries completed this round
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
  void fireBomberShot(Entity &e, Vector3 targetPos);

  // Seeder — slow drift + periodic drone deployment.
  void updateSeeder(Entity &e, float dt, const Planet &planet,
                    const Player &player);

  // Carrier — boss-tier. 4-sector shield, hovers high, continuously
  // deploys drones. Same hover-and-drift movement pattern as Seeder
  // but slower drift speed and much higher altitude.
  void updateCarrier(Entity &e, float dt, const Planet &planet,
                     const Player &player);

  // Friendly units — ground installations the player defends.
  // Collector roams between waypoints; RepairStation heals the player
  // on proximity; RadarBooster is passive but extends radar range
  // while alive (state read from EntityManager by Radar).
  void updateCollector(Entity &e, float dt, const Planet &planet);
  void updateRepairStation(Entity &e, float dt, Player &player);
  void updateRadarBooster(Entity &e, float dt);
  // Base — passive friendly building, Collector delivery destination.
  // No movement; only renders + animates a landing-light yaw.
  void updateBase(Entity &e, float dt);

  // Ground Turret — stationary, rotates toward player, fires in cone.
  void updateGroundTurret(Entity &e, float dt, const Planet &planet,
                          const Player &player);
  void fireTurretShot(Entity &e, const Player &player);

  // Projectile update + collision
  void updateProjectile(Entity &p, float dt, const Planet &planet,
                        Player &player, ParticleSystem &particles);

  // Splash damage application — apply damage to all live enemies in
  // radius. Used by Plasma + Missile on detonation; calls applyDamage
  // per affected target so shield routing and kill bursts fire
  // normally for each.
  void applySplashDamage(Vector3 pos, float radius, float damage,
                         ParticleSystem &particles);

  // Damage application — handles shield → hull routing and timeSinceHit.
  // Emits a kill burst when the target dies. hitPos is the world-space
  // impact point; used to determine which directional sector takes
  // damage on multi-sector enemies (Carrier). For single-shield
  // entities the hitPos is ignored and the scalar shield drains as before.
  void applyDamage(Entity &target, float damage, Vector3 hitPos,
                   ParticleSystem &particles);

  // Resolve which directional shield sector takes a hit, given the
  // world-space hit position and the target's pose. Returns a
  // ShieldSector enum cast to int (0..3). Exposed for unit tests /
  // future weapon types that bypass sectors (EMP, depth charge, etc).
  int damageSectorFromHit(const Entity &target, Vector3 hitPos) const;

  // Helpers
  Entity *allocEnemy();
  Entity *allocProjectile();

  // Render helpers
  void renderEnemy(const Entity &e) const;
  void renderProjectile(const Entity &p) const;
};
