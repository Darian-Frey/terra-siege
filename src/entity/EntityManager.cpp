#include "EntityManager.hpp"
#include "Player.hpp"
#include "core/Particles.hpp"
#include "mesh/EntityProfileRegistry.hpp"
#include "mesh/MeshRegistry.hpp"
#include "raymath.h"
#include "rlgl.h"
#include "shield/ShieldRenderer.hpp"
#include "world/Planet.hpp"
#include <cmath>

namespace {

// Auto-aim target predicate (Slice B.5 follow-up). Shared by the
// player auto-turret, missile lock, missile reacquire, and friendly
// base turret — any system that picks its own target without player
// input. Skips dead, projectiles, true friendlies, and infected /
// infecting entities (they're aligned with the player after the
// flip). Manual-aim weapons (Cannon, Plasma, Beam, Shield Laser)
// don't use this; the player can still hit infected ships if they
// deliberately aim at them.
inline bool isAutoAimTarget(const Entity &e) {
  if (!e.alive) return false;
  if (e.type == EntityType::Projectile) return false;
  if (e.type == EntityType::Collector ||
      e.type == EntityType::RepairStation ||
      e.type == EntityType::RadarBooster ||
      e.type == EntityType::Base)
    return false;
  if (e.aiState == AIState::Infected ||
      e.aiState == AIState::Infecting)
    return false;
  return true;
}

// Tint colour for infected / infecting entities (Slice B.4). Infected
// ships lean greenish so the player can spot former allies at a
// glance. Infecting ships flicker red↔green during the reboot. Returns
// the base colour unchanged for normal entities. For OBJ-rendered
// entities the returned colour multiplies against the baked per-vertex
// colours, so the green tint is partial; for procedural cubes the
// colour replaces the body outright.
inline Color infectionTint(Color base, AIState state, float infectionTimer) {
  if (state == AIState::Infected) {
    return Color{120, 255, 140, base.a};
  }
  if (state == AIState::Infecting) {
    // Flicker between green and base — about 5 transitions / second.
    float phase = sinf(infectionTimer * 32.0f);
    if (phase > 0.0f) return Color{120, 255, 140, base.a};
    return base;
  }
  return base;
}

// Draw a billboarded HP / shield bar. The bar always lies flat in the
// world XZ plane but is oriented perpendicular to the horizontal vector
// from entity to camera, so it reads at full width regardless of the
// ship's yaw or the player's approach angle. `yOffset` raises the bar
// above the entity centre; `width` is the full bar length at 100%;
// `t` is the fill fraction in [0,1].
inline void drawHpBar(Vector3 pos, float yOffset, float width, float t,
                      Color col, Vector3 camPos) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float dx = camPos.x - pos.x;
  float dz = camPos.z - pos.z;
  float len = sqrtf(dx * dx + dz * dz);
  Vector3 right;
  if (len < 0.001f) {
    // Camera directly above — pick a stable world axis so the bar
    // doesn't flip orientation tick-to-tick at the singularity.
    right = {1.0f, 0.0f, 0.0f};
  } else {
    // Rotate the horizontal entity→camera vector by 90° CCW in XZ to
    // get the bar's "right" direction. Horizontal so the bar stays
    // flat across the player's view regardless of altitude differences.
    right = {-dz / len, 0.0f, dx / len};
  }
  Vector3 start = {pos.x - right.x * (width * 0.5f), pos.y + yOffset,
                   pos.z - right.z * (width * 0.5f)};
  Vector3 end = {start.x + right.x * width * t, start.y,
                 start.z + right.z * width * t};
  DrawLine3D(start, end, col);
}

} // namespace

// ====================================================================
// Lifecycle / clear
// ====================================================================
void EntityManager::clear() {
  for (auto &e : m_entities) e.alive = false;
  for (auto &p : m_projectiles) p.alive = false;
  m_nextEntity = 0;
  m_nextProjectile = 0;
  m_liveEnemies = 0;
  m_liveProjectiles = 0;
  m_deliveryCount = 0;
}

// Dev hotkey support — silent enemy clear. No explosions, no scoring,
// no particle bursts; just flips the alive flag so wave-clear detection
// fires on the next tick. Projectiles are intentionally left alone.
void EntityManager::killAllEnemies() {
  for (auto &e : m_entities) {
    if (!e.alive) continue;
    e.alive = false;
  }
  m_liveEnemies = 0;
}

// ====================================================================
// Round-robin allocators — wrap the cursor; if the slot is occupied,
// recycle it (oldest-first replacement). With pool sizes 256/512 and
// the wave counts in the spec this is comfortably oversized.
// ====================================================================
Entity *EntityManager::allocEnemy() {
  size_t start = m_nextEntity;
  size_t i = start;
  do {
    Entity &slot = m_entities[i];
    if (!slot.alive) {
      m_nextEntity = (i + 1) % m_entities.size();
      return &slot;
    }
    i = (i + 1) % m_entities.size();
  } while (i != start);

  // Pool full — recycle the cursor slot
  Entity &slot = m_entities[m_nextEntity];
  if (slot.alive) --m_liveEnemies;
  size_t which = m_nextEntity;
  m_nextEntity = (m_nextEntity + 1) % m_entities.size();
  return &m_entities[which];
}

Entity *EntityManager::allocProjectile() {
  size_t start = m_nextProjectile;
  size_t i = start;
  do {
    Entity &slot = m_projectiles[i];
    if (!slot.alive) {
      m_nextProjectile = (i + 1) % m_projectiles.size();
      return &slot;
    }
    i = (i + 1) % m_projectiles.size();
  } while (i != start);

  Entity &slot = m_projectiles[m_nextProjectile];
  if (slot.alive) --m_liveProjectiles;
  size_t which = m_nextProjectile;
  m_nextProjectile = (m_nextProjectile + 1) % m_projectiles.size();
  return &m_projectiles[which];
}

// ====================================================================
// Spawn — initialise from Config-derived per-type values
// ====================================================================
Entity *EntityManager::spawnEnemy(EntityType type, Vector3 pos) {
  Entity *e = allocEnemy();
  if (!e) return nullptr;

  *e = {}; // reset all fields
  e->type = type;
  e->id = m_nextId++;
  e->alive = true;
  e->pos = pos;
  // Slice A: spawnPos is the retreat destination once hull < RETREAT_HP_THRESHOLD.
  // Recorded for every type even though only Fighter/Bomber/Seeder retreat — keeps
  // the spawn path uniform and lets Slice C re-bind this to a home-base pointer.
  e->spawnPos = pos;
  e->aiState = AIState::Pursue;

  // F.2 — sidecar profile takes precedence when present. `prof` is
  // resolved per-spawn (cheap pointer lookup) so live edits via the
  // inspector pick up on the next spawn without a restart.
  const tsmesh::EntityProfile *prof =
      m_profileRegistry ? m_profileRegistry->get(type) : nullptr;

  // F.4 + F.5 — per-entity AI / FX / infection cached fields. Set
  // universally (independent of entity type) so the AI tick paths
  // and tickDamageSmoke can read e.* values directly. Config
  // constants are the fallback when no sidecar is loaded.
  if (prof && prof->view.aiPresent) {
    e->aiDetectionRange = prof->view.detectionRange;
    e->aiAttackRange = prof->view.attackRange;
    e->aiEvadeAtHPFrac = prof->view.evadeAtHPFrac;
    e->aiRetreatHPFrac = prof->view.retreatAtHPFrac;
  } else {
    e->aiDetectionRange = Config::AI_PURSUE_RANGE;
    e->aiAttackRange = Config::AI_ATTACK_RANGE;
    e->aiEvadeAtHPFrac = Config::AI_EVADE_HEALTH;
    e->aiRetreatHPFrac = Config::RETREAT_HP_THRESHOLD;
  }
  if (prof && prof->view.infectionPresent) {
    e->aiSpeedPenaltyAfterInfect = prof->view.speedPenaltyAfter;
    e->infectionRebootDuration = prof->view.rebootDuration;
  } else {
    e->aiSpeedPenaltyAfterInfect = Config::INFECT_SPEED_PENALTY;
    e->infectionRebootDuration = Config::INFECT_REBOOT_DURATION;
  }
  if (prof && prof->view.fxPresent) {
    e->smokeAtHPFrac = prof->view.smokeAtHPFrac;
    e->deathExplosionScale = prof->view.deathExplosionScale;
  } else {
    e->smokeAtHPFrac = Config::SMOKE_HP_THRESHOLD;
    e->deathExplosionScale = 1.0f;
  }

  switch (type) {
  case EntityType::Fighter:
    // Fighter is the canonical F.2 migration target — values read
    // from fighter.meta.json when the registry is wired, else fall
    // back to the same TTK-derived Config defaults as before.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_FIGHTER;
      e->radius = Config::HIT_RADIUS_FIGHTER;
    }
    if (prof && prof->view.shieldsPresent) {
      e->shieldMax = prof->view.shieldHP;
      e->shieldRate = prof->view.shieldRegen;
      e->shieldDelay = prof->view.shieldDelay;
    } else {
      e->shieldMax = Config::SHIELD_FIGHTER;
      e->shieldRate = Config::SHIELD_RATE_FIGHTER;
      e->shieldDelay = Config::SHIELD_DELAY_FIGHTER;
    }
    e->hullHP = e->hullMax;
    e->shieldHP = e->shieldMax;
    e->canBeInfected = true; // Slice B.4
    break;
  case EntityType::Drone:
    // Light gunship swarm — 1-shot kill, no shield, no contact damage.
    // Holds a stand-off band and fires small projectiles. fireTimer
    // starts at FIRST_SHOT_DELAY so a freshly-dropped drone doesn't
    // instantly burst the player. stateTimer drives the orbital-drift
    // bearing pick; first pick happens immediately on the first tick.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_DRONE;
      e->radius = Config::HIT_RADIUS_DRONE;
    }
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->fireTimer = Config::DRONE_FIRST_SHOT_DELAY;
    e->stateTimer = 0.0f; // forces immediate bearing pick on first update
    break;
  case EntityType::Bomber:
    // Heavy bruiser — high HP, has a shield with the slowest recharge
    // delay so sustained pressure keeps the shield down.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_BOMBER;
      e->radius = Config::HIT_RADIUS_BOMBER;
    }
    if (prof && prof->view.shieldsPresent) {
      e->shieldMax = prof->view.shieldHP;
      e->shieldRate = prof->view.shieldRegen;
      e->shieldDelay = prof->view.shieldDelay;
    } else {
      e->shieldMax = Config::SHIELD_BOMBER;
      e->shieldRate = Config::SHIELD_RATE_BOMBER;
      e->shieldDelay = Config::SHIELD_DELAY_BOMBER;
    }
    e->hullHP = e->hullMax;
    e->shieldHP = e->shieldMax;
    e->canBeInfected = true; // Slice B.4
    break;
  case EntityType::Seeder:
    // Slow flying drone-dispenser. Fragile, no shield. fireTimer is
    // re-purposed as the deploy cooldown — first drop after the
    // grace delay so the seeder doesn't dump a drone the instant it
    // pops in next to the player.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_SEEDER;
      e->radius = Config::HIT_RADIUS_SEEDER;
    }
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->fireTimer = Config::SEEDER_FIRST_DROP_DELAY;
    break;
  case EntityType::Carrier:
    // Boss-tier — 4-sector directional shield, huge hull, no weapons.
    // The scalar shieldHP stays 0 (the directional-shield path is
    // triggered by sectorMax[i] > 0). shieldDelay + shieldRate are
    // shared by all sectors via the per-tick recharge loop.
    //
    // Profile.shieldHP for a "4-sector" model is interpreted as the
    // PER-SECTOR HP (matches the sidecar field's natural meaning;
    // the Config equivalent is SHIELD_CARRIER_PER_SECTOR). Total
    // shield = sector_hp × 4.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_CARRIER;
      e->radius = Config::HIT_RADIUS_CARRIER;
    }
    {
      float sectorHP, sectorRate, sectorDelay;
      if (prof && prof->view.shieldsPresent) {
        sectorHP = prof->view.shieldHP;
        sectorRate = prof->view.shieldRegen;
        sectorDelay = prof->view.shieldDelay;
      } else {
        sectorHP = Config::SHIELD_CARRIER_PER_SECTOR;
        sectorRate = Config::SHIELD_RATE_CARRIER;
        sectorDelay = Config::SHIELD_DELAY_CARRIER;
      }
      e->shieldRate = sectorRate;
      e->shieldDelay = sectorDelay;
      for (int i = 0; i < 4; ++i) {
        e->sectorMax[i] = sectorHP;
        e->sectorHP[i] = sectorHP;
        e->sectorTimer[i] = 0.0f;
      }
    }
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    // fireTimer doubles as drone-deploy cooldown (same as Seeder).
    e->fireTimer = Config::CARRIER_FIRST_DROP_DELAY;
    break;
  case EntityType::GroundTurret:
    // Stationary ground threat. yaw is the barrel direction (starts
    // pointing forward by convention; updateGroundTurret rotates it
    // toward the player). pos.y is anchored to terrain at spawn.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::HULL_TURRET;
      e->radius = Config::HIT_RADIUS_TURRET;
    }
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->aiState = AIState::Idle; // turret has no pursue/evade — just track+fire
    break;
  case EntityType::Collector:
    // Ground vehicle that wanders between waypoints. fireTimer is
    // re-purposed as the waypoint-pick cooldown. stateTimer holds
    // the current yaw target (faked: encoded in vel direction).
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::COLLECTOR_HULL;
      e->radius = Config::HIT_RADIUS_COLLECTOR;
    }
    e->hullHP = e->hullMax;
    // Initial heading: random-ish from spawn seed; updateCollector
    // pivots toward fresh waypoints as it walks.
    e->yaw = static_cast<float>((e->id * 1103515245u) % 6283) / 1000.0f;
    e->fireTimer = 0.0f;
    break;
  case EntityType::RepairStation:
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::REPAIR_STATION_HULL;
      e->radius = Config::HIT_RADIUS_REPAIR;
    }
    e->hullHP = e->hullMax;
    break;
  case EntityType::RadarBooster:
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::RADAR_BOOSTER_HULL;
      e->radius = Config::HIT_RADIUS_BOOSTER;
    }
    e->hullHP = e->hullMax;
    // yaw is animated by updateRadarBooster (rotating dish visual).
    e->yaw = 0.0f;
    break;
  case EntityType::Base:
    // Stationary delivery destination. High HP — losing this means
    // losing the collector economy, so it should take real effort
    // to destroy. yaw animates a landing-light beacon at render time.
    if (prof && prof->view.hullPresent) {
      e->hullMax = prof->view.hullHP;
      e->radius = prof->view.hullCollisionRadius;
    } else {
      e->hullMax = Config::BASE_HULL;
      e->radius = Config::HIT_RADIUS_BASE;
    }
    e->hullHP = e->hullMax;
    e->yaw = 0.0f;
    break;
  default:
    e->hullMax = 100.0f;
    e->hullHP = 100.0f;
    e->radius = 1.5f;
    break;
  }

  ++m_liveEnemies;
  return e;
}

Entity *EntityManager::spawnProjectile(Vector3 pos, Vector3 vel, float damage,
                                       float range, float speed,
                                       ProjectileOwner owner,
                                       ProjectileKind kind,
                                       float splashRadius,
                                       uint32_t seekTargetId,
                                       float turnRate) {
  Entity *p = allocProjectile();
  if (!p) return nullptr;

  *p = {};
  p->type = EntityType::Projectile;
  p->id = m_nextId++;
  p->alive = true;
  p->pos = pos;
  p->vel = vel;
  p->damage = damage;
  p->lifeRemaining = (speed > 0.01f) ? (range / speed) : 1.5f;
  p->radius = Config::HIT_RADIUS_PROJECTILE;
  p->owner = owner;
  p->kind = kind;
  p->splashRadius = splashRadius;
  p->seekTargetId = seekTargetId;
  p->turnRate = turnRate;

  ++m_liveProjectiles;
  return p;
}

// Forward-cone target acquisition for missile lock. O(N) over the pool
// — fine at current sizes. Picks the entity that's both inside the
// cone AND closest by straight-line distance. Friendly units are
// excluded (we'd shoot them by mistake). Returns 0 (no entity id) on
// no-target.
uint32_t EntityManager::acquireTarget(Vector3 origin, Vector3 forward,
                                      float cosHalfAngle,
                                      float maxRange) const {
  uint32_t best = 0;
  float bestDist = maxRange;
  for (const Entity &e : m_entities) {
    if (!isAutoAimTarget(e)) continue;
    Vector3 d = Vector3Subtract(e.pos, origin);
    float dist = Vector3Length(d);
    if (dist < 0.01f || dist > maxRange) continue;
    Vector3 dn = Vector3Scale(d, 1.0f / dist);
    float cosAngle = Vector3DotProduct(forward, dn);
    if (cosAngle < cosHalfAngle) continue;
    if (dist < bestDist) {
      bestDist = dist;
      best = e.id;
    }
  }
  return best;
}

// Beam Laser raycast — sphere-cast against each live enemy, find the
// nearest one whose hit-sphere is pierced by the ray within maxRange.
// Closed-form ray vs sphere: project (centre - origin) onto dir, then
// pythagoras gives perpendicular distance. Closest forward intersection
// wins; damageThisTick applied to that target's centre as the impact
// point (so directional shields still route correctly).
//
// Returns the entity id of the hit target (0 if none) and writes the
// impact world position. GameState uses outHitPos for the visual line
// endpoint — capping at maxRange when nothing was hit.
uint32_t EntityManager::beamRaycast(Vector3 origin, Vector3 dir,
                                    float maxRange, float damageThisTick,
                                    ParticleSystem &particles,
                                    Vector3 &outHitPos) {
  uint32_t bestId = 0;
  float bestT = maxRange;
  Entity *bestE = nullptr;
  // Normalise dir defensively.
  float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dl < 0.001f) {
    outHitPos = origin;
    return 0;
  }
  Vector3 dn = {dir.x / dl, dir.y / dl, dir.z / dl};
  for (Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue;
    Vector3 oc = Vector3Subtract(e.pos, origin);
    float tCentre = Vector3DotProduct(oc, dn);
    if (tCentre < 0.0f || tCentre > bestT) continue;
    float perp2 = Vector3DotProduct(oc, oc) - tCentre * tCentre;
    float r = e.radius;
    if (perp2 > r * r) continue;
    // Forward intersection — use the entry point t = tCentre - sqrt(r²-perp²).
    float tHit = tCentre - sqrtf(r * r - perp2);
    if (tHit < 0.0f) tHit = 0.0f;
    if (tHit < bestT) {
      bestT = tHit;
      bestId = e.id;
      bestE = &e;
    }
  }
  if (bestE) {
    Vector3 impact = Vector3Add(origin, Vector3Scale(dn, bestT));
    outHitPos = impact;
    // Continuous-fire damage: passed dps × dt already.
    applyDamage(*bestE, damageThisTick, impact, particles);
    return bestId;
  }
  outHitPos = Vector3Add(origin, Vector3Scale(dn, maxRange));
  return 0;
}

uint32_t EntityManager::shieldLaserRaycast(Vector3 origin, Vector3 dir,
                                           float maxRange,
                                           float shieldDmgThisTick,
                                           float hullDmgThisTick,
                                           ParticleSystem &particles,
                                           Vector3 &outHitPos) {
  // Same sphere-intersection sweep as beamRaycast — duplicated rather
  // than refactored because the damage routing diverges below and a
  // single ray helper would need a callback parameter that adds more
  // noise than it removes.
  uint32_t bestId = 0;
  float bestT = maxRange;
  Entity *bestE = nullptr;
  float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dl < 0.001f) {
    outHitPos = origin;
    return 0;
  }
  Vector3 dn = {dir.x / dl, dir.y / dl, dir.z / dl};
  for (Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue;
    Vector3 oc = Vector3Subtract(e.pos, origin);
    float tCentre = Vector3DotProduct(oc, dn);
    if (tCentre < 0.0f || tCentre > bestT) continue;
    float perp2 = Vector3DotProduct(oc, oc) - tCentre * tCentre;
    float r = e.radius;
    if (perp2 > r * r) continue;
    float tHit = tCentre - sqrtf(r * r - perp2);
    if (tHit < 0.0f) tHit = 0.0f;
    if (tHit < bestT) {
      bestT = tHit;
      bestId = e.id;
      bestE = &e;
    }
  }
  if (!bestE) {
    outHitPos = Vector3Add(origin, Vector3Scale(dn, maxRange));
    return 0;
  }
  Vector3 impact = Vector3Add(origin, Vector3Scale(dn, bestT));
  outHitPos = impact;
  applyShieldHit(*bestE, shieldDmgThisTick, hullDmgThisTick, impact,
                 particles);
  return bestId;
}

// Compute the entity-local unit direction from `target.pos` to `hitPos`
// (yaw removed). Used by the shield-impact ring buffer so the visual
// cap moves with the ship as it turns. Returns +Z (nose) as a safe
// fallback when the points coincide.
static Vector3 shieldHitDirLocal(const Entity &target, Vector3 hitPos) {
  Vector3 d = {hitPos.x - target.pos.x,
               hitPos.y - target.pos.y,
               hitPos.z - target.pos.z};
  float L = Vector3Length(d);
  if (L < 1e-4f) return Vector3{0, 0, 1};
  d = Vector3Scale(d, 1.0f / L);
  // Rotate world → ship-local by -yaw on the XZ plane.
  float c = cosf(target.yaw), s = sinf(target.yaw);
  return Vector3{ d.x * c - d.z * s, d.y, d.x * s + d.z * c };
}

// Shield-priority damage routing — drains shields and hull independently
// rather than running the shield-first-overflow path used by applyDamage.
// Reused by Shield Laser (per-tick continuous fire) and Shield Missile
// (single-impact warhead). The strategic point of both weapons is "strip
// shields fast with minimal hull bleed", so hull drains at hullDmg even
// while shields are up.
void EntityManager::applyShieldHit(Entity &target, float shieldDmg,
                                   float hullDmg, Vector3 hitPos,
                                   ParticleSystem &particles) {
  target.timeSinceHit = 0.0f;
  target.damageFlashTimer = 0.12f;

  // Shield drain — sectored vs scalar. Inline check rather than
  // calling hasDirectionalShield() because that helper is defined
  // further down the file and forward-declaring it for one caller
  // adds more noise than the four-OR test it inlines to.
  bool sectored = (target.sectorMax[0] > 0.0f ||
                   target.sectorMax[1] > 0.0f ||
                   target.sectorMax[2] > 0.0f ||
                   target.sectorMax[3] > 0.0f);
  bool shieldEngaged = false;
  if (sectored) {
    int s = damageSectorFromHit(target, hitPos);
    target.sectorTimer[s] = 0.0f;
    if (target.sectorHP[s] > 0.0f) shieldEngaged = true;
    target.sectorHP[s] -= shieldDmg;
    if (target.sectorHP[s] < 0.0f) target.sectorHP[s] = 0.0f;
  } else if (target.shieldHP > 0.0f) {
    shieldEngaged = true;
    target.shieldHP -= shieldDmg;
    if (target.shieldHP < 0.0f) target.shieldHP = 0.0f;
  }
  if (shieldEngaged) {
    shieldfx::pushImpact(target.shieldImpactDir, target.shieldImpactTimer,
                         Config::SHIELD_IMPACT_SLOTS,
                         shieldHitDirLocal(target, hitPos));
  }

  // Hull drain — always, even while shields are up.
  target.hullHP -= hullDmg;
  if (target.hullHP <= 0.0f) {
    target.hullHP = 0.0f;
    target.alive = false;
    --m_liveEnemies;
    emitKillExplosion(target.pos, particles);
  }
}

// Try to flip an entity's faction (Slice B.4). Requires canBeInfected
// and depleted shields (scalar + every sector). On success, transitions
// to AIState::Infecting and starts the reboot countdown; canBeInfected
// is set false so this is a one-way flip. Caller (B.5 Infectious
// Missile) ignores the return for now — Slice B.4 ships the state
// machine but nothing triggers it yet.
bool EntityManager::tryInfect(Entity &target) {
  if (!target.canBeInfected) return false;
  if (!target.alive) return false;
  // Scalar shield must be zero.
  if (target.shieldHP > 0.0f) return false;
  // Every directional sector must be zero.
  for (int i = 0; i < 4; ++i) {
    if (target.sectorHP[i] > 0.0f) return false;
  }
  target.aiState = AIState::Infecting;
  target.infectionTimer = target.infectionRebootDuration;
  target.canBeInfected = false;
  // Freeze velocity so the reboot freeze reads as a real pause.
  target.vel = {0.0f, 0.0f, 0.0f};
  return true;
}

// Nearest live enemy to origin, excluding excludeId and filtered by
// infection state. Used by infected ship AI to find a target — pass
// `wantInfected = false` to find un-flipped enemies. Skips friendlies
// and projectiles; ground turrets and tanks are valid targets (the
// "attack former allies" pattern is more satisfying when every red
// blip on the radar is fair game).
const Entity *EntityManager::nearestEnemyTo(Vector3 origin,
                                            uint32_t excludeId,
                                            bool wantInfected) const {
  const Entity *best = nullptr;
  float bestD2 = 1e18f;
  for (const Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.id == excludeId) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue;
    bool eIsInfected = (e.aiState == AIState::Infected ||
                        e.aiState == AIState::Infecting);
    if (eIsInfected != wantInfected) continue;
    Vector3 d = Vector3Subtract(e.pos, origin);
    float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (d2 < bestD2) {
      bestD2 = d2;
      best = &e;
    }
  }
  return best;
}

// EMP area stun — set stunTimer on every enemy within radius. Skips
// friendlies and projectiles. The stunned enemies' update bodies will
// see stunTimer > 0 and short-circuit out of AI logic.
void EntityManager::applyEMPStun(Vector3 pos, float radius, float duration) {
  float r2 = radius * radius;
  for (Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue;
    Vector3 d = Vector3Subtract(e.pos, pos);
    float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (d2 <= r2) {
      e.stunTimer = duration;
    }
  }
}

// ====================================================================
// Slice A helpers — damage-smoke + retreat-to-spawn behaviour shared
// across Fighter / Bomber / Seeder (and Carrier for smoke only).
// ====================================================================
void EntityManager::tickDamageSmoke(Entity &e, ParticleSystem &particles,
                                    float dt) {
  if (e.hullMax <= 0.0f) return;
  // F.5 — per-entity smoke threshold from sidecar (Config fallback
  // already baked in at spawn). 0 means "no smoke for this entity".
  float threshold = (e.smokeAtHPFrac > 0.0f) ? e.smokeAtHPFrac
                                              : Config::SMOKE_HP_THRESHOLD;
  float hpFrac = e.hullHP / e.hullMax;
  if (hpFrac >= threshold) return;

  // Linear emit-rate ramp from MIN at the threshold to MAX at zero HP.
  float damageFrac = 1.0f - (hpFrac / threshold);
  if (damageFrac < 0.0f) damageFrac = 0.0f;
  if (damageFrac > 1.0f) damageFrac = 1.0f;
  float emitRate =
      Config::SMOKE_EMIT_RATE_MIN +
      damageFrac * (Config::SMOKE_EMIT_RATE_MAX - Config::SMOKE_EMIT_RATE_MIN);

  e.smokeTimer -= dt;
  if (e.smokeTimer > 0.0f) return;
  // Slight drift back along the ship's velocity + gentle upward rise.
  Vector3 trailVel = Vector3Scale(e.vel, -0.3f);
  trailVel.y += 1.5f;
  // Dark brown smoke per design-doc §9.1, with the alpha edging up as
  // the ship gets closer to dead.
  unsigned char alpha = static_cast<unsigned char>(160.0f + 70.0f * damageFrac);
  Color smoke = {120, 100, 80, alpha};
  particles.emit(e.pos, trailVel, smoke, 0.45f, Config::SMOKE_LIFETIME,
                 ParticleSystem::Shape::Cube, ParticleSystem::FLAG_GRAVITY);
  e.smokeTimer = 1.0f / emitRate;
}

bool EntityManager::tickRetreat(Entity &e, float dt, const Planet &planet,
                                float maxSpeed, float turnRate,
                                float thrust) {
  Vector3 toSpawn = Vector3Subtract(e.spawnPos, e.pos);
  Vector3 toSpawnH = {toSpawn.x, 0.0f, toSpawn.z};
  float distH = Vector3Length(toSpawnH);
  float dist = Vector3Length(toSpawn);

  // Despawn on arrival — Slice C will replace this with dock + heal at
  // home base. We mark the entity dead and let the manager's alive=false
  // path skip it next tick.
  if (dist < Config::RETREAT_DESPAWN_DIST) {
    e.alive = false;
    return true;
  }

  // Engine-damage scaling: full retreat speed at the threshold (hp ==
  // 40%), tapering toward 10% at zero HP so a near-dead ship visibly
  // limps but doesn't freeze in place. F.4 — retreat threshold can
  // come from sidecar; falls back to Config::RETREAT_HP_THRESHOLD at
  // spawn so the default behaviour is unchanged.
  float retreatThr = (e.aiRetreatHPFrac > 0.0f) ? e.aiRetreatHPFrac
                                                 : Config::RETREAT_HP_THRESHOLD;
  float hpFrac = (e.hullMax > 0.0f) ? (e.hullHP / e.hullMax) : 0.0f;
  float band = hpFrac / retreatThr; // 1 at threshold, 0 at dead
  if (band < 0.1f) band = 0.1f;
  if (band > 1.0f) band = 1.0f;
  float speedMul = Config::RETREAT_SPEED_FRAC * band;

  // Turn yaw toward spawnPos.
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toSpawnH.x, toSpawnH.z);
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = turnRate * speedMul * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // Thrust forward along heading.
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  e.vel.x += fwd.x * thrust * speedMul * dt;
  e.vel.z += fwd.z * thrust * speedMul * dt;

  // Speed cap (horizontal) — damaged speed.
  float spdH = sqrtf(e.vel.x * e.vel.x + e.vel.z * e.vel.z);
  float capped = maxSpeed * speedMul;
  if (spdH > capped && spdH > 0.001f) {
    float k = capped / spdH;
    e.vel.x *= k;
    e.vel.z *= k;
  }

  // Drag.
  float drag = 0.5f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel.x *= 1.0f - drag;
  e.vel.z *= 1.0f - drag;

  // Altitude pull toward the spawn altitude — natural for ships
  // returning to their entry corridor. Gentle so the ship visibly
  // climbs / sinks rather than snapping.
  float altErr = e.spawnPos.y - e.pos.y;
  e.vel.y += altErr * 0.4f * dt;
  e.vel.y *= 1.0f - 1.0f * dt;

  // Integrate.
  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  // Don't sink into terrain (still possible at low altitude).
  float floor = planet.heightAt(e.pos.x, e.pos.z) + 5.0f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }
  return false;
}

// ====================================================================
// Update — drive enemies and tick projectiles
// ====================================================================
void EntityManager::update(float dt, const Planet &planet, Player &player,
                           ParticleSystem &particles) {
  for (auto &e : m_entities) {
    if (!e.alive) continue;

    // Shield-impact ring buffer — age every active slot.
    shieldfx::tickImpacts(e.shieldImpactTimer, Config::SHIELD_IMPACT_SLOTS, dt);

    // Shield recharge — uniform across all enemy types that have shields.
    e.timeSinceHit += dt;
    if (e.shieldMax > 0.0f && e.timeSinceHit >= e.shieldDelay &&
        e.shieldHP < e.shieldMax) {
      e.shieldHP += e.shieldRate * dt;
      if (e.shieldHP > e.shieldMax) e.shieldHP = e.shieldMax;
    }
    // Per-sector recharge — runs independently per face, so the player
    // pressuring the front sector doesn't suppress the rear's regen.
    for (int i = 0; i < 4; ++i) {
      if (e.sectorMax[i] <= 0.0f) continue;
      e.sectorTimer[i] += dt;
      if (e.sectorTimer[i] >= e.shieldDelay &&
          e.sectorHP[i] < e.sectorMax[i]) {
        e.sectorHP[i] += e.shieldRate * dt;
        if (e.sectorHP[i] > e.sectorMax[i])
          e.sectorHP[i] = e.sectorMax[i];
      }
    }

    // Damage flash decay — render path tints the body white while > 0.
    if (e.damageFlashTimer > 0.0f) e.damageFlashTimer -= dt;

    if (e.fireTimer > 0.0f) e.fireTimer -= dt;

    // EMP stun — if the enemy is stunned, decrement the timer and
    // skip its AI update entirely. Velocity is heavily damped so a
    // mid-burn fighter doesn't coast halfway across the map during
    // the stun window. Projectiles already in flight are unaffected.
    if (e.stunTimer > 0.0f) {
      e.stunTimer -= dt;
      e.vel = Vector3Scale(e.vel, 1.0f - 3.0f * dt); // ~95% damped per sec
      continue;
    }

    // Infection reboot — Slice B.4. While Infecting, the entity is
    // held in place and skips its AI update. When the timer expires
    // it transitions to Infected and the next tick runs the per-type
    // AI with the infected-targeting branch.
    if (e.aiState == AIState::Infecting) {
      e.infectionTimer -= dt;
      e.vel = Vector3Scale(e.vel, 1.0f - 6.0f * dt); // strong damp — freeze
      if (e.infectionTimer <= 0.0f) {
        e.aiState = AIState::Infected;
        e.infectionTimer = 0.0f;
        // Shields stay at zero (they were already drained — that's
        // the precondition for tryInfect).
      }
      continue;
    }

    // Infected HP bleed — Slice B.5 follow-up. The virus consumes the
    // ship's systems; hull drops at INFECT_BLEED_HP_FRAC_PS × hullMax
    // per second, eventually killing it. Uses fraction-of-max so all
    // infectable types take the same wall-clock time to die from the
    // bleed (~100s at 0.01/s). Normal kill path: emitKillExplosion on
    // hullHP hitting zero, mark dead, decrement liveEnemies, skip the
    // rest of the AI tick.
    if (e.aiState == AIState::Infected && e.hullMax > 0.0f) {
      e.hullHP -= e.hullMax * Config::INFECT_BLEED_HP_FRAC_PS * dt;
      if (e.hullHP <= 0.0f) {
        e.hullHP = 0.0f;
        e.alive = false;
        --m_liveEnemies;
        emitKillExplosion(e.pos, particles);
        continue;
      }
    }

    // Slice A — damage smoke for hull-wearing flying enemies. Drones
    // and ground entities don't smoke (they're either 1-shot or don't
    // visibly degrade). Smoke is decoupled from AI state so a wounded
    // ship leaks even while still pursuing.
    switch (e.type) {
    case EntityType::Fighter:
    case EntityType::Bomber:
    case EntityType::Seeder:
    case EntityType::Carrier:
      tickDamageSmoke(e, particles, dt);
      break;
    default:
      break;
    }

    switch (e.type) {
    case EntityType::Fighter:
      updateFighter(e, dt, planet, player, particles);
      break;
    case EntityType::Drone:
      updateDrone(e, dt, planet, player, particles);
      break;
    case EntityType::Bomber:
      updateBomber(e, dt, planet, player, particles);
      break;
    case EntityType::Seeder:
      updateSeeder(e, dt, planet, player);
      break;
    case EntityType::Carrier:
      updateCarrier(e, dt, planet, player);
      break;
    case EntityType::GroundTurret:
      updateGroundTurret(e, dt, planet, player);
      break;
    case EntityType::Collector:
      updateCollector(e, dt, planet);
      break;
    case EntityType::RepairStation:
      updateRepairStation(e, dt, player);
      break;
    case EntityType::RadarBooster:
      updateRadarBooster(e, dt);
      break;
    case EntityType::Base:
      updateBase(e, dt);
      break;
    default:
      break;
    }
  }

  for (auto &p : m_projectiles) {
    if (!p.alive) continue;
    updateProjectile(p, dt, planet, player, particles);
  }
}

// ====================================================================
// Fighter AI — simple PURSUE/ATTACK state machine.
// PURSUE: turn nose toward player, accelerate forward, hover at altitude.
// ATTACK: in firing range and rough nose alignment, fire periodically.
// ====================================================================
void EntityManager::updateFighter(Entity &e, float dt, const Planet &planet,
                                  const Player &player,
                                  ParticleSystem &particles) {
  (void)particles; // smoke is now centralised in tickDamageSmoke

  // Slice B.4 — infected fighters target the nearest un-flipped enemy
  // instead of the player. If no enemies remain, the infected ship
  // has no valid target — we don't want it firing at the player it's
  // now aligned with, so hasTarget gates firing below. Movement still
  // uses player position as a fallback so the ship doesn't lock up.
  bool isInfected = (e.aiState == AIState::Infected);
  Vector3 targetPos = player.position();
  bool hasTarget = !isInfected;
  if (isInfected) {
    const Entity *prey = nearestEnemyTo(e.pos, e.id, false);
    if (prey) {
      targetPos = prey->pos;
      hasTarget = true;
    }
  }

  Vector3 toPlayer = Vector3Subtract(targetPos, e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // Slice A — retreat preempts everything else once hull drops below
  // RETREAT_HP_THRESHOLD. Triggers before EVADE (which is 25%); the
  // ship will despawn at spawnPos before EVADE could ever take over.
  // Infected ships skip retreat — design §5.3 says they fight where
  // they are until destroyed and aren't replaced.
  if (!isInfected && e.hullMax > 0.0f &&
      (e.hullHP / e.hullMax) < e.aiRetreatHPFrac) {
    e.aiState = AIState::Retreating;
  }
  if (e.aiState == AIState::Retreating) {
    tickRetreat(e, dt, planet, Config::FIGHTER_MAX_SPEED,
                Config::FIGHTER_TURN_RATE, Config::FIGHTER_THRUST);
    return;
  }

  // Infection speed penalty — applies as a multiplier on max speed.
  // F.4 — pulled from per-entity sidecar cache.
  float infectMul = isInfected ? e.aiSpeedPenaltyAfterInfect : 1.0f;

  // EVADE state — fighter peels AWAY from the player when its hull
  // drops below aiEvadeAtHPFrac (default 25%). Exit when it has put
  // enough distance between itself and the player to recover. Now
  // effectively dead code (Retreating preempts at 40%), kept as a
  // fallback if aiRetreatHPFrac ever moves below aiEvadeAtHPFrac.
  // Suppressed for infected ships so the Infected aiState isn't
  // overwritten when the bleed drops HP into the EVADE band.
  bool wantEvade = !isInfected && (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < e.aiEvadeAtHPFrac);
  if (wantEvade && dist < e.aiDetectionRange * 1.5f) {
    e.aiState = AIState::Evade;
  } else if (e.aiState == AIState::Evade && !wantEvade) {
    e.aiState = AIState::Pursue;
  }

  // Engine-damage penalty — only active in EVADE. Scales with HP
  // remaining within the evade band: at the threshold (just entered
  // EVADE) the fighter is at 60% of normal performance; at 0 HP it's
  // limping at 25%. Affects thrust, top speed, AND turn rate so the
  // damaged ship reads as sluggish on every axis.
  float evadeScale = 1.0f;
  if (e.aiState == AIState::Evade && e.hullMax > 0.0f) {
    float hpFrac = e.hullHP / e.hullMax; // 0 .. aiEvadeAtHPFrac
    float bandT = 1.0f - (hpFrac / e.aiEvadeAtHPFrac);
    if (bandT < 0.0f) bandT = 0.0f;
    if (bandT > 1.0f) bandT = 1.0f;
    evadeScale = 0.60f - 0.35f * bandT; // 0.60 → 0.25
  }

  // Desired yaw — point at player normally, AWAY from player in EVADE.
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toPlayerH.x, toPlayerH.z);
    if (e.aiState == AIState::Evade) {
      desiredYaw += 3.14159f; // 180° — face away
    }
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    // Damaged steering — turn rate scaled by evadeScale in EVADE.
    float maxStep = Config::FIGHTER_TURN_RATE * evadeScale * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // State transition: ATTACK when in range and nose roughly aligned.
  // Infected ships use the same Pursue/Attack pattern — they're just
  // pointing at a different target.
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  if (e.aiState != AIState::Evade && !isInfected) {
    if (dist < e.aiAttackRange) {
      e.aiState = AIState::Attack;
    } else if (dist < e.aiDetectionRange) {
      e.aiState = AIState::Pursue;
    }
  }
  // Infected ships keep AIState::Infected but use the Attack-style
  // firing condition below — handled by the willFire flag. Gated on
  // hasTarget so an infected ship with no live enemy doesn't waste
  // shots at the player position (its projectiles wouldn't damage
  // the player anyway, but the visual would read wrong).
  bool willFire = (e.aiState == AIState::Attack) ||
                  (isInfected && hasTarget &&
                   dist < e.aiAttackRange);

  // Thrust — composed: eased on ATTACK; further reduced by evadeScale
  // and infection penalty.
  float thrustScale = willFire ? 0.4f : 1.0f;
  thrustScale *= evadeScale * infectMul;
  e.vel.x += fwd.x * Config::FIGHTER_THRUST * thrustScale * dt;
  e.vel.z += fwd.z * Config::FIGHTER_THRUST * thrustScale * dt;

  // Altitude hold — try to stay at FIGHTER_PREFERRED_ALT above terrain
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::FIGHTER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.5f * dt; // gentle restoring force
  e.vel.y *= 1.0f - 1.0f * dt;   // damping so it doesn't oscillate

  // Speed cap — scaled by evadeScale + infection penalty so a damaged
  // and / or infected fighter can't sprint at full speed.
  float spd = Vector3Length(e.vel);
  float maxSpeed = Config::FIGHTER_MAX_SPEED * evadeScale * infectMul;
  if (spd > maxSpeed) {
    e.vel = Vector3Scale(e.vel, maxSpeed / spd);
  }

  // Drag (similar to player — coasts but slows)
  float drag = 0.5f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel.x *= 1.0f - drag;
  e.vel.z *= 1.0f - drag;

  // Integrate
  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  // Don't sink into terrain
  float floor = planet.heightAt(e.pos.x, e.pos.z) + 5.0f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }

  // Fire when in attack range and nose aligned within ~15°. For
  // infected fighters, the target is the nearest enemy (computed up
  // top); projectile owner flips to Player so the shot damages the
  // former allies instead of bouncing off them.
  if (willFire && e.fireTimer <= 0.0f) {
    Vector3 toTargetN =
        Vector3Normalize(Vector3Subtract(targetPos, e.pos));
    Vector3 fwd3 = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd3, toTargetN);
    if (dot > 0.96f) { // ~16° cone
      fireFighterShot(e, targetPos, isInfected);
      e.fireTimer = Config::FIGHTER_FIRE_RATE;
    }
  }
}

void EntityManager::fireFighterShot(Entity &e, Vector3 targetPos,
                                    bool infected) {
  // Lead-less aim — fire straight at the target's current position.
  // Player can dodge by moving sideways. Lead-aim comes when AI gets
  // smarter.
  Vector3 toTarget = Vector3Subtract(targetPos, e.pos);
  float d = Vector3Length(toTarget);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toTarget, 1.0f / d);
  Vector3 vel = Vector3Scale(dir, Config::FIGHTER_PROJ_SPEED);
  // Spawn just in front of the fighter
  Vector3 spawn = Vector3Add(e.pos, Vector3Scale(dir, 1.5f));
  // Infected fighter's shots are routed as player-owned so they
  // damage other enemies (and skip the player + friendlies).
  ProjectileOwner owner =
      infected ? ProjectileOwner::Player : ProjectileOwner::Enemy;
  spawnProjectile(spawn, vel, Config::FIGHTER_FIRE_DAMAGE,
                  Config::FIGHTER_PROJ_RANGE,
                  Config::FIGHTER_PROJ_SPEED, owner);
}

// ====================================================================
// Bomber — heavier, slower Fighter variant. Same PURSUE/ATTACK/EVADE
// state machine but with: lower turn rate (player can out-strafe a
// committed bomber), lower top speed, slow but punishing fire pattern
// (~31 DPS in 25-damage chunks), heavy hull + shield. STRAFE_FRIENDLY
// state is deferred to 5g — when friendly units land, that branch
// will redirect the bomber away from the player and onto whatever
// friendly target is nearest.
//
// Same engine-damage limp-home behaviour as Fighter when hull drops
// into the EVADE band (≤25%): scale thrust + turn + top-speed and
// emit a smoke trail. Reuses the same evadeScale formula since the
// "limping" feel should be consistent across enemy types.
// ====================================================================
void EntityManager::updateBomber(Entity &e, float dt, const Planet &planet,
                                 const Player &player,
                                 ParticleSystem &particles) {
  (void)particles; // smoke is now centralised in tickDamageSmoke

  // Slice A — retreat preempts target selection. Wounded bombers limp
  // back to spawn rather than continuing a strafe run.
  // Slice B.4 — infected bombers attack the nearest un-flipped enemy
  // instead of player + friendlies. Skip the standard target-selection
  // path entirely in that case.
  bool isInfected = (e.aiState == AIState::Infected);

  if (!isInfected && e.hullMax > 0.0f &&
      (e.hullHP / e.hullMax) < e.aiRetreatHPFrac) {
    e.aiState = AIState::Retreating;
  }
  if (e.aiState == AIState::Retreating) {
    tickRetreat(e, dt, planet, Config::BOMBER_MAX_SPEED,
                Config::BOMBER_TURN_RATE, Config::BOMBER_THRUST);
    return;
  }

  // Target acquisition. Standard bombers prefer friendlies (strafe-run
  // pattern); infected bombers target the nearest un-flipped enemy.
  // hasTarget gates firing below for infected bombers — same as
  // Fighter, an infected ship with no live enemy shouldn't waste
  // shots at the player.
  Vector3 targetPos;
  bool hasTarget = !isInfected;
  if (isInfected) {
    const Entity *prey = nearestEnemyTo(e.pos, e.id, false);
    if (prey) {
      targetPos = prey->pos;
      hasTarget = true;
    } else {
      targetPos = player.position();
    }
  } else {
    Vector3 friendlyPos;
    uint32_t friendlyId = nearestFriendly(e.pos, friendlyPos);
    Vector3 playerPos = player.position();
    float distPlayer = Vector3Distance(playerPos, e.pos);

    targetPos = playerPos;
    bool targetingFriendly = false;
    if (friendlyId != 0) {
      float distFriendly = Vector3Distance(friendlyPos, e.pos);
      if (distFriendly <
          distPlayer * Config::BOMBER_FRIENDLY_PRIORITY) {
        targetPos = friendlyPos;
        targetingFriendly = true;
        e.aiState = AIState::StrafeFriendly;
      }
    }
    (void)targetingFriendly; // reserved for future strafe-specific tuning
  }
  float infectMul = isInfected ? e.aiSpeedPenaltyAfterInfect : 1.0f;

  Vector3 toPlayer = Vector3Subtract(targetPos, e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // EVADE entry/exit — same fraction threshold as Fighter so all
  // shielded fliers behave consistently when wounded. Skipped for
  // infected bombers (their AIState is locked to Infected — they
  // commit to the new fight per design §5.3).
  bool wantEvade = !isInfected && (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < e.aiEvadeAtHPFrac);
  if (wantEvade && dist < e.aiDetectionRange * 1.5f) {
    e.aiState = AIState::Evade;
  } else if (e.aiState == AIState::Evade && !wantEvade) {
    e.aiState = AIState::Pursue;
  }

  float evadeScale = 1.0f;
  if (e.aiState == AIState::Evade && e.hullMax > 0.0f) {
    float hpFrac = e.hullHP / e.hullMax;
    float bandT = 1.0f - (hpFrac / e.aiEvadeAtHPFrac);
    if (bandT < 0.0f) bandT = 0.0f;
    if (bandT > 1.0f) bandT = 1.0f;
    evadeScale = 0.60f - 0.35f * bandT; // 0.60 → 0.25, mirrors Fighter
  }

  // Aim — toward player normally, away in EVADE.
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toPlayerH.x, toPlayerH.z);
    if (e.aiState == AIState::Evade) {
      desiredYaw += 3.14159f;
    }
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = Config::BOMBER_TURN_RATE * evadeScale * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // Pursue / Attack transitions — infected bombers keep AIState::
  // Infected and use the willFire flag below for firing condition.
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  if (e.aiState != AIState::Evade && !isInfected) {
    if (dist < e.aiAttackRange) {
      e.aiState = AIState::Attack;
    } else if (dist < e.aiDetectionRange) {
      e.aiState = AIState::Pursue;
    }
  }
  bool willFire = (e.aiState == AIState::Attack) ||
                  (isInfected && hasTarget &&
                   dist < e.aiAttackRange);

  // Bombers ease the throttle in ATTACK so the slow projectiles get
  // a stable firing platform; full thrust during PURSUE to close.
  // Infection penalty applied multiplicatively on top of evadeScale.
  float thrustScale = willFire ? 0.35f : 1.0f;
  thrustScale *= evadeScale * infectMul;
  e.vel.x += fwd.x * Config::BOMBER_THRUST * thrustScale * dt;
  e.vel.z += fwd.z * Config::BOMBER_THRUST * thrustScale * dt;

  // Altitude hold (lower than Fighter — bomber lumbers near the deck).
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::BOMBER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.4f * dt;
  e.vel.y *= 1.0f - 1.0f * dt;

  // Speed cap (scaled by evadeScale and infection penalty).
  float spd = Vector3Length(e.vel);
  float maxSpeed = Config::BOMBER_MAX_SPEED * evadeScale * infectMul;
  if (spd > maxSpeed) {
    e.vel = Vector3Scale(e.vel, maxSpeed / spd);
  }

  // Drag.
  float drag = 0.4f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel.x *= 1.0f - drag;
  e.vel.z *= 1.0f - drag;

  // Integrate.
  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  // Don't sink.
  float floor = planet.heightAt(e.pos.x, e.pos.z) + 6.0f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }

  // Fire when nose aligned within the wider Bomber cone (~25°).
  // Infected bombers fire player-owned shots at their new target.
  if (willFire && e.fireTimer <= 0.0f) {
    Vector3 toTargetN =
        Vector3Normalize(Vector3Subtract(targetPos, e.pos));
    Vector3 fwd3 = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd3, toTargetN);
    if (dot > 0.90f) { // ~25° cone
      fireBomberShot(e, targetPos, isInfected);
      e.fireTimer = Config::BOMBER_FIRE_RATE;
    }
  }
}

void EntityManager::fireBomberShot(Entity &e, Vector3 targetPos,
                                   bool infected) {
  Vector3 toTarget = Vector3Subtract(targetPos, e.pos);
  float d = Vector3Length(toTarget);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toTarget, 1.0f / d);
  Vector3 vel = Vector3Scale(dir, Config::BOMBER_PROJ_SPEED);
  Vector3 spawn = Vector3Add(e.pos, Vector3Scale(dir, 2.5f));
  ProjectileOwner owner =
      infected ? ProjectileOwner::Player : ProjectileOwner::Enemy;
  spawnProjectile(spawn, vel, Config::BOMBER_FIRE_DAMAGE,
                  Config::BOMBER_PROJ_RANGE, Config::BOMBER_PROJ_SPEED,
                  owner);
}

// ====================================================================
// Drone — boids flocking + pursuit + kamikaze contact damage.
//
// Three boids rules accumulated against neighbouring drones:
//   Separation : push away from neighbours within DRONE_SEP_RADIUS
//   Alignment  : match average velocity of neighbours within ALIGN_RADIUS
//   Cohesion   : drift toward centroid of neighbours within COHESION_RADIUS
// Plus a constant pursuit force toward the player. Weights tuned so
// the swarm stays loosely clustered while converging on the player.
//
// O(N²) over the entity pool — fine while N stays small (16 drones
// = 256 ops). Spatial grid replacement comes when 5d.4 (Carrier) and
// later increase concurrent enemy counts.
// ====================================================================
// ====================================================================
// Drone v2 — light gunship swarm. Boids flocking (sep / align /
// cohesion) keeps inter-drone spacing; the player-relative force is
// a stand-off band + orbital drift instead of "ram the player".
//
//   * Stand-off:  radial push away if inside DRONE_STANDOFF_MIN,
//                 radial pull in if outside DRONE_STANDOFF_MAX,
//                 zero inside the band so orbital drift dominates.
//   * Orbital:    each drone holds a random bearing (unit XZ vector,
//                 stored in targetPos) and tries to slide to
//                 player.pos + bearing * standoffMid. stateTimer
//                 counts down; on zero a fresh bearing is picked
//                 (interval randomised per-drone 3..4s). This gives
//                 chaotic swirl rather than predictable orbits.
//   * Weapon:     fire when fireTimer ≤ 0 AND distance ≤ FIRE_RANGE.
//                 Weak per-shot damage — the threat is the count.
//   * No contact damage — drones never self-detonate on the player.
// ====================================================================
void EntityManager::updateDrone(Entity &e, float dt, const Planet &planet,
                                Player &player, ParticleSystem &particles) {
  (void)particles; // unused now — kamikaze burst retired

  // ---------- Boids (sep / align / cohesion) ----------
  Vector3 sepForce = {0, 0, 0};
  Vector3 alignAvgVel = {0, 0, 0};
  Vector3 cohesionAvgPos = {0, 0, 0};
  int alignCount = 0;
  int cohesionCount = 0;

  for (const Entity &other : m_entities) {
    if (!other.alive) continue;
    if (other.type != EntityType::Drone) continue;
    if (&other == &e) continue;
    Vector3 d = Vector3Subtract(e.pos, other.pos);
    float dist = Vector3Length(d);
    if (dist < 0.01f) continue;

    if (dist < Config::DRONE_SEP_RADIUS) {
      Vector3 push = Vector3Scale(d, 1.0f / (dist * dist));
      sepForce = Vector3Add(sepForce, push);
    }
    if (dist < Config::DRONE_ALIGN_RADIUS) {
      alignAvgVel = Vector3Add(alignAvgVel, other.vel);
      ++alignCount;
    }
    if (dist < Config::DRONE_COHESION_RADIUS) {
      cohesionAvgPos = Vector3Add(cohesionAvgPos, other.pos);
      ++cohesionCount;
    }
  }

  Vector3 alignForce = {0, 0, 0};
  if (alignCount > 0) {
    alignAvgVel = Vector3Scale(alignAvgVel, 1.0f / alignCount);
    alignForce = Vector3Subtract(alignAvgVel, e.vel);
  }
  Vector3 cohesionForce = {0, 0, 0};
  if (cohesionCount > 0) {
    cohesionAvgPos = Vector3Scale(cohesionAvgPos, 1.0f / cohesionCount);
    cohesionForce = Vector3Subtract(cohesionAvgPos, e.pos);
    float clen = Vector3Length(cohesionForce);
    if (clen > 0.01f)
      cohesionForce = Vector3Scale(cohesionForce, 1.0f / clen);
  }

  // ---------- Stand-off + orbital drift ----------
  Vector3 toPlayerH = {player.position().x - e.pos.x, 0.0f,
                       player.position().z - e.pos.z};
  float distH = sqrtf(toPlayerH.x * toPlayerH.x + toPlayerH.z * toPlayerH.z);

  // Radial: 0 inside the band, +radial (toward player) if too far,
  // -radial (away) if too close.
  Vector3 standoffForce = {0, 0, 0};
  if (distH > 0.01f) {
    Vector3 radial = Vector3Scale(toPlayerH, 1.0f / distH);
    if (distH < Config::DRONE_STANDOFF_MIN) {
      standoffForce = Vector3Scale(radial, -1.0f);
    } else if (distH > Config::DRONE_STANDOFF_MAX) {
      standoffForce = radial;
    }
  }

  // Bearing retarget. stateTimer < 0 forces an immediate pick on the
  // first tick after spawn (allocEnemy() sets stateTimer = 0). On each
  // retarget, a fresh unit bearing is sampled from a hash of (id, time)
  // — deterministic per drone but uncorrelated across the swarm.
  e.stateTimer -= dt;
  if (e.stateTimer <= 0.0f) {
    uint32_t h = e.id * 1103515245u +
                 static_cast<uint32_t>(player.position().x * 17.0f) +
                 static_cast<uint32_t>(player.position().z * 31.0f);
    float angle = static_cast<float>(h % 6283) / 1000.0f; // 0..2π
    e.targetPos = {sinf(angle), 0.0f, cosf(angle)};
    float u = static_cast<float>((h >> 13) & 0xFF) / 255.0f;
    e.stateTimer = Config::DRONE_ORBIT_RETARGET_MIN +
                   u * (Config::DRONE_ORBIT_RETARGET_MAX -
                        Config::DRONE_ORBIT_RETARGET_MIN);
  }

  // Orbit target = player + bearing × midpoint of the band. Force is
  // the unit vector from drone → orbit target on the XZ plane.
  float standoffMid = 0.5f * (Config::DRONE_STANDOFF_MIN +
                              Config::DRONE_STANDOFF_MAX);
  Vector3 orbitTarget = {player.position().x + e.targetPos.x * standoffMid,
                         e.pos.y,
                         player.position().z + e.targetPos.z * standoffMid};
  Vector3 orbitForce = {orbitTarget.x - e.pos.x, 0.0f,
                        orbitTarget.z - e.pos.z};
  float ol = sqrtf(orbitForce.x * orbitForce.x +
                   orbitForce.z * orbitForce.z);
  if (ol > 0.001f) {
    orbitForce.x /= ol;
    orbitForce.z /= ol;
  }

  // ---------- Sum forces ----------
  Vector3 total = {0, 0, 0};
  total = Vector3Add(total, Vector3Scale(sepForce, Config::DRONE_SEP_WEIGHT));
  total = Vector3Add(total,
                     Vector3Scale(alignForce, Config::DRONE_ALIGN_WEIGHT));
  total = Vector3Add(total, Vector3Scale(cohesionForce,
                                          Config::DRONE_COHESION_WEIGHT));
  total = Vector3Add(total, Vector3Scale(standoffForce,
                                          Config::DRONE_STANDOFF_WEIGHT));
  total = Vector3Add(total,
                     Vector3Scale(orbitForce, Config::DRONE_ORBIT_WEIGHT));

  e.vel = Vector3Add(e.vel, Vector3Scale(total, Config::DRONE_THRUST * dt));

  // Speed cap
  float spd = Vector3Length(e.vel);
  if (spd > Config::DRONE_MAX_SPEED)
    e.vel = Vector3Scale(e.vel, Config::DRONE_MAX_SPEED / spd);

  // Light drag so drones don't oscillate forever after a force pulse.
  float drag = 0.4f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel = Vector3Scale(e.vel, 1.0f - drag);

  // Update yaw to match horizontal velocity (visual + radar triangle).
  if (fabsf(e.vel.x) + fabsf(e.vel.z) > 0.1f)
    e.yaw = atan2f(e.vel.x, e.vel.z);

  // Integrate
  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  // Keep above terrain by a small margin
  float floor = planet.heightAt(e.pos.x, e.pos.z) + 3.0f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }

  // ---------- Weapon fire ----------
  // fireTimer is auto-decremented at the top of update(). Fire when
  // ready AND inside the engage range. Aim straight at the player —
  // the slow projectile speed gives the player a fair dodge window.
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  float pdist = Vector3Length(toPlayer);
  if (e.fireTimer <= 0.0f && pdist <= Config::DRONE_FIRE_RANGE) {
    fireDroneShot(e, player.position());
    e.fireTimer = Config::DRONE_FIRE_RATE;
  }
}

void EntityManager::fireDroneShot(Entity &e, Vector3 targetPos) {
  Vector3 toTarget = Vector3Subtract(targetPos, e.pos);
  float d = Vector3Length(toTarget);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toTarget, 1.0f / d);
  Vector3 vel = Vector3Scale(dir, Config::DRONE_PROJ_SPEED);
  Vector3 spawn = Vector3Add(e.pos, Vector3Scale(dir, 1.0f));
  spawnProjectile(spawn, vel, Config::DRONE_FIRE_DAMAGE,
                  Config::DRONE_PROJ_RANGE, Config::DRONE_PROJ_SPEED,
                  ProjectileOwner::Enemy);
}

int EntityManager::liveDroneCount() const {
  int n = 0;
  for (const Entity &e : m_entities) {
    if (e.alive && e.type == EntityType::Drone) ++n;
  }
  return n;
}

// ====================================================================
// Seeder — high-altitude drone dispenser. Drifts in a loose orbit
// around the player and drops a fresh drone every SEEDER_DEPLOY_INTERVAL
// seconds (subject to a SEEDER_DEPLOY_RANGE gate so a faraway seeder
// doesn't silently flood the map). Peels away if the player closes to
// SEEDER_RETREAT_RANGE — fragile, doesn't want a knife-fight.
//
// fireTimer is re-purposed as the deploy cooldown for this entity
// type. AI state stays Idle — seeders have no PURSUE/ATTACK/EVADE
// transitions; their behaviour is driven by player range alone.
// ====================================================================
void EntityManager::updateSeeder(Entity &e, float dt, const Planet &planet,
                                 const Player &player) {
  // Slice A — retreat. Seeders are fragile (50 HP) so the band
  // between aiRetreatHPFrac and zero is narrow, but a wounded seeder
  // visibly peels off rather than continuing to drift in orbit.
  if (e.hullMax > 0.0f &&
      (e.hullHP / e.hullMax) < e.aiRetreatHPFrac) {
    e.aiState = AIState::Retreating;
  }
  if (e.aiState == AIState::Retreating) {
    tickRetreat(e, dt, planet, Config::SEEDER_MAX_SPEED, 1.5f,
                Config::SEEDER_THRUST);
    return;
  }

  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);

  // Desired horizontal heading: orbit at SEEDER_DRIFT_RADIUS. If too
  // far, head in. If too close, peel away. Otherwise circle by
  // adding a 90° tangent to the toward-player vector.
  Vector3 desiredDir = {0, 0, 0};
  if (distH > 0.001f) {
    Vector3 toN = Vector3Scale(toPlayerH, 1.0f / distH);
    if (distH < Config::SEEDER_RETREAT_RANGE) {
      desiredDir = Vector3Scale(toN, -1.0f); // peel away
    } else if (distH > Config::SEEDER_DRIFT_RADIUS * 1.2f) {
      desiredDir = toN; // close in
    } else {
      // Tangent — rotate toN by 90° in the XZ plane for orbital drift.
      desiredDir = {-toN.z, 0.0f, toN.x};
    }
  }

  // Apply thrust along desired direction.
  e.vel.x += desiredDir.x * Config::SEEDER_THRUST * dt;
  e.vel.z += desiredDir.z * Config::SEEDER_THRUST * dt;

  // Yaw follows velocity for visual alignment.
  if (fabsf(e.vel.x) + fabsf(e.vel.z) > 0.05f) {
    e.yaw = atan2f(e.vel.x, e.vel.z);
  }

  // Altitude hold — sit high.
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::SEEDER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.3f * dt;
  e.vel.y *= 1.0f - 1.0f * dt;

  // Speed cap (horizontal only — vertical is governed by altitude hold).
  float spdH = sqrtf(e.vel.x * e.vel.x + e.vel.z * e.vel.z);
  if (spdH > Config::SEEDER_MAX_SPEED) {
    float k = Config::SEEDER_MAX_SPEED / spdH;
    e.vel.x *= k;
    e.vel.z *= k;
  }

  // Light drag (so peel-away doesn't accelerate forever).
  float drag = 0.6f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel.x *= 1.0f - drag;
  e.vel.z *= 1.0f - drag;

  // Integrate.
  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  // Don't sink (shouldn't happen at this altitude, but defensively).
  float floor = planet.heightAt(e.pos.x, e.pos.z) +
                Config::SEEDER_PREFERRED_ALT * 0.5f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }

  // Drone drop — fireTimer is the deploy cooldown for seeders.
  // Already decremented at the top of update() so we just check it.
  // Gate on player range so distant seeders don't flood the global pool,
  // AND on the global drone cap — when full, we DO NOT consume the
  // cooldown, so the next drone-death immediately frees the slot for a
  // fresh drop (no surprise stack-up the moment a kill happens).
  float dist = Vector3Length(toPlayer);
  if (e.fireTimer <= 0.0f && dist < Config::SEEDER_DEPLOY_RANGE &&
      liveDroneCount() < Config::DRONE_GLOBAL_CAP) {
    Vector3 dropPos = {e.pos.x, e.pos.y - 4.0f, e.pos.z};
    spawnEnemy(EntityType::Drone, dropPos);
    e.fireTimer = Config::SEEDER_DEPLOY_INTERVAL;
  }
}

// ====================================================================
// Carrier — boss. Drifts in a slow high-altitude orbit around the
// player and steadily deploys drones. Movement pattern mirrors
// Seeder so the player has a familiar visual cue (large slow
// hover-and-drift = drone factory) but Carrier moves much slower
// and hovers higher. Threat is the steady drip + the 4-sector
// shield forcing the player to flank rather than dump cannon into
// one face. No direct weapons — the menace is opportunity cost.
//
// fireTimer is re-purposed as the deploy cooldown (same as Seeder).
// ====================================================================
void EntityManager::updateCarrier(Entity &e, float dt, const Planet &planet,
                                  const Player &player) {
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);

  // Orbit / approach / retreat — same three-zone heading logic as
  // Seeder but at Carrier-scale distances.
  Vector3 desiredDir = {0, 0, 0};
  if (distH > 0.001f) {
    Vector3 toN = Vector3Scale(toPlayerH, 1.0f / distH);
    if (distH < Config::CARRIER_RETREAT_RANGE) {
      desiredDir = Vector3Scale(toN, -1.0f);
    } else if (distH > Config::CARRIER_DRIFT_RADIUS * 1.2f) {
      desiredDir = toN;
    } else {
      desiredDir = {-toN.z, 0.0f, toN.x};
    }
  }

  e.vel.x += desiredDir.x * Config::CARRIER_THRUST * dt;
  e.vel.z += desiredDir.z * Config::CARRIER_THRUST * dt;

  // Face the direction of travel so sector orientation (front =
  // direction of motion) reads naturally to the player.
  if (fabsf(e.vel.x) + fabsf(e.vel.z) > 0.05f) {
    e.yaw = atan2f(e.vel.x, e.vel.z);
  }

  // Altitude hold — sits very high.
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::CARRIER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.25f * dt;
  e.vel.y *= 1.0f - 1.0f * dt;

  // Speed cap (horizontal).
  float spdH = sqrtf(e.vel.x * e.vel.x + e.vel.z * e.vel.z);
  if (spdH > Config::CARRIER_MAX_SPEED) {
    float k = Config::CARRIER_MAX_SPEED / spdH;
    e.vel.x *= k;
    e.vel.z *= k;
  }

  // Heavy drag — keeps the carrier feeling ponderous.
  float drag = 0.7f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel.x *= 1.0f - drag;
  e.vel.z *= 1.0f - drag;

  e.pos = Vector3Add(e.pos, Vector3Scale(e.vel, dt));

  float floor = planet.heightAt(e.pos.x, e.pos.z) +
                Config::CARRIER_PREFERRED_ALT * 0.5f;
  if (e.pos.y < floor) {
    e.pos.y = floor;
    if (e.vel.y < 0.0f) e.vel.y = 0.0f;
  }

  // Drone drop — same cap-aware gate as Seeder. Cooldown is NOT
  // consumed when the cap is full so the moment a drone dies the next
  // drop fires immediately.
  float dist = Vector3Length(toPlayer);
  if (e.fireTimer <= 0.0f && dist < Config::CARRIER_DEPLOY_RANGE &&
      liveDroneCount() < Config::DRONE_GLOBAL_CAP) {
    Vector3 dropPos = {e.pos.x, e.pos.y - 6.0f, e.pos.z};
    spawnEnemy(EntityType::Drone, dropPos);
    e.fireTimer = Config::CARRIER_DEPLOY_INTERVAL;
  }
}

// ====================================================================
// Ground Tank — was a stationary turret in 5d.2; now a tracked vehicle
// that drives toward the player while healthy, fires when in range
// and roughly aligned, and reverses course at low HP. Same chassis
// + barrel visual as the original turret; only behaviour changed.
//
// State machine:
//   Pursue → close the distance, fire en route
//   Attack → same as Pursue but eased throttle when in firing range
//   Evade  → hull below AI_EVADE_HEALTH (25%); drive AWAY from player,
//            do not fire. Hysteresis: returns to Pursue only after
//            hull recovers past TANK_EVADE_RECOVERY (50%) — tanks
//            can't self-repair so this effectively means "evade for
//            the rest of the round once committed".
//
// yaw is now the chassis/barrel angle in one (no separate turret
// cap rotation — the whole chassis aims forward). Render path
// already uses e.yaw for the barrel direction so the visual matches.
// ====================================================================
void EntityManager::updateGroundTurret(Entity &e, float dt,
                                       const Planet &planet,
                                       const Player &player) {
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // EVADE entry/exit with hysteresis. Once committed, stay committed
  // until hull crawls back above TANK_EVADE_RECOVERY (won't happen
  // without a heal — by design, badly damaged tanks try to retreat).
  bool wantEvade = (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < e.aiEvadeAtHPFrac);
  if (wantEvade) {
    e.aiState = AIState::Evade;
  } else if (e.aiState == AIState::Evade &&
             (e.hullHP / e.hullMax) > Config::TANK_EVADE_RECOVERY) {
    e.aiState = AIState::Pursue;
  }

  // Chassis yaw — toward player in Pursue/Attack, AWAY in Evade.
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toPlayerH.x, toPlayerH.z);
    if (e.aiState == AIState::Evade) desiredYaw += 3.14159f;
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = Config::TANK_TURN_RATE * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // Drive forward along chassis direction.
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  e.pos.x += fwd.x * Config::TANK_DRIVE_SPEED * dt;
  e.pos.z += fwd.z * Config::TANK_DRIVE_SPEED * dt;
  // Resnap to terrain.
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  e.pos.y = ground + Config::TURRET_MOUNT_HEIGHT;

  // Fire — only outside EVADE, only when player in range AND chassis
  // roughly aligned. Wider cone than fighters (~25°) because the
  // whole chassis turns: the cannon's aim is the body's heading.
  if (e.aiState != AIState::Evade &&
      dist < Config::TURRET_FIRE_RANGE && e.fireTimer <= 0.0f) {
    Vector3 toPlayerN = Vector3Normalize(toPlayer);
    float dot = Vector3DotProduct(fwd, toPlayerN);
    if (dot > (1.0f - Config::TANK_FIRE_CONE)) {
      fireTurretShot(e, player);
      e.fireTimer = Config::TURRET_ENEMY_FIRE_RATE;
    }
  }
}

// ====================================================================
// Collector — ground vehicle running an out-and-back delivery loop.
//
// State machine (stored in aiState):
//   CollectorOutbound  — heading toward a random pickup site
//   CollectorPickup    — dwelling at the site, loading cargo
//   CollectorInbound   — heading back to the home Base
//   CollectorUnload    — dwelling at the base, unloading + scoring
//
// targetPos holds the current destination. seekTargetId holds the
// home Base entity id; resolved each tick so a re-spawned base
// after a kill picks up cleanly. stateTimer is the dwell countdown
// at each end. hasCargo flips on pickup-done and off on unload-done;
// rendering uses it to colour the cabin pip (yellow=empty / green=full).
//
// If no Base is alive, the collector idles in place — there's
// nowhere to deliver to. Once a Base spawns / respawns, the loop
// resumes from the start.
// ====================================================================
void EntityManager::updateCollector(Entity &e, float dt, const Planet &planet) {
  // Find / refresh the home Base. If our seekTargetId is stale, look
  // up the nearest live Base and adopt it.
  Vector3 basePos = {0, 0, 0};
  const Entity *base = nullptr;
  if (e.seekTargetId != 0) {
    for (const Entity &b : m_entities) {
      if (b.alive && b.id == e.seekTargetId &&
          b.type == EntityType::Base) {
        base = &b;
        basePos = b.pos;
        break;
      }
    }
  }
  if (!base) {
    uint32_t newId = nearestBase(e.pos, basePos);
    if (newId == 0) {
      // No base alive — collector idles in place.
      e.vel = {0, 0, 0};
      e.pos.y = planet.heightAt(e.pos.x, e.pos.z) + 0.8f;
      return;
    }
    e.seekTargetId = newId;
    // First-time setup: pick a pickup site and head out.
    e.aiState = AIState::CollectorOutbound;
    e.hasCargo = false;
  }

  // Pick a fresh pickup site whenever we transition into Outbound
  // without a valid target — i.e. round start, or after returning.
  auto pickPickupSite = [&]() {
    uint32_t s = (e.id * 2654435761u) ^
                 static_cast<uint32_t>(m_nextId + (int)(e.pos.x * 7.0f));
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    float a = (static_cast<float>(s & 0xFFFF) / 65535.0f) * 6.28318f;
    s = s * 1103515245u + 12345u;
    float t = static_cast<float>(s & 0xFFFF) / 65535.0f;
    float r = Config::COLLECTOR_PICKUP_MIN_DIST +
              t * (Config::COLLECTOR_PICKUP_MAX_DIST -
                   Config::COLLECTOR_PICKUP_MIN_DIST);
    float x = basePos.x + sinf(a) * r;
    float z = basePos.z + cosf(a) * r;
    e.targetPos = {x, planet.heightAt(x, z) + 0.8f, z};
  };

  switch (e.aiState) {
  case AIState::CollectorOutbound: {
    // Pick a pickup site if we don't have one yet (first cycle).
    Vector3 toTgt = Vector3Subtract(e.targetPos, e.pos);
    float distXZ = sqrtf(toTgt.x * toTgt.x + toTgt.z * toTgt.z);
    if (distXZ < 0.01f || (e.targetPos.x == 0.0f && e.targetPos.z == 0.0f)) {
      pickPickupSite();
      toTgt = Vector3Subtract(e.targetPos, e.pos);
      distXZ = sqrtf(toTgt.x * toTgt.x + toTgt.z * toTgt.z);
    }
    if (distXZ <= Config::COLLECTOR_WAYPOINT_RADIUS) {
      e.aiState = AIState::CollectorPickup;
      e.stateTimer = Config::COLLECTOR_DWELL_TIME;
      e.vel = {0, 0, 0};
    } else {
      e.yaw = atan2f(toTgt.x, toTgt.z);
      float spd = Config::COLLECTOR_SPEED;
      e.vel.x = sinf(e.yaw) * spd;
      e.vel.z = cosf(e.yaw) * spd;
      e.pos.x += e.vel.x * dt;
      e.pos.z += e.vel.z * dt;
    }
    break;
  }
  case AIState::CollectorPickup: {
    e.stateTimer -= dt;
    if (e.stateTimer <= 0.0f) {
      e.hasCargo = true;
      e.targetPos = basePos;
      e.aiState = AIState::CollectorInbound;
    }
    break;
  }
  case AIState::CollectorInbound: {
    Vector3 toTgt = Vector3Subtract(basePos, e.pos);
    float distXZ = sqrtf(toTgt.x * toTgt.x + toTgt.z * toTgt.z);
    if (distXZ <= Config::COLLECTOR_WAYPOINT_RADIUS) {
      e.aiState = AIState::CollectorUnload;
      e.stateTimer = Config::COLLECTOR_DWELL_TIME;
      e.vel = {0, 0, 0};
    } else {
      e.yaw = atan2f(toTgt.x, toTgt.z);
      float spd = Config::COLLECTOR_SPEED;
      e.vel.x = sinf(e.yaw) * spd;
      e.vel.z = cosf(e.yaw) * spd;
      e.pos.x += e.vel.x * dt;
      e.pos.z += e.vel.z * dt;
    }
    break;
  }
  case AIState::CollectorUnload: {
    e.stateTimer -= dt;
    if (e.stateTimer <= 0.0f) {
      // Delivery complete — count it, drop cargo, head out again.
      ++m_deliveryCount;
      e.hasCargo = false;
      pickPickupSite();
      e.aiState = AIState::CollectorOutbound;
    }
    break;
  }
  default:
    // Unexpected state on a fresh collector — bootstrap into Outbound.
    e.aiState = AIState::CollectorOutbound;
    e.hasCargo = false;
    break;
  }

  // Resnap to terrain — collector hugs the ground regardless of state.
  e.pos.y = planet.heightAt(e.pos.x, e.pos.z) + 0.8f;
}

// ====================================================================
// Base — stationary delivery destination + defensive auto-turret.
//
// Each tick: scan the entity pool for the nearest enemy in
// BASE_TURRET_RANGE. If found, rotate yaw toward it at
// BASE_TURRET_AIM_RATE and fire whenever the barrel is within the
// fire cone and the cooldown is ready. If no target is in range,
// idle-rotate slowly so the turret still reads as "active" at
// distance.
//
// fireTimer is the turret cooldown. Decremented at the top of the
// main update loop so we don't need to tick it here. yaw stores the
// turret's barrel angle (same convention as the Ground Tank now).
// ====================================================================
void EntityManager::updateBase(Entity &e, float dt) {
  // Find nearest enemy in range via the shared auto-aim predicate.
  // Skips friendlies, projectiles, AND infected / infecting entities
  // (which are the player's allies after the flip).
  uint32_t targetId = 0;
  Vector3 targetPos{};
  float bestDist = Config::BASE_TURRET_RANGE;
  for (const Entity &other : m_entities) {
    if (!isAutoAimTarget(other)) continue;
    float d = Vector3Distance(other.pos, e.pos);
    if (d < bestDist) {
      bestDist = d;
      targetId = other.id;
      targetPos = other.pos;
    }
  }

  if (targetId == 0) {
    // No target — idle rotation so the turret reads as alive.
    e.yaw += 0.6f * dt;
    if (e.yaw > 6.28318f) e.yaw -= 6.28318f;
    return;
  }

  // Aim turret yaw at the target. Barrel rises from
  // BASE_TURRET_BARREL_HEIGHT above the base, but for yaw resolution
  // we use the horizontal vector to the target.
  Vector3 toTarget = Vector3Subtract(targetPos, e.pos);
  float distH = sqrtf(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toTarget.x, toTarget.z);
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = Config::BASE_TURRET_AIM_RATE * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // Fire — in range + nose-aligned within fire cone + cooldown ready.
  if (e.fireTimer <= 0.0f) {
    Vector3 toTargetN = Vector3Normalize(toTarget);
    Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd, toTargetN);
    if (dot > (1.0f - Config::BASE_TURRET_FIRE_CONE)) {
      // Muzzle sits on top of the control tower. Spawn shot slightly
      // ahead of the muzzle along the firing direction.
      Vector3 muzzle = {e.pos.x + 2.5f, e.pos.y +
                                            Config::BASE_TURRET_BARREL_HEIGHT,
                        e.pos.z + 2.5f};
      Vector3 dir3 = Vector3Subtract(targetPos, muzzle);
      float d3 = Vector3Length(dir3);
      if (d3 > 0.001f) {
        dir3 = Vector3Scale(dir3, 1.0f / d3);
        Vector3 spawn = Vector3Add(muzzle, Vector3Scale(dir3, 1.5f));
        Vector3 vel = Vector3Scale(dir3, Config::CANNON_SPEED);
        // Player-owned so the projectile→entity collision path picks
        // up enemies (and the friendly-fire filter below skips
        // friendlies).
        spawnProjectile(spawn, vel, Config::BASE_TURRET_DAMAGE,
                        Config::BASE_TURRET_RANGE, Config::CANNON_SPEED,
                        ProjectileOwner::Player);
        e.fireTimer = Config::BASE_TURRET_FIRE_RATE;
      }
    }
  }
}

// ====================================================================
// Repair Station — stationary installation. When the player is within
// REPAIR_STATION_HEAL_RADIUS, ticks hull HP back up at HEAL_RATE.
// Player handles its own cap inside Player::heal(). Y stays anchored
// to terrain in case heightmap shifts (defensive).
// ====================================================================
void EntityManager::updateRepairStation(Entity &e, float dt, Player &player) {
  // (Planet ref not available here — y was set at spawn time.)
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  float d = Vector3Length(toPlayer);
  if (d <= Config::REPAIR_STATION_HEAL_RADIUS) {
    player.heal(Config::REPAIR_STATION_HEAL_RATE * dt);
  }
}

// ====================================================================
// Radar Booster — purely passive. While alive, Radar reads
// anyRadarBoosterAlive() and switches its display range from
// RADAR_BASE_RANGE to RADAR_BOOST_RANGE. The only per-tick work here
// is animating the rotating dish visual (yaw).
// ====================================================================
void EntityManager::updateRadarBooster(Entity &e, float dt) {
  e.yaw += 1.5f * dt;
  if (e.yaw > 6.28318f) e.yaw -= 6.28318f;
}

bool EntityManager::anyRadarBoosterAlive() const {
  for (const Entity &e : m_entities) {
    if (e.alive && e.type == EntityType::RadarBooster) return true;
  }
  return false;
}

int EntityManager::liveFriendlyCount() const {
  int n = 0;
  for (const Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base) {
      ++n;
    }
  }
  return n;
}

uint32_t EntityManager::nearestFriendly(Vector3 origin,
                                        Vector3 &outPos) const {
  uint32_t bestId = 0;
  float bestDist = 1e9f;
  for (const Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type != EntityType::Collector &&
        e.type != EntityType::RepairStation &&
        e.type != EntityType::RadarBooster &&
        e.type != EntityType::Base)
      continue;
    float d = Vector3Distance(e.pos, origin);
    if (d < bestDist) {
      bestDist = d;
      bestId = e.id;
      outPos = e.pos;
    }
  }
  return bestId;
}

uint32_t EntityManager::nearestBase(Vector3 origin, Vector3 &outPos) const {
  uint32_t bestId = 0;
  float bestDist = 1e9f;
  for (const Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type != EntityType::Base) continue;
    float d = Vector3Distance(e.pos, origin);
    if (d < bestDist) {
      bestDist = d;
      bestId = e.id;
      outPos = e.pos;
    }
  }
  return bestId;
}

void EntityManager::fireTurretShot(Entity &e, const Player &player) {
  Vector3 muzzle = {e.pos.x, e.pos.y + Config::TURRET_BARREL_HEIGHT, e.pos.z};
  Vector3 toPlayer = Vector3Subtract(player.position(), muzzle);
  float d = Vector3Length(toPlayer);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toPlayer, 1.0f / d);
  // Spawn just past the barrel tip along the firing direction so the
  // tracer doesn't appear to come out of the chassis floor.
  Vector3 spawn = Vector3Add(muzzle, Vector3Scale(dir, 1.8f));
  Vector3 vel = Vector3Scale(dir, Config::TURRET_PROJ_SPEED);
  spawnProjectile(spawn, vel, Config::TURRET_ENEMY_DAMAGE,
                  Config::TURRET_PROJ_RANGE, Config::TURRET_PROJ_SPEED,
                  ProjectileOwner::Enemy);
}

// ====================================================================
// Projectile update + collision detection
// O(P*E) for player projectiles; for v1 (P~50, E~16) this is ~800
// distance checks per tick — trivial. Spatial grid wires in for 5d
// when enemy counts climb.
// ====================================================================
void EntityManager::updateProjectile(Entity &p, float dt, const Planet &planet,
                                     Player &player,
                                     ParticleSystem &particles) {
  p.lifeRemaining -= dt;
  if (p.lifeRemaining <= 0.0f) {
    p.alive = false;
    --m_liveProjectiles;
    return;
  }

  // Depth Charge — no propulsion, just gravity. Drops out of the
  // ship and falls onto whatever is below. Detonates with heavy
  // splash on terrain hit. Mid-air enemy collision also detonates.
  if (p.kind == ProjectileKind::DepthCharge) {
    // World gravity (NEWTON_GRAVITY for consistency with player physics).
    p.vel.y -= Config::NEWTON_GRAVITY * dt;
  }

  // Missile guidance — proportional navigation. Applies to both
  // Missile and ClusterParent (the parent carrier flies like a
  // missile). When the lock target is gone (killed / stale id), the
  // missile reacquires the nearest live enemy within
  // MISSILE_REACQUIRE_RANGE. If nothing's in range, it goes ballistic
  // — no steering input, just a small downward acceleration so it
  // eventually hits the ground and detonates.
  //
  // ClusterParent additionally checks for "near target" each tick and
  // splits into 4 sub-missiles when within CLUSTER_SPLIT_DISTANCE.
  // The carrier deals no damage itself; the children carry the load.
  bool isGuided = (p.kind == ProjectileKind::Missile ||
                   p.kind == ProjectileKind::ClusterParent ||
                   p.kind == ProjectileKind::ShieldMissile ||
                   p.kind == ProjectileKind::InfectiousMissile) &&
                  p.turnRate > 0.0f;
  if (isGuided) {
    // Resolve current target by id. If the locked target got infected
    // mid-flight, treat it as gone so the missile drops the lock and
    // tries to reacquire on a still-valid enemy below.
    const Entity *target = nullptr;
    if (p.seekTargetId != 0) {
      for (const Entity &e : m_entities) {
        if (e.id == p.seekTargetId && isAutoAimTarget(e)) {
          target = &e;
          break;
        }
      }
    }
    // Reacquire if the prior target is gone (or never had one).
    // Same auto-aim filter as the initial lock — infected ships are
    // skipped here too.
    if (!target) {
      uint32_t newId = 0;
      float bestDist = Config::MISSILE_REACQUIRE_RANGE;
      for (const Entity &e : m_entities) {
        if (!isAutoAimTarget(e)) continue;
        float d = Vector3Distance(e.pos, p.pos);
        if (d < bestDist) {
          bestDist = d;
          newId = e.id;
          target = &e;
        }
      }
      p.seekTargetId = newId; // 0 if still none
    }

    if (target) {
      Vector3 toTarget = Vector3Subtract(target->pos, p.pos);
      float dist = Vector3Length(toTarget);

      // ClusterParent split check — split if either (a) we're inside
      // the nominal split radius, OR (b) we've passed our closest
      // approach to the target and are still inside an extended split
      // window. Without (b) the parent would loop back the way normal
      // missiles used to before the proximity fuse — circling the
      // target instead of releasing its payload. Splitting on (b)
      // hands the engagement to the more-agile children.
      bool shouldSplit = false;
      if (p.kind == ProjectileKind::ClusterParent) {
        if (dist < Config::CLUSTER_SPLIT_DISTANCE) {
          shouldSplit = true;
        } else if (dist < Config::CLUSTER_SPLIT_DISTANCE * 1.5f &&
                   dist > 0.001f) {
          Vector3 toTargetN = Vector3Scale(toTarget, 1.0f / dist);
          float closing = Vector3DotProduct(p.vel, toTargetN);
          if (closing < 0.0f) shouldSplit = true;
        }
      }
      if (shouldSplit) {
        float speed = Vector3Length(p.vel);
        if (speed < 1.0f) speed = Config::MISSILE_SPEED;
        Vector3 fwd = Vector3Scale(p.vel, 1.0f / speed);
        // Build a stable perpendicular basis (right + up) using a
        // world-up reference. If fwd is near-vertical, swap to world-Z.
        Vector3 worldUp = {0.0f, 1.0f, 0.0f};
        if (fabsf(fwd.y) > 0.95f) worldUp = {0.0f, 0.0f, 1.0f};
        Vector3 rightV = {
            fwd.y * worldUp.z - fwd.z * worldUp.y,
            fwd.z * worldUp.x - fwd.x * worldUp.z,
            fwd.x * worldUp.y - fwd.y * worldUp.x};
        float rl = Vector3Length(rightV);
        if (rl > 0.001f) rightV = Vector3Scale(rightV, 1.0f / rl);
        Vector3 upV = {
            rightV.y * fwd.z - rightV.z * fwd.y,
            rightV.z * fwd.x - rightV.x * fwd.z,
            rightV.x * fwd.y - rightV.y * fwd.x};

        float spreadTan =
            tanf(Config::CLUSTER_SPREAD * (3.14159f / 180.0f));
        // Four cardinal offsets in the perpendicular plane.
        const Vector3 offsets[4] = {
            {rightV.x * spreadTan, rightV.y * spreadTan,
             rightV.z * spreadTan},
            {-rightV.x * spreadTan, -rightV.y * spreadTan,
             -rightV.z * spreadTan},
            {upV.x * spreadTan, upV.y * spreadTan, upV.z * spreadTan},
            {-upV.x * spreadTan, -upV.y * spreadTan,
             -upV.z * spreadTan},
        };
        for (int i = 0; i < 4; ++i) {
          Vector3 dir = {fwd.x + offsets[i].x, fwd.y + offsets[i].y,
                         fwd.z + offsets[i].z};
          float dl = sqrtf(dir.x * dir.x + dir.y * dir.y +
                           dir.z * dir.z);
          if (dl > 0.001f) {
            dir.x /= dl;
            dir.y /= dl;
            dir.z /= dl;
          }
          Vector3 childPos = Vector3Add(p.pos, Vector3Scale(dir, 0.8f));
          Vector3 childVel = Vector3Scale(dir, speed);
          // Each child reacquires individually next tick (id=0). Damage:
          // each sub-missile carries CLUSTER_CHILD_DAMAGE_FRAC × MISSILE_DAMAGE,
          // so 4 children = exactly one missile's worth of total damage —
          // cluster's advantage is spread across targets, not raw output.
          // Turn rate is higher (CLUSTER_CHILD_TURN_RATE) so the children
          // are more agile than the parent and can reacquire targets the
          // parent would have whiffed.
          spawnProjectile(childPos, childVel,
                          Config::MISSILE_DAMAGE *
                              Config::CLUSTER_CHILD_DAMAGE_FRAC,
                          Config::MISSILE_RANGE, Config::MISSILE_SPEED,
                          ProjectileOwner::Player,
                          ProjectileKind::Missile, Config::PLASMA_SPLASH,
                          0 /* no lock — children reacquire next tick */,
                          Config::CLUSTER_CHILD_TURN_RATE);
        }
        // Carrier vanishes silently (no kill burst — it's a carrier,
        // not a warhead).
        p.alive = false;
        --m_liveProjectiles;
        return;
      }

      // Proximity fuse — Missile + ShieldMissile (ClusterParent has its
      // own split logic above). If the missile has passed its closest
      // approach to the locked target AND is inside fuse range,
      // detonate now instead of trying to circle back. The
      // direction-blend steering below can't snap-180° tight enough
      // when the miss-distance exceeds turn radius (speed / turnRate),
      // so near-misses without the fuse spiral around the target.
      //
      // Standard Missile uses splashRadius for the fuse range and
      // applies splash damage on detonation. Shield Missile has no
      // splash — its fuse range is target-radius based and it applies
      // its shield-priority damage to the locked target directly.
      if (p.kind == ProjectileKind::Missile && p.splashRadius > 0.0f &&
          dist > 0.001f) {
        float fuseRange =
            target->radius + p.splashRadius * Config::MISSILE_FUSE_FRAC;
        if (dist < fuseRange) {
          Vector3 toTargetN = Vector3Scale(toTarget, 1.0f / dist);
          float closing = Vector3DotProduct(p.vel, toTargetN);
          if (closing < 0.0f) {
            // Past closest approach inside splash range — detonate.
            emitHitBurst(p.pos, particles);
            if (p.owner == ProjectileOwner::Player) {
              applySplashDamage(p.pos, p.splashRadius, p.damage, particles);
              emitKillExplosion(p.pos, particles);
            }
            p.alive = false;
            --m_liveProjectiles;
            return;
          }
        }
      } else if (p.kind == ProjectileKind::ShieldMissile &&
                 dist > 0.001f) {
        // No splashRadius for Shield Missile — fuse range is just the
        // target's hit radius + a small buffer so the warhead always
        // lands on the locked target.
        float fuseRange = target->radius + 1.5f;
        if (dist < fuseRange) {
          Vector3 toTargetN = Vector3Scale(toTarget, 1.0f / dist);
          float closing = Vector3DotProduct(p.vel, toTargetN);
          if (closing < 0.0f) {
            // Past closest approach — detonate on the locked target.
            emitHitBurst(target->pos, particles);
            if (p.owner == ProjectileOwner::Player) {
              // Find the mutable target — target is a const* because
              // we found it in a const-iteration loop above.
              for (Entity &e : m_entities) {
                if (e.alive && e.id == p.seekTargetId) {
                  applyShieldHit(e,
                                 Config::SHIELD_MISSILE_SHIELD_DMG,
                                 Config::SHIELD_MISSILE_HULL_DMG,
                                 target->pos, particles);
                  break;
                }
              }
              emitKillExplosion(p.pos, particles);
            }
            p.alive = false;
            --m_liveProjectiles;
            return;
          }
        }
      } else if (p.kind == ProjectileKind::InfectiousMissile &&
                 dist > 0.001f) {
        // Same fuse pattern as Shield Missile — narrow range, just
        // enough to catch a near-miss. On detonation, call tryInfect
        // on the locked target. Success → infection sequence runs
        // (B.4 state machine). Failure (shields up / un-infectable) →
        // dud puff, no damage, no infection. The player learns to
        // strip shields first.
        float fuseRange = target->radius + 1.5f;
        if (dist < fuseRange) {
          Vector3 toTargetN = Vector3Scale(toTarget, 1.0f / dist);
          float closing = Vector3DotProduct(p.vel, toTargetN);
          if (closing < 0.0f) {
            if (p.owner == ProjectileOwner::Player) {
              bool flipped = false;
              for (Entity &e : m_entities) {
                if (e.alive && e.id == p.seekTargetId) {
                  flipped = tryInfect(e);
                  break;
                }
              }
              if (flipped) {
                // Strong visual on a successful flip — the moment
                // matters.
                emitKillExplosion(target->pos, particles);
              } else {
                // Dud — small puff so the player still sees the
                // missile expended.
                emitHitBurst(target->pos, particles);
              }
            }
            p.alive = false;
            --m_liveProjectiles;
            return;
          }
        }
      }

      // Standard proportional-nav steering toward target.
      if (dist > 0.001f) {
        Vector3 desiredDir = Vector3Scale(toTarget, 1.0f / dist);
        float speed = Vector3Length(p.vel);
        if (speed > 0.001f) {
          Vector3 currentDir = Vector3Scale(p.vel, 1.0f / speed);
          float cosA = Vector3DotProduct(currentDir, desiredDir);
          if (cosA < 1.0f) {
            float blend = p.turnRate * dt;
            if (blend > 1.0f) blend = 1.0f;
            Vector3 newDir = {
                currentDir.x + (desiredDir.x - currentDir.x) * blend,
                currentDir.y + (desiredDir.y - currentDir.y) * blend,
                currentDir.z + (desiredDir.z - currentDir.z) * blend};
            float nl = Vector3Length(newDir);
            if (nl > 0.001f) {
              p.vel = Vector3Scale(newDir, speed / nl);
            }
          }
        }
      }
    } else {
      // Ballistic — no target anywhere in range. Apply a small
      // downward acceleration so the missile dips toward the
      // terrain and detonates instead of flying out to its lifetime
      // limit and disappearing. The ClusterParent in this state
      // also splits NOW so the children at least scatter on the way
      // down (they'll go ballistic too, but spread the warhead).
      if (p.kind == ProjectileKind::ClusterParent) {
        float speed = Vector3Length(p.vel);
        if (speed < 1.0f) speed = Config::MISSILE_SPEED;
        Vector3 fwd = Vector3Scale(p.vel, 1.0f / speed);
        for (int i = 0; i < 4; ++i) {
          // 4-way nudge in the XZ plane; tiny pitch jitter.
          float a = (i * 1.5708f); // 90° per child
          Vector3 dir = {fwd.x * cosf(0.1f) + sinf(a) * 0.1f, fwd.y,
                         fwd.z * cosf(0.1f) + cosf(a) * 0.1f};
          float dl = sqrtf(dir.x * dir.x + dir.y * dir.y +
                           dir.z * dir.z);
          if (dl > 0.001f) {
            dir.x /= dl;
            dir.y /= dl;
            dir.z /= dl;
          }
          spawnProjectile(p.pos, Vector3Scale(dir, speed),
                          Config::MISSILE_DAMAGE *
                              Config::CLUSTER_CHILD_DAMAGE_FRAC,
                          Config::MISSILE_RANGE, Config::MISSILE_SPEED,
                          ProjectileOwner::Player,
                          ProjectileKind::Missile, Config::PLASMA_SPLASH,
                          0, Config::CLUSTER_CHILD_TURN_RATE);
        }
        p.alive = false;
        --m_liveProjectiles;
        return;
      }
      p.vel.y -= Config::MISSILE_BALLISTIC_DIP * dt;
    }

    // Smoke trail — every guided projectile leaves a trail so it
    // reads as a missile/carrier at chase distance.
    Vector3 trailVel = Vector3Scale(p.vel, -0.2f);
    trailVel.y += 0.5f;
    Color smoke = {180, 180, 190, 200};
    particles.emit(p.pos, trailVel, smoke, 0.3f, 0.35f,
                   ParticleSystem::Shape::Cube,
                   ParticleSystem::FLAG_GRAVITY);
  }

  p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));

  // Terrain hit — emit a small spark burst at impact, then kill it.
  // Reuses the same hit-burst helper as enemy contacts so the player
  // gets consistent feedback whether they're shooting craft or dirt.
  float ground = planet.heightAt(p.pos.x, p.pos.z);
  if (p.pos.y < ground) {
    Vector3 impact = {p.pos.x, ground + 0.2f, p.pos.z};
    emitHitBurst(impact, particles);
    // Plasma + missile both splash on terrain too.
    if (p.splashRadius > 0.0f && p.owner == ProjectileOwner::Player) {
      applySplashDamage(impact, p.splashRadius, p.damage, particles);
      emitKillExplosion(impact, particles);
    }
    p.alive = false;
    --m_liveProjectiles;
    return;
  }

  // Collision routing: player projectile → enemies, enemy projectile → player.
  if (p.owner == ProjectileOwner::Player) {
    for (auto &e : m_entities) {
      if (!e.alive) continue;
      // Friendly-fire filter — player cannon / plasma / missile /
      // cluster / depth charge / auto-turret / base-turret shots all
      // skip friendlies. Splash damage already filters friendlies
      // inside applySplashDamage(), so a player splash on a near-miss
      // can't collateral-kill a Collector either.
      if (e.type == EntityType::Collector ||
          e.type == EntityType::RepairStation ||
          e.type == EntityType::RadarBooster ||
          e.type == EntityType::Base)
        continue;
      float d = Vector3Distance(p.pos, e.pos);
      if (d < (e.radius + p.radius)) {
        emitHitBurst(p.pos, particles);
        // Damage routing branches on kind:
        //   ShieldMissile     → shield-priority split, no splash
        //   InfectiousMissile → tryInfect on the contact target,
        //                       no damage of its own (success or
        //                       dud only matters visually)
        //   everything else   → p.damage normally, splash if set
        if (p.kind == ProjectileKind::ShieldMissile) {
          applyShieldHit(e, Config::SHIELD_MISSILE_SHIELD_DMG,
                         Config::SHIELD_MISSILE_HULL_DMG, p.pos, particles);
          emitKillExplosion(p.pos, particles);
        } else if (p.kind == ProjectileKind::InfectiousMissile) {
          bool flipped = tryInfect(e);
          if (flipped) {
            // Strong visual cue on flip; the kill-burst sound + fx
            // double as the "infection started" cue.
            emitKillExplosion(p.pos, particles);
          }
          // Dud (shields up / can't be infected) — just the
          // emitHitBurst above is the feedback.
        } else {
          applyDamage(e, p.damage, p.pos, particles);
          // Splash weapons (Plasma, Missile) blast everyone else in
          // radius for the same damage figure — simple model, can
          // graduate to falloff later if it feels too generous.
          if (p.splashRadius > 0.0f) {
            applySplashDamage(p.pos, p.splashRadius, p.damage, particles);
            emitKillExplosion(p.pos, particles);
          }
        }
        p.alive = false;
        --m_liveProjectiles;
        return;
      }
    }
  } else {
    Vector3 ppos = player.position();
    float d = Vector3Distance(p.pos, ppos);
    if (d < (Config::HIT_RADIUS_PLAYER + p.radius)) {
      // Player damage — directional. The projectile's world position
      // at impact picks which shield sector absorbs the hit. Overflow
      // bleeds to hull.
      emitHitBurst(p.pos, particles);
      player.applyDamage(p.damage, p.pos);
      p.alive = false;
      --m_liveProjectiles;
      return;
    }
  }
}

// Splash damage helper — applies `damage` to every alive enemy within
// `radius` of `pos`, except friendlies. Used by Plasma + Missile on
// detonation. Each affected target gets its own applyDamage call so
// the existing shield routing + kill-burst logic fires per-target.
void EntityManager::applySplashDamage(Vector3 pos, float radius, float damage,
                                      ParticleSystem &particles) {
  float r2 = radius * radius;
  for (Entity &e : m_entities) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue;
    Vector3 d = Vector3Subtract(e.pos, pos);
    float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (d2 <= r2) {
      applyDamage(e, damage, pos, particles);
    }
  }
}

// ====================================================================
// Damage application — shield first, then hull. timeSinceHit reset so
// the shield delay counter starts over (combat_tuning.md: continuous
// pressure suppresses recharge). Damage flash gives the player visual
// confirmation; kill triggers an explosion burst.
//
// Two paths:
//  * Scalar (single-sector) shield — Fighter, Bomber. Uses
//    target.shieldHP / shieldMax; hitPos ignored.
//  * Directional (4-sector) shield — Carrier (player in Phase 4).
//    Resolves the hit sector by hit direction in target-local space,
//    drains that sector first, overflow rolls to hull. Each sector
//    has its own recharge timer so opposite faces regen
//    independently while one is being pressured.
// ====================================================================

// Detect whether the target uses directional sectors. Any non-zero
// sectorMax means we route by quadrant; this is cheaper than carrying
// a flag on every entity and lets the per-type spawn path declare
// "this entity has sectors" purely by filling the array.
static bool hasDirectionalShield(const Entity &e) {
  return e.sectorMax[0] > 0.0f || e.sectorMax[1] > 0.0f ||
         e.sectorMax[2] > 0.0f || e.sectorMax[3] > 0.0f;
}

int EntityManager::damageSectorFromHit(const Entity &target,
                                       Vector3 hitPos) const {
  // World hit direction → target-local space. Yaw rotation only —
  // pitch/roll deliberately ignored so that a banking Carrier's
  // sectors still align with cardinal facings the player can read.
  float c = cosf(target.yaw), s = sinf(target.yaw);
  Vector3 worldDir = Vector3Subtract(hitPos, target.pos);
  float lx = worldDir.x * c - worldDir.z * s;
  float lz = worldDir.x * s + worldDir.z * c;
  // Dominant axis decides front-back vs left-right; sign on that
  // axis picks the specific sector.
  if (fabsf(lz) > fabsf(lx)) {
    return (lz > 0.0f) ? static_cast<int>(ShieldSector::Front)
                       : static_cast<int>(ShieldSector::Rear);
  }
  return (lx > 0.0f) ? static_cast<int>(ShieldSector::Right)
                     : static_cast<int>(ShieldSector::Left);
}

void EntityManager::applyDamage(Entity &target, float damage,
                                Vector3 hitPos,
                                ParticleSystem &particles) {
  target.timeSinceHit = 0.0f;
  target.damageFlashTimer = 0.12f;

  bool shieldEngaged = false;
  if (hasDirectionalShield(target)) {
    // Sectored path — drain the hit quadrant; overflow goes to hull.
    int s = damageSectorFromHit(target, hitPos);
    target.sectorTimer[s] = 0.0f;
    if (target.sectorHP[s] > 0.0f) {
      shieldEngaged = true;
      if (damage <= target.sectorHP[s]) {
        target.sectorHP[s] -= damage;
        shieldfx::pushImpact(target.shieldImpactDir, target.shieldImpactTimer,
                             Config::SHIELD_IMPACT_SLOTS,
                             shieldHitDirLocal(target, hitPos));
        return;
      }
      damage -= target.sectorHP[s];
      target.sectorHP[s] = 0.0f;
    }
  } else if (target.shieldHP > 0.0f) {
    // Scalar (single-sector) path — drain shieldHP first.
    shieldEngaged = true;
    if (damage <= target.shieldHP) {
      target.shieldHP -= damage;
      shieldfx::pushImpact(target.shieldImpactDir, target.shieldImpactTimer,
                           Config::SHIELD_IMPACT_SLOTS,
                           shieldHitDirLocal(target, hitPos));
      return;
    }
    damage -= target.shieldHP;
    target.shieldHP = 0.0f;
  }
  if (shieldEngaged) {
    shieldfx::pushImpact(target.shieldImpactDir, target.shieldImpactTimer,
                         Config::SHIELD_IMPACT_SLOTS,
                         shieldHitDirLocal(target, hitPos));
  }

  target.hullHP -= damage;
  if (target.hullHP <= 0.0f) {
    target.hullHP = 0.0f;
    target.alive = false;
    --m_liveEnemies;
    emitKillExplosion(target.pos, particles);
  }
}

// ====================================================================
// Hit / kill visual bursts — small spark cloud on every hit, larger
// fireball on kill. Uses ParticleSystem's existing pool with FLAG_GRAVITY
// so the sparks fall under gravity (no bounce — they're sparks, not
// exhaust). Particles inherit no parent velocity here; the burst is
// stationary at the hit point.
// ====================================================================
void EntityManager::emitHitBurst(Vector3 pos, ParticleSystem &particles) {
  const int N = 8;
  for (int i = 0; i < N; ++i) {
    // Pseudo-random radial direction. Using the entity counter as a
    // cheap rotation source so the burst doesn't repeat the same
    // pattern across consecutive hits.
    float a = (i * 0.7853f) + (m_nextId * 0.123f); // 45° steps + jitter
    float p = ((i * 17) % 5) * 0.3f - 0.6f;        // pitch in [-0.6, 0.6]
    float speed = 6.0f + ((i * 13) % 5);
    Vector3 v = {sinf(a) * cosf(p) * speed, sinf(p) * speed + 4.0f,
                 cosf(a) * cosf(p) * speed};
    Color c = {255, static_cast<unsigned char>(180 + (i * 11) % 60),
               80, 240};
    particles.emit(pos, v, c, 0.25f, 0.35f, ParticleSystem::Shape::Cube,
                   ParticleSystem::FLAG_GRAVITY);
  }
}

void EntityManager::emitKillExplosion(Vector3 pos,
                                      ParticleSystem &particles) {
  const int N = 36;
  for (int i = 0; i < N; ++i) {
    float a = (i * 0.5236f); // 30° increments around full circle
    float p = (((i * 7) % 9) - 4) * 0.25f; // pitch fan
    float speed = 12.0f + ((i * 19) % 9);
    Vector3 v = {sinf(a) * cosf(p) * speed, sinf(p) * speed + 6.0f,
                 cosf(a) * cosf(p) * speed};
    // Hot-orange palette, larger, longer-lived. Bounce flag on so the
    // cloud rolls across nearby terrain — reads as debris.
    unsigned char r = 235 + ((i * 5) % 20);
    unsigned char g = 120 + ((i * 13) % 80);
    Color c = {r, g, 50, 245};
    particles.emit(pos, v, c, 0.55f, 0.85f, ParticleSystem::Shape::Cube,
                   ParticleSystem::FLAG_GRAVITY |
                       ParticleSystem::FLAG_BOUNCE);
  }
}

// ====================================================================
// Render — placeholder primitive geometry. Replace with procedural
// flat-shaded meshes when each enemy type lands its own visual pass.
// ====================================================================
void EntityManager::render(Camera3D camera,
                           const tsmesh::MeshRegistry *registry) const {
  for (const auto &e : m_entities) {
    if (!e.alive) continue;
    renderEnemy(e, camera, registry);
  }
  // Shield-impact caps for every shielded enemy — emitted after all
  // bodies so the translucent geometry composites over the meshes.
  // Cheap test: any entity with any active impact slot draws here.
  for (const auto &e : m_entities) {
    if (!e.alive) continue;
    float r = e.radius * Config::SHIELD_VIS_RADIUS_SCALE;
    shieldfx::renderImpacts(e.shieldImpactDir, e.shieldImpactTimer,
                            Config::SHIELD_IMPACT_SLOTS,
                            e.pos, r, e.yaw);
  }
  for (const auto &p : m_projectiles) {
    if (!p.alive) continue;
    renderProjectile(p);
  }
}

void EntityManager::renderEnemy(const Entity &e, Camera3D camera,
                                const tsmesh::MeshRegistry *registry) const {
  const Vector3 camPos = camera.position;
  switch (e.type) {
  case EntityType::Fighter: {
    Color modelTint = infectionTint(WHITE, e.aiState, e.infectionTimer);
    if (registry && registry->has(EntityType::Fighter)) {
      registry->draw(EntityType::Fighter, e.pos, e.yaw, 1.0f, modelTint);
    } else {
      // Procedural fallback — kept for when the OBJ fails to load.
      Color body = infectionTint(Color{200, 50, 50, 255}, e.aiState,
                                 e.infectionTimer);
      Color tip = infectionTint(Color{240, 200, 80, 255}, e.aiState,
                                e.infectionTimer);
      if (e.damageFlashTimer > 0.0f) {
        body = {255, 255, 255, 255};
        tip = {255, 255, 255, 255};
      }
      DrawCubeV(e.pos, {3.0f, 1.5f, 4.0f}, body);
      Vector3 nose = {e.pos.x + sinf(e.yaw) * 2.0f, e.pos.y,
                      e.pos.z + cosf(e.yaw) * 2.0f};
      DrawCubeV(nose, {1.2f, 0.8f, 1.2f}, tip);
    }
    // HP + shield bars (always rendered, mesh or procedural).
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 2.5f, 4.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    if (e.shieldMax > 0.0f && e.shieldHP > 0.0f) {
      drawHpBar(e.pos, 2.9f, 4.0f, e.shieldHP / e.shieldMax,
                {120, 180, 255, 220}, camPos);
    }
    break;
  }
  case EntityType::Drone: {
    if (registry && registry->has(EntityType::Drone)) {
      registry->draw(EntityType::Drone, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color body = {200, 80, 220, 255};
      if (e.damageFlashTimer > 0.0f) body = {255, 255, 255, 255};
      DrawCubeV(e.pos, {1.6f, 1.0f, 1.6f}, body);
      DrawCubeV({e.pos.x, e.pos.y + 0.7f, e.pos.z}, {0.4f, 0.4f, 0.4f},
                {255, 200, 255, 255});
    }
    break;
  }
  case EntityType::Bomber: {
    // Heavy blocky fuselage in dark olive — visually obvious it's
    // the slow target. Twin underwing pods + nose tip cue facing.
    Color modelTint = infectionTint(WHITE, e.aiState, e.infectionTimer);
    if (registry && registry->has(EntityType::Bomber)) {
      registry->draw(EntityType::Bomber, e.pos, e.yaw, 1.0f, modelTint);
    } else {
      Color body =
          infectionTint(Color{120, 130, 80, 255}, e.aiState, e.infectionTimer);
      Color pod =
          infectionTint(Color{90, 100, 60, 255}, e.aiState, e.infectionTimer);
      Color nose =
          infectionTint(Color{220, 180, 90, 255}, e.aiState, e.infectionTimer);
      if (e.damageFlashTimer > 0.0f) {
        body = pod = nose = {255, 255, 255, 255};
      }
      DrawCubeV(e.pos, {5.0f, 2.2f, 6.0f}, body);
      // Underwing pods — offset in local +X/-X along yaw.
      float rx = cosf(e.yaw); // local-right direction in XZ
      float rz = -sinf(e.yaw);
      Vector3 podL = {e.pos.x - rx * 2.4f, e.pos.y - 0.8f,
                      e.pos.z - rz * 2.4f};
      Vector3 podR = {e.pos.x + rx * 2.4f, e.pos.y - 0.8f,
                      e.pos.z + rz * 2.4f};
      DrawCubeV(podL, {1.2f, 0.9f, 2.5f}, pod);
      DrawCubeV(podR, {1.2f, 0.9f, 2.5f}, pod);
      Vector3 noseV = {e.pos.x + sinf(e.yaw) * 3.0f, e.pos.y,
                       e.pos.z + cosf(e.yaw) * 3.0f};
      DrawCubeV(noseV, {1.0f, 0.8f, 1.0f}, nose);
    }

    // Hull + shield bars — same convention as Fighter.
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 2.4f, 5.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    if (e.shieldMax > 0.0f && e.shieldHP > 0.0f) {
      drawHpBar(e.pos, 2.8f, 5.0f, e.shieldHP / e.shieldMax,
                {120, 180, 255, 220}, camPos);
    }
    break;
  }
  case EntityType::Seeder: {
    if (registry && registry->has(EntityType::Seeder)) {
      registry->draw(EntityType::Seeder, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color body = {120, 60, 160, 255};
      Color hatch = {200, 140, 240, 255};
      Color top = {80, 40, 120, 255};
      if (e.damageFlashTimer > 0.0f) {
        body = hatch = top = {255, 255, 255, 255};
      }
      DrawCubeV(e.pos, {6.0f, 1.6f, 4.5f}, body);
      DrawCubeV({e.pos.x, e.pos.y + 0.9f, e.pos.z}, {3.5f, 0.5f, 2.5f}, top);
      DrawCubeV({e.pos.x, e.pos.y - 0.85f, e.pos.z}, {2.5f, 0.15f, 1.8f},
                hatch);
    }

    // Hull bar — same convention as Fighter so the player can read
    // damage at a glance.
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 1.6f, 6.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::Carrier: {
    // Hull body — OBJ when available, procedural otherwise. Sector
    // shields used to draw as always-visible slab panels here; they
    // were retired with the impact-cap rebuild — shields are now
    // invisible until hit, then flash via shieldfx::renderImpacts in
    // EntityManager::render (same path as Fighter/Bomber).
    if (registry && registry->has(EntityType::Carrier)) {
      registry->draw(EntityType::Carrier, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color hull = {70, 80, 110, 255};
      Color belly = {40, 50, 80, 255};
      Color spire = {180, 200, 240, 255};
      if (e.damageFlashTimer > 0.0f) {
        hull = belly = spire = {255, 255, 255, 255};
      }
      rlPushMatrix();
      rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
      rlRotatef(e.yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
      DrawCubeV({0.0f, 0.0f, 0.0f}, {10.0f, 3.0f, 12.0f}, hull);
      DrawCubeV({0.0f, -1.8f, 0.0f}, {6.0f, 0.6f, 4.0f}, belly);
      DrawCubeV({0.0f, 1.9f, 0.0f}, {3.0f, 0.9f, 3.0f}, spire);
      rlPopMatrix();
    }

    // Hull bar — billboarded toward the camera so it reads at full
    // width from any approach angle (the carrier rotates with yaw,
    // which would otherwise show the bar edge-on at certain headings).
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 3.5f, 8.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::GroundTurret: {
    // Three-piece tower: cylindrical chassis + rotating turret cap +
    // forward-pointing barrel. Barrel rotation uses e.yaw; chassis
    // stays world-aligned so the rotation axis reads visually.
    Color base = {110, 100, 90, 255};
    if (registry && registry->has(EntityType::GroundTurret)) {
      // Whole tank (chassis + cap + barrel + tip) baked into the OBJ
      // and rotates as one with e.yaw.
      registry->draw(EntityType::GroundTurret, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color cap = {160, 70, 60, 255};
      Color barrel = {220, 220, 220, 255};
      if (e.damageFlashTimer > 0.0f) {
        base = cap = barrel = {255, 255, 255, 255};
      }
      DrawCubeV({e.pos.x, e.pos.y, e.pos.z},
                {3.0f, Config::TURRET_MOUNT_HEIGHT * 2.0f, 3.0f}, base);
      Vector3 capPos = {e.pos.x,
                        e.pos.y + Config::TURRET_BARREL_HEIGHT,
                        e.pos.z};
      DrawCubeV(capPos, {2.4f, 1.2f, 2.4f}, cap);
      float bx = sinf(e.yaw);
      float bz = cosf(e.yaw);
      Vector3 muzzle = {capPos.x + bx * 1.6f, capPos.y, capPos.z + bz * 1.6f};
      DrawCubeV(muzzle, {0.6f, 0.4f, 0.6f}, barrel);
      Vector3 tip = {capPos.x + bx * 2.4f, capPos.y, capPos.z + bz * 2.4f};
      DrawCubeV(tip, {0.35f, 0.35f, 0.35f}, {255, 230, 100, 255});
    }

    // Hull bar above the cap.
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, Config::TURRET_BARREL_HEIGHT + 1.5f, 4.0f,
                e.hullHP / e.hullMax, {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::Collector: {
    // Low boxy hull with side treads + a small cabin pip. Olive +
    // green palette to read distinct from enemy types at a glance.
    // Cabin pip switches yellow→green when cargo is loaded so the
    // player can tell "this one's heading home" at a glance.
    if (registry && registry->has(EntityType::Collector)) {
      registry->draw(EntityType::Collector, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color hull = {80, 110, 70, 255};
      Color tread = {40, 50, 35, 255};
      if (e.damageFlashTimer > 0.0f) hull = tread = {255, 255, 255, 255};
      DrawCubeV(e.pos, {3.0f, 1.4f, 4.5f}, hull);
      DrawCubeV({e.pos.x - 1.8f, e.pos.y - 0.4f, e.pos.z},
                {0.6f, 0.6f, 4.5f}, tread);
      DrawCubeV({e.pos.x + 1.8f, e.pos.y - 0.4f, e.pos.z},
                {0.6f, 0.6f, 4.5f}, tread);
    }
    // Cabin pip — always procedural (its colour flips per cargo state,
    // and rebuilding the mesh per frame to swap colour would be silly).
    Color cab = e.hasCargo ? Color{120, 220, 100, 255}
                           : Color{220, 200, 80, 255};
    if (e.damageFlashTimer > 0.0f) cab = {255, 255, 255, 255};
    DrawCubeV({e.pos.x, e.pos.y + 0.9f, e.pos.z}, {1.4f, 0.6f, 1.4f},
              cab);

    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 1.6f, 3.6f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::RepairStation: {
    // Square pad with a glowing pulse on top — pulses bright when
    // the player is within heal radius so the "I'm healing you" cue
    // reads instantly. Pulse magnitude derived from a sinusoid of
    // the entity's lifespan; cheap and doesn't need extra state.
    if (registry && registry->has(EntityType::RepairStation)) {
      registry->draw(EntityType::RepairStation, e.pos, e.yaw, 1.0f, WHITE);
    } else {
      Color base = {60, 80, 70, 255};
      Color pad = {120, 200, 140, 255};
      if (e.damageFlashTimer > 0.0f) base = pad = {255, 255, 255, 255};
      DrawCubeV(e.pos, {5.0f, 0.8f, 5.0f}, base);
      DrawCubeV({e.pos.x, e.pos.y + 0.5f, e.pos.z}, {3.5f, 0.2f, 3.5f},
                pad);
      Color cross = {240, 220, 80, 255};
      if (e.damageFlashTimer > 0.0f) cross = {255, 255, 255, 255};
      DrawCubeV({e.pos.x, e.pos.y + 0.65f, e.pos.z}, {1.8f, 0.1f, 0.4f},
                cross);
      DrawCubeV({e.pos.x, e.pos.y + 0.65f, e.pos.z}, {0.4f, 0.1f, 1.8f},
                cross);
    }

    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 1.4f, 4.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::RadarBooster: {
    // Tall cylinder-ish tower with a rotating dish on top. yaw is
    // animated in updateRadarBooster — gives the booster a clear
    // "alive" indicator at distance.
    if (registry && registry->has(EntityType::RadarBooster)) {
      // Tower + foundation come from the OBJ; the dish rotates
      // independently with e.yaw and stays procedural.
      registry->draw(EntityType::RadarBooster, e.pos, 0.0f, 1.0f, WHITE);
    } else {
      Color tower = {90, 100, 130, 255};
      if (e.damageFlashTimer > 0.0f) tower = {255, 255, 255, 255};
      DrawCubeV({e.pos.x, e.pos.y + 1.8f, e.pos.z}, {1.6f, 3.6f, 1.6f},
                tower);
      DrawCubeV({e.pos.x, e.pos.y + 0.1f, e.pos.z}, {2.6f, 0.6f, 2.6f},
                tower);
    }
    // Rotating dish + tip pip — always procedural.
    Color dish = {200, 220, 255, 255};
    if (e.damageFlashTimer > 0.0f) dish = {255, 255, 255, 255};
    float dx = sinf(e.yaw), dz = cosf(e.yaw);
    Vector3 dishPos = {e.pos.x + dx * 0.6f, e.pos.y + 3.6f,
                       e.pos.z + dz * 0.6f};
    DrawCubeV(dishPos, {1.8f, 0.5f, 1.8f}, dish);
    Vector3 tip = {e.pos.x + dx * 1.6f, e.pos.y + 3.6f,
                   e.pos.z + dz * 1.6f};
    DrawCubeV(tip, {0.5f, 0.4f, 0.5f}, {255, 220, 100, 255});

    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 4.4f, 4.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  case EntityType::Base: {
    // Wide landing pad + control tower + rotating beacon. The
    // beacon yaw animates so the base reads as "active" at distance,
    // and the central control tower gives a clear silhouette
    // collectors are visibly approaching.
    if (registry && registry->has(EntityType::Base)) {
      // Pad + lights + tower + tower-top from the OBJ. yaw=0 so the
      // static structure stays world-aligned (the base never rotates
      // as a whole); the rotating turret comes after.
      registry->draw(EntityType::Base, e.pos, 0.0f, 1.0f, WHITE);
    } else {
      Color pad = {90, 100, 110, 255};
      Color tower = {120, 130, 150, 255};
      Color top = {200, 220, 240, 255};
      if (e.damageFlashTimer > 0.0f) pad = tower = top = {255, 255, 255, 255};
      DrawCubeV({e.pos.x, e.pos.y + 0.2f, e.pos.z}, {9.0f, 0.6f, 9.0f},
                pad);
      Color light = {255, 220, 80, 255};
      DrawCubeV({e.pos.x + 4.0f, e.pos.y + 0.6f, e.pos.z + 4.0f},
                {0.5f, 0.5f, 0.5f}, light);
      DrawCubeV({e.pos.x - 4.0f, e.pos.y + 0.6f, e.pos.z + 4.0f},
                {0.5f, 0.5f, 0.5f}, light);
      DrawCubeV({e.pos.x + 4.0f, e.pos.y + 0.6f, e.pos.z - 4.0f},
                {0.5f, 0.5f, 0.5f}, light);
      DrawCubeV({e.pos.x - 4.0f, e.pos.y + 0.6f, e.pos.z - 4.0f},
                {0.5f, 0.5f, 0.5f}, light);
      DrawCubeV({e.pos.x + 2.5f, e.pos.y + 2.2f, e.pos.z + 2.5f},
                {2.0f, 4.0f, 2.0f}, tower);
      DrawCubeV({e.pos.x + 2.5f, e.pos.y + 4.5f, e.pos.z + 2.5f},
                {2.6f, 0.6f, 2.6f}, top);
    }
    // Turret cap + barrel + tip — always procedural, rotates with
    // e.yaw independently of the static structure.
    Color capCol = {180, 110, 90, 255};
    Color barrelCol = {220, 220, 220, 255};
    if (e.damageFlashTimer > 0.0f) capCol = barrelCol = {255, 255, 255, 255};
    Vector3 capPos = {e.pos.x + 2.5f, e.pos.y + Config::BASE_TURRET_BARREL_HEIGHT,
                      e.pos.z + 2.5f};
    DrawCubeV(capPos, {1.4f, 0.8f, 1.4f}, capCol);
    float dx = sinf(e.yaw), dz = cosf(e.yaw);
    Vector3 barrel = {capPos.x + dx * 1.1f, capPos.y,
                      capPos.z + dz * 1.1f};
    DrawCubeV(barrel, {0.5f, 0.4f, 0.5f}, barrelCol);
    Vector3 tip = {capPos.x + dx * 1.8f, capPos.y, capPos.z + dz * 1.8f};
    DrawCubeV(tip, {0.4f, 0.35f, 0.4f}, {255, 220, 100, 255});

    // Hull bar above the tower so killing the base is visible.
    if (e.hullMax > 0.0f) {
      drawHpBar(e.pos, 6.0f, 8.0f, e.hullHP / e.hullMax,
                {80, 220, 100, 220}, camPos);
    }
    break;
  }
  default:
    DrawCubeV(e.pos, {2.0f, 2.0f, 2.0f}, {200, 200, 200, 255});
    break;
  }
}

void EntityManager::renderProjectile(const Entity &p) const {
  switch (p.kind) {
  case ProjectileKind::Plasma: {
    // Glowing magenta orb — bigger than a cannon tracer and visibly
    // distinct so the player can see which weapon is firing.
    DrawCubeV(p.pos, {0.9f, 0.9f, 0.9f}, {220, 80, 240, 255});
    DrawCubeV(p.pos, {1.4f, 1.4f, 1.4f}, {220, 80, 240, 80}); // soft halo
    break;
  }
  case ProjectileKind::DepthCharge: {
    // Big dark drum + bright fin on top so it reads as "heavy
    // gravity bomb" against the sky as it falls.
    DrawCubeV(p.pos, {1.4f, 1.0f, 1.4f}, {60, 70, 90, 255});
    DrawCubeV({p.pos.x, p.pos.y + 0.6f, p.pos.z}, {0.5f, 0.4f, 0.5f},
              {220, 200, 80, 255});
    break;
  }
  case ProjectileKind::ClusterParent: {
    // Slightly larger silver body with a yellow band so the carrier
    // reads distinct from a single-shot missile as it streaks in.
    DrawCubeV(p.pos, {0.9f, 0.9f, 2.0f}, {220, 230, 240, 255});
    DrawCubeV(p.pos, {1.0f, 0.4f, 0.4f}, {255, 220, 80, 255});
    break;
  }
  case ProjectileKind::Missile: {
    // Elongated silver cube — reads as a missile body even at chase
    // distance. Smoke trail comes from updateProjectile's particle
    // emission, not the render path.
    DrawCubeV(p.pos, {0.7f, 0.7f, 1.6f}, {220, 230, 240, 255});
    // Red tip indicator so direction is clear.
    Vector3 tip = p.pos;
    Vector3 dir = p.vel;
    float speed = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (speed > 0.01f) {
      tip.x += dir.x * 0.8f / speed;
      tip.z += dir.z * 0.8f / speed;
    }
    DrawCubeV(tip, {0.4f, 0.4f, 0.4f}, {240, 80, 60, 255});
    break;
  }
  case ProjectileKind::ShieldMissile: {
    // Same silhouette as a standard missile but with a blue body
    // tint and blue tip so the player reads it as "shield-stripper"
    // at chase distance.
    DrawCubeV(p.pos, {0.7f, 0.7f, 1.6f}, {180, 210, 240, 255});
    Vector3 tip = p.pos;
    Vector3 dir = p.vel;
    float speed = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (speed > 0.01f) {
      tip.x += dir.x * 0.8f / speed;
      tip.z += dir.z * 0.8f / speed;
    }
    DrawCubeV(tip, {0.4f, 0.4f, 0.4f}, {80, 180, 255, 255});
    break;
  }
  case ProjectileKind::InfectiousMissile: {
    // Sickly yellow-green body + bright green tip — reads as
    // "biological" at chase distance, distinct from Shield Missile's
    // blue and standard Missile's silver+red.
    DrawCubeV(p.pos, {0.7f, 0.7f, 1.6f}, {180, 220, 120, 255});
    Vector3 tip = p.pos;
    Vector3 dir = p.vel;
    float speed = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (speed > 0.01f) {
      tip.x += dir.x * 0.8f / speed;
      tip.z += dir.z * 0.8f / speed;
    }
    DrawCubeV(tip, {0.4f, 0.4f, 0.4f}, {120, 255, 100, 255});
    break;
  }
  case ProjectileKind::Cannon:
  default: {
    Color c = (p.owner == ProjectileOwner::Player)
                  ? Color{255, 230, 80, 255}
                  : Color{255, 80, 80, 255};
    DrawCubeV(p.pos, {0.6f, 0.6f, 0.6f}, c);
    break;
  }
  }
}
