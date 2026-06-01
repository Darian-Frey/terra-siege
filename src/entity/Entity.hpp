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
  Base, // delivery destination for Collectors; counts as friendly
  // Other
  Projectile,
};

enum class ProjectileOwner : uint8_t {
  Player = 0,
  Enemy = 1,
};

// Projectile sub-type — switches behaviour inside updateProjectile.
// Cannon = straight-line tracer (default).
// Plasma = straight-line + splash radius damage on impact.
// Missile = seeking projectile using proportional-navigation guidance;
//          turns toward seekTargetId at turnRate rad/s. Lock is set
//          at fire time and held even if the target moves.
enum class ProjectileKind : uint8_t {
  Cannon = 0,
  Plasma,
  Missile,
  // Heavy gravity-bomb: no propulsion, falls under gravity, big
  // splash radius on terrain impact. Used for Depth Charge.
  DepthCharge,
  // Cluster parent carrier — flies like a missile until it nears the
  // target, then splits into 4 child Missiles that spread out and
  // reacquire individually. Does NOT damage anything itself; its
  // job is delivery.
  ClusterParent,
};

// AI state machine — Drones and projectiles ignore this. The
// Collector-* values run a separate state machine (delivery loop)
// and never overlap with the enemy aiStates above.
enum class AIState : uint8_t {
  Idle = 0,
  Pursue,
  Attack,
  Evade,
  StrafeFriendly, // Bomber-only
  // Collector economy loop — runs out from base to a pickup site,
  // dwells, returns to base, dwells, scores a delivery, repeats.
  CollectorOutbound,
  CollectorPickup,
  CollectorInbound,
  CollectorUnload,
};

// Shield sector — for entities with directional (4-quadrant) shields
// (Carrier today; player in Phase 4). Order matters: damage routing
// computes the sector from the hit direction expressed in local
// space, so the enum values match the dominant-axis sign convention
// used in damageSectorFromHit().
enum class ShieldSector : uint8_t {
  Front = 0, // +Z local
  Rear = 1,  // -Z local
  Right = 2, // +X local
  Left = 3,  // -X local
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
  float shieldHP = 0.0f;       // single-sector path (Fighter, Bomber)
  float shieldMax = 0.0f;
  float shieldDelay = 0.0f;    // seconds before recharge starts
  float shieldRate = 0.0f;     // HP per second while recharging
  float timeSinceHit = 0.0f;

  // 4-sector directional shield path (Carrier today; player in
  // Phase 4). When sectorMax[i] > 0 for any i, the applyDamage path
  // routes by hit-direction quadrant instead of using the scalar
  // shieldHP/shieldMax above. Each sector has its own recharge
  // timer so sustained pressure on one face doesn't suppress the
  // opposite side's regen. Indexed by ShieldSector enum.
  float sectorHP[4] = {0, 0, 0, 0};
  float sectorMax[4] = {0, 0, 0, 0};
  float sectorTimer[4] = {0, 0, 0, 0}; // per-sector time-since-hit

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
  ProjectileKind kind = ProjectileKind::Cannon;
  float splashRadius = 0.0f;  // 0 = no splash (Plasma sets this on fire)
  float turnRate = 0.0f;      // rad/s; 0 = straight-line projectile
  uint32_t seekTargetId = 0;  // for Missile: tracked target entity id

  // EMP stun — non-zero means this entity is frozen by an EMP blast.
  // AI update skips while this is positive; render path tints blue.
  float stunTimer = 0.0f;

  // Collector economy loop (5h). targetPos is the current navigation
  // goal — either a randomly-picked pickup site or the home base
  // position. hasCargo flips after a successful pickup and back to
  // false on delivery; rendering uses it for the cabin-pip colour.
  // seekTargetId on Collectors holds the home Base entity id.
  Vector3 targetPos = {0, 0, 0};
  bool hasCargo = false;
};
