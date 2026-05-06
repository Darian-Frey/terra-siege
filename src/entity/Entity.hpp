#pragma once

#include "raylib.h"
#include <cstdint>

// ====================================================================
// Entity — single type-tagged struct in a flat pool. Enemies, friendly
// units, and projectiles all share this layout. Combat is the source
// of truth for the field set: see terra_rebuild/combat_tuning.md.
//
// Design choices:
// - One struct, type tag, single pool — flat memory, easy iteration.
// - Shield HP is omnidirectional for v1 (Fighter/Bomber). Carrier's
//   four-sector shield will use a separate layered structure when
//   that enemy lands; for now Carrier is just a higher-HP omni shield.
// - Projectiles share the struct: vel + lifetime + damage stored in
//   the same fields, distinguished by `type == Projectile`.
// ====================================================================

enum class EntityType : uint8_t {
  None = 0,
  // Enemies
  Drone,
  Seeder,
  Fighter,
  Bomber,
  Carrier,
  GroundTurret,
  // Friendlies
  Collector,
  RepairStation,
  RadarBooster,
  // Other
  Projectile,
};

enum class ProjectileOwner : uint8_t {
  Player = 0,
  Enemy = 1,
};

// AI state machine — Drones and projectiles ignore this.
enum class AIState : uint8_t {
  Idle = 0,
  Pursue,
  Attack,
  Evade,
  StrafeFriendly, // Bomber-only
};

struct Entity {
  // Identity
  EntityType type = EntityType::None;
  uint32_t id = 0;
  bool alive = false;

  // Transform
  Vector3 pos = {0, 0, 0};
  Vector3 vel = {0, 0, 0};
  float yaw = 0.0f;

  // Combat
  float hullHP = 0.0f;
  float hullMax = 0.0f;
  float shieldHP = 0.0f;
  float shieldMax = 0.0f;
  float shieldDelay = 0.0f; // seconds before recharge starts
  float shieldRate = 0.0f;  // HP per second while recharging
  float timeSinceHit = 0.0f;

  // Visual / collision
  float radius = 1.0f;

  // Damage flash — render path tints the body white while > 0 so hits
  // are visually unmistakable. Set in applyDamage, decays per tick.
  float damageFlashTimer = 0.0f;

  // AI state
  AIState aiState = AIState::Idle;
  float stateTimer = 0.0f;
  float fireTimer = 0.0f;

  // Projectile-specific (overlaps the unused AI fields by reuse below)
  float lifeRemaining = 0.0f; // seconds before projectile expires
  float damage = 0.0f;        // damage dealt on contact
  ProjectileOwner owner = ProjectileOwner::Player;
};
