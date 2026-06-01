#include "EntityManager.hpp"
#include "Player.hpp"
#include "core/Particles.hpp"
#include "raymath.h"
#include "rlgl.h"
#include "world/Planet.hpp"
#include <cmath>

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
  e->aiState = AIState::Pursue;

  switch (type) {
  case EntityType::Fighter:
    e->hullMax = Config::HULL_FIGHTER;
    e->hullHP = e->hullMax;
    e->shieldMax = Config::SHIELD_FIGHTER;
    e->shieldHP = e->shieldMax;
    e->shieldDelay = Config::SHIELD_DELAY_FIGHTER;
    e->shieldRate = Config::SHIELD_RATE_FIGHTER;
    e->radius = Config::HIT_RADIUS_FIGHTER;
    break;
  case EntityType::Drone:
    // Kamikaze swarm — 1-shot kill, no shield, contact damage only.
    e->hullMax = Config::HULL_DRONE;
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->radius = Config::HIT_RADIUS_DRONE;
    break;
  case EntityType::Bomber:
    // Heavy bruiser — high HP, has a shield with the slowest recharge
    // delay so sustained pressure keeps the shield down.
    e->hullMax = Config::HULL_BOMBER;
    e->hullHP = e->hullMax;
    e->shieldMax = Config::SHIELD_BOMBER;
    e->shieldHP = e->shieldMax;
    e->shieldDelay = Config::SHIELD_DELAY_BOMBER;
    e->shieldRate = Config::SHIELD_RATE_BOMBER;
    e->radius = Config::HIT_RADIUS_BOMBER;
    break;
  case EntityType::Seeder:
    // Slow flying drone-dispenser. Fragile, no shield. fireTimer is
    // re-purposed as the deploy cooldown — first drop after the
    // grace delay so the seeder doesn't dump a drone the instant it
    // pops in next to the player.
    e->hullMax = Config::HULL_SEEDER;
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->radius = Config::HIT_RADIUS_SEEDER;
    e->fireTimer = Config::SEEDER_FIRST_DROP_DELAY;
    break;
  case EntityType::Carrier:
    // Boss-tier — 4-sector directional shield, huge hull, no weapons.
    // The scalar shieldHP stays 0 (the directional-shield path is
    // triggered by sectorMax[i] > 0). shieldDelay + shieldRate are
    // shared by all sectors via the per-tick recharge loop.
    e->hullMax = Config::HULL_CARRIER;
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->shieldDelay = Config::SHIELD_DELAY_CARRIER;
    e->shieldRate = Config::SHIELD_RATE_CARRIER;
    for (int i = 0; i < 4; ++i) {
      e->sectorMax[i] = Config::SHIELD_CARRIER_PER_SECTOR;
      e->sectorHP[i] = e->sectorMax[i];
      e->sectorTimer[i] = 0.0f;
    }
    e->radius = Config::HIT_RADIUS_CARRIER;
    // fireTimer doubles as drone-deploy cooldown (same as Seeder).
    e->fireTimer = Config::CARRIER_FIRST_DROP_DELAY;
    break;
  case EntityType::GroundTurret:
    // Stationary ground threat. yaw is the barrel direction (starts
    // pointing forward by convention; updateGroundTurret rotates it
    // toward the player). pos.y is anchored to terrain at spawn.
    e->hullMax = Config::HULL_TURRET;
    e->hullHP = e->hullMax;
    e->shieldMax = 0.0f;
    e->shieldHP = 0.0f;
    e->radius = Config::HIT_RADIUS_TURRET;
    e->aiState = AIState::Idle; // turret has no pursue/evade — just track+fire
    break;
  case EntityType::Collector:
    // Ground vehicle that wanders between waypoints. fireTimer is
    // re-purposed as the waypoint-pick cooldown. stateTimer holds
    // the current yaw target (faked: encoded in vel direction).
    e->hullMax = Config::COLLECTOR_HULL;
    e->hullHP = e->hullMax;
    e->radius = Config::HIT_RADIUS_COLLECTOR;
    // Initial heading: random-ish from spawn seed; updateCollector
    // pivots toward fresh waypoints as it walks.
    e->yaw = static_cast<float>((e->id * 1103515245u) % 6283) / 1000.0f;
    e->fireTimer = 0.0f;
    break;
  case EntityType::RepairStation:
    e->hullMax = Config::REPAIR_STATION_HULL;
    e->hullHP = e->hullMax;
    e->radius = Config::HIT_RADIUS_REPAIR;
    break;
  case EntityType::RadarBooster:
    e->hullMax = Config::RADAR_BOOSTER_HULL;
    e->hullHP = e->hullMax;
    e->radius = Config::HIT_RADIUS_BOOSTER;
    // yaw is animated by updateRadarBooster (rotating dish visual).
    e->yaw = 0.0f;
    break;
  case EntityType::Base:
    // Stationary delivery destination. High HP — losing this means
    // losing the collector economy, so it should take real effort
    // to destroy. yaw animates a landing-light beacon at render time.
    e->hullMax = Config::BASE_HULL;
    e->hullHP = e->hullMax;
    e->radius = Config::HIT_RADIUS_BASE;
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
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster ||
        e.type == EntityType::Base)
      continue; // never target friendlies
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
// Update — drive enemies and tick projectiles
// ====================================================================
void EntityManager::update(float dt, const Planet &planet, Player &player,
                           ParticleSystem &particles) {
  for (auto &e : m_entities) {
    if (!e.alive) continue;

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
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // EVADE state — fighter peels AWAY from the player when its hull
  // drops below AI_EVADE_HEALTH (default 25%). Exit when it has put
  // enough distance between itself and the player to recover.
  bool wantEvade = (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < Config::AI_EVADE_HEALTH);
  if (wantEvade && dist < Config::AI_PURSUE_RANGE * 1.5f) {
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
    float hpFrac = e.hullHP / e.hullMax; // 0 .. AI_EVADE_HEALTH
    float bandT = 1.0f - (hpFrac / Config::AI_EVADE_HEALTH);
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

  // State transition: ATTACK when in range and nose roughly aligned
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  if (e.aiState != AIState::Evade) {
    if (dist < Config::AI_ATTACK_RANGE) {
      e.aiState = AIState::Attack;
    } else if (dist < Config::AI_PURSUE_RANGE) {
      e.aiState = AIState::Pursue;
    }
  }

  // Thrust — composed: eased on ATTACK; further reduced by evadeScale.
  float thrustScale = (e.aiState == AIState::Attack) ? 0.4f : 1.0f;
  thrustScale *= evadeScale;
  e.vel.x += fwd.x * Config::FIGHTER_THRUST * thrustScale * dt;
  e.vel.z += fwd.z * Config::FIGHTER_THRUST * thrustScale * dt;

  // Damaged engine smoke — small dark sputtering trail when in EVADE.
  // Stochastic emission so we don't overwhelm the particle pool with
  // multiple damaged fighters running at once.
  if (e.aiState == AIState::Evade) {
    e.fireTimer -= dt; // re-using fireTimer as the smoke-emit cooldown
    if (e.fireTimer <= 0.0f) {
      // ~40% drift back along ship velocity + small lateral jitter.
      Vector3 trailVel = Vector3Scale(e.vel, -0.3f);
      trailVel.y += 1.5f; // gentle upward drift like real smoke
      // Mostly grey/dark with a hint of orange — engine sputter look.
      Color smoke = {110, 90, 80, 220};
      // Smaller, gravity-affected; bounce off so it lingers near the
      // crash trajectory.
      particles.emit(e.pos, trailVel, smoke, 0.45f, 0.55f,
                     ParticleSystem::Shape::Cube,
                     ParticleSystem::FLAG_GRAVITY);
      e.fireTimer = 0.06f; // ~16 puffs/sec while damaged
    }
  }

  // Altitude hold — try to stay at FIGHTER_PREFERRED_ALT above terrain
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::FIGHTER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.5f * dt; // gentle restoring force
  e.vel.y *= 1.0f - 1.0f * dt;   // damping so it doesn't oscillate

  // Speed cap — also scaled by evadeScale so damaged fighters
  // can't sprint away at full speed once their engines are wrecked.
  float spd = Vector3Length(e.vel);
  float maxSpeed = Config::FIGHTER_MAX_SPEED * evadeScale;
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

  // Fire when in attack range and nose aligned within ~15°
  if (e.aiState == AIState::Attack && e.fireTimer <= 0.0f) {
    Vector3 toPlayerN =
        Vector3Normalize(Vector3Subtract(player.position(), e.pos));
    Vector3 fwd3 = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd3, toPlayerN);
    if (dot > 0.96f) { // ~16° cone
      fireFighterShot(e, player);
      e.fireTimer = Config::FIGHTER_FIRE_RATE;
    }
  }
}

void EntityManager::fireFighterShot(Entity &e, const Player &player) {
  // Lead-less aim — fire straight at current player position. Player
  // can dodge by moving sideways. Lead-aim comes when AI gets smarter.
  Vector3 toPlayer =
      Vector3Subtract(player.position(), e.pos);
  float d = Vector3Length(toPlayer);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toPlayer, 1.0f / d);
  Vector3 vel = Vector3Scale(dir, Config::FIGHTER_PROJ_SPEED);
  // Spawn just in front of the fighter
  Vector3 spawn = Vector3Add(e.pos, Vector3Scale(dir, 1.5f));
  spawnProjectile(spawn, vel, Config::FIGHTER_FIRE_DAMAGE,
                  Config::FIGHTER_PROJ_RANGE,
                  Config::FIGHTER_PROJ_SPEED, ProjectileOwner::Enemy);
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
  // Bombers prefer friendlies as targets — the "strafe run" pattern
  // from the design doc. If the nearest friendly is closer than
  // BOMBER_FRIENDLY_PRIORITY × dist-to-player, the bomber targets
  // it instead. Falls back to the player when no friendlies are alive.
  Vector3 friendlyPos;
  uint32_t friendlyId = nearestFriendly(e.pos, friendlyPos);
  Vector3 playerPos = player.position();
  float distPlayer = Vector3Distance(playerPos, e.pos);

  Vector3 targetPos = playerPos;
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

  Vector3 toPlayer = Vector3Subtract(targetPos, e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // EVADE entry/exit — same fraction threshold as Fighter so all
  // shielded fliers behave consistently when wounded. EVADE
  // overrides StrafeFriendly so a damaged bomber peels off.
  bool wantEvade = (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < Config::AI_EVADE_HEALTH);
  if (wantEvade && dist < Config::AI_PURSUE_RANGE * 1.5f) {
    e.aiState = AIState::Evade;
    targetingFriendly = false; // peel away, doesn't matter from whom
  } else if (e.aiState == AIState::Evade && !wantEvade) {
    e.aiState = AIState::Pursue;
  }
  (void)targetingFriendly; // reserved for future strafe-specific tuning

  float evadeScale = 1.0f;
  if (e.aiState == AIState::Evade && e.hullMax > 0.0f) {
    float hpFrac = e.hullHP / e.hullMax;
    float bandT = 1.0f - (hpFrac / Config::AI_EVADE_HEALTH);
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

  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  if (e.aiState != AIState::Evade) {
    if (dist < Config::AI_ATTACK_RANGE) {
      e.aiState = AIState::Attack;
    } else if (dist < Config::AI_PURSUE_RANGE) {
      e.aiState = AIState::Pursue;
    }
  }

  // Bombers ease the throttle in ATTACK so the slow projectiles get
  // a stable firing platform; full thrust during PURSUE to close.
  float thrustScale = (e.aiState == AIState::Attack) ? 0.35f : 1.0f;
  thrustScale *= evadeScale;
  e.vel.x += fwd.x * Config::BOMBER_THRUST * thrustScale * dt;
  e.vel.z += fwd.z * Config::BOMBER_THRUST * thrustScale * dt;

  // Damaged engine smoke — same emission as Fighter when in EVADE so
  // the player gets consistent "limping" feedback across enemy types.
  if (e.aiState == AIState::Evade) {
    e.fireTimer -= dt; // re-using fireTimer as smoke-emit cooldown in EVADE
    if (e.fireTimer <= 0.0f) {
      Vector3 trailVel = Vector3Scale(e.vel, -0.3f);
      trailVel.y += 1.5f;
      Color smoke = {110, 90, 80, 220};
      particles.emit(e.pos, trailVel, smoke, 0.45f, 0.55f,
                     ParticleSystem::Shape::Cube,
                     ParticleSystem::FLAG_GRAVITY);
      e.fireTimer = 0.06f;
    }
  }

  // Altitude hold (lower than Fighter — bomber lumbers near the deck).
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::BOMBER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.4f * dt;
  e.vel.y *= 1.0f - 1.0f * dt;

  // Speed cap (also scaled by evadeScale).
  float spd = Vector3Length(e.vel);
  float maxSpeed = Config::BOMBER_MAX_SPEED * evadeScale;
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

  // Fire — only outside EVADE, only when in attack range and aligned.
  // Wider cone than Fighter (~25°) because bombers turn so slowly that
  // a tight cone makes them effectively non-threatening. Aim direction
  // uses the same targetPos that drove movement, so a bomber on a
  // strafe run fires at the friendly, not the player.
  if (e.aiState == AIState::Attack && e.fireTimer <= 0.0f) {
    Vector3 toTargetN =
        Vector3Normalize(Vector3Subtract(targetPos, e.pos));
    Vector3 fwd3 = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd3, toTargetN);
    if (dot > 0.90f) { // ~25° cone
      fireBomberShot(e, targetPos);
      e.fireTimer = Config::BOMBER_FIRE_RATE;
    }
  }
}

void EntityManager::fireBomberShot(Entity &e, Vector3 targetPos) {
  Vector3 toTarget = Vector3Subtract(targetPos, e.pos);
  float d = Vector3Length(toTarget);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toTarget, 1.0f / d);
  Vector3 vel = Vector3Scale(dir, Config::BOMBER_PROJ_SPEED);
  Vector3 spawn = Vector3Add(e.pos, Vector3Scale(dir, 2.5f));
  spawnProjectile(spawn, vel, Config::BOMBER_FIRE_DAMAGE,
                  Config::BOMBER_PROJ_RANGE, Config::BOMBER_PROJ_SPEED,
                  ProjectileOwner::Enemy);
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
void EntityManager::updateDrone(Entity &e, float dt, const Planet &planet,
                                Player &player, ParticleSystem &particles) {
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
      // Closer = stronger push; inverse-distance weight.
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

  // Alignment force = (avg neighbour vel) − (own vel), nudges toward
  // group heading without snapping to it.
  Vector3 alignForce = {0, 0, 0};
  if (alignCount > 0) {
    alignAvgVel = Vector3Scale(alignAvgVel, 1.0f / alignCount);
    alignForce = Vector3Subtract(alignAvgVel, e.vel);
  }

  // Cohesion force = direction from drone toward neighbour centroid.
  Vector3 cohesionForce = {0, 0, 0};
  if (cohesionCount > 0) {
    cohesionAvgPos = Vector3Scale(cohesionAvgPos, 1.0f / cohesionCount);
    cohesionForce = Vector3Subtract(cohesionAvgPos, e.pos);
    float clen = Vector3Length(cohesionForce);
    if (clen > 0.01f)
      cohesionForce = Vector3Scale(cohesionForce, 1.0f / clen);
  }

  // Pursuit — head toward player (3D, not just horizontal).
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  float pdist = Vector3Length(toPlayer);
  Vector3 pursueForce = {0, 0, 0};
  if (pdist > 0.01f)
    pursueForce = Vector3Scale(toPlayer, 1.0f / pdist);

  // Sum weighted forces — drives velocity rather than position
  // directly so the drone has momentum (matches the rest of the
  // physics-based world).
  Vector3 total = {0, 0, 0};
  total = Vector3Add(total, Vector3Scale(sepForce, Config::DRONE_SEP_WEIGHT));
  total =
      Vector3Add(total, Vector3Scale(alignForce, Config::DRONE_ALIGN_WEIGHT));
  total = Vector3Add(
      total, Vector3Scale(cohesionForce, Config::DRONE_COHESION_WEIGHT));
  total =
      Vector3Add(total, Vector3Scale(pursueForce, Config::DRONE_PURSUE_WEIGHT));

  e.vel = Vector3Add(e.vel, Vector3Scale(total, Config::DRONE_THRUST * dt));

  // Speed cap
  float spd = Vector3Length(e.vel);
  if (spd > Config::DRONE_MAX_SPEED)
    e.vel = Vector3Scale(e.vel, Config::DRONE_MAX_SPEED / spd);

  // Light drag so drones don't oscillate forever after a force pulse.
  float drag = 0.4f * dt;
  if (drag > 1.0f) drag = 1.0f;
  e.vel = Vector3Scale(e.vel, 1.0f - drag);

  // Update yaw to match horizontal velocity (purely visual — used for
  // the radar triangle direction even though drones render as cubes).
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

  // Kamikaze contact — if the drone is touching the player, deal
  // contact damage and self-destruct (with a kill burst at the
  // impact point so the player sees it die). Drone position is used
  // as the hit point so the shield sector facing the swarm absorbs
  // the impact.
  Vector3 toPlayer3 = Vector3Subtract(player.position(), e.pos);
  if (Vector3Length(toPlayer3) <
      (e.radius + Config::HIT_RADIUS_PLAYER)) {
    player.applyDamage(Config::DRONE_CONTACT_DAMAGE, e.pos);
    e.alive = false;
    --m_liveEnemies;
    emitKillExplosion(e.pos, particles);
  }
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
  // Gate on player range so distant seeders don't flood the global pool.
  float dist = Vector3Length(toPlayer);
  if (e.fireTimer <= 0.0f && dist < Config::SEEDER_DEPLOY_RANGE) {
    // Spawn the drone slightly below the seeder so it visibly "drops"
    // from the underside before the boids forces take over.
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

  // Drone drop — fireTimer already decremented at the top of update().
  // Drop from the underside of the carrier hull.
  float dist = Vector3Length(toPlayer);
  if (e.fireTimer <= 0.0f && dist < Config::CARRIER_DEPLOY_RANGE) {
    Vector3 dropPos = {e.pos.x, e.pos.y - 6.0f, e.pos.z};
    spawnEnemy(EntityType::Drone, dropPos);
    e.fireTimer = Config::CARRIER_DEPLOY_INTERVAL;
  }
}

// ====================================================================
// Ground Turret — stationary, anchored to terrain. Rotates the barrel
// toward the player at TURRET_AIM_RATE; fires when the player is in
// range AND the barrel is within the firing cone. No movement, no
// shield — it's a hard point that the player has to suppress.
//
// pos is fixed at spawn (set y to heightAt + mount height there). yaw
// stores the barrel angle, NOT the chassis angle — the rendered base
// stays world-aligned, the turret cap rotates.
// ====================================================================
void EntityManager::updateGroundTurret(Entity &e, float dt,
                                       const Planet &planet,
                                       const Player &player) {
  // Resnap to terrain in case the heightmap shifts (e.g., procedural
  // re-gen). Cheap and keeps barrel pivot honest.
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  e.pos.y = ground + Config::TURRET_MOUNT_HEIGHT;

  // Barrel pivot is above the chassis — aim from there so the lead
  // looks correct visually.
  Vector3 muzzle = {e.pos.x, e.pos.y + Config::TURRET_BARREL_HEIGHT, e.pos.z};
  Vector3 toPlayer = Vector3Subtract(player.position(), muzzle);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  if (distH > 0.001f) {
    float desiredYaw = atan2f(toPlayerH.x, toPlayerH.z);
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = Config::TURRET_AIM_RATE * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // Fire: in range + nose-aligned within fire cone + cooldown ready.
  if (dist < Config::TURRET_FIRE_RANGE && e.fireTimer <= 0.0f) {
    Vector3 toPlayerN = Vector3Normalize(toPlayer);
    Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd, toPlayerN);
    // Use a more permissive cone than fighters because the turret is
    // immobile horizontally — the player can always strafe out, so
    // tight cones make turrets useless.
    if (dot > (1.0f - Config::TURRET_FIRE_CONE_ENEMY)) {
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
// Base — stationary delivery destination. No movement; yaw animates
// a rotating landing-light beacon for the render path.
// ====================================================================
void EntityManager::updateBase(Entity &e, float dt) {
  e.yaw += 1.0f * dt;
  if (e.yaw > 6.28318f) e.yaw -= 6.28318f;
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
                   p.kind == ProjectileKind::ClusterParent) &&
                  p.turnRate > 0.0f;
  if (isGuided) {
    // Resolve current target by id.
    const Entity *target = nullptr;
    if (p.seekTargetId != 0) {
      for (const Entity &e : m_entities) {
        if (e.alive && e.id == p.seekTargetId) {
          target = &e;
          break;
        }
      }
    }
    // Reacquire if the prior target is gone (or never had one).
    if (!target) {
      uint32_t newId = 0;
      float bestDist = Config::MISSILE_REACQUIRE_RANGE;
      for (const Entity &e : m_entities) {
        if (!e.alive) continue;
        if (e.type == EntityType::Projectile) continue;
        if (e.type == EntityType::Collector ||
            e.type == EntityType::RepairStation ||
            e.type == EntityType::RadarBooster ||
            e.type == EntityType::Base)
          continue;
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

      // ClusterParent split check — if we're inside the split radius,
      // detonate the carrier and spawn 4 sub-missiles. Each child
      // inherits the carrier's speed + a 4-way perpendicular spread,
      // gets a fresh local target lock so they spread out across
      // adjacent enemies.
      if (p.kind == ProjectileKind::ClusterParent &&
          dist < Config::CLUSTER_SPLIT_DISTANCE) {
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
          // Each child reacquires individually next tick (id=0).
          // Damage: each sub-missile carries 45% of MISSILE_DAMAGE so
          // a single carrier overlapping 4 enemies delivers ~1.8x
          // single-missile damage in total.
          spawnProjectile(childPos, childVel,
                          Config::MISSILE_DAMAGE * 0.45f,
                          Config::MISSILE_RANGE, Config::MISSILE_SPEED,
                          ProjectileOwner::Player,
                          ProjectileKind::Missile, Config::PLASMA_SPLASH,
                          0 /* no lock — children reacquire next tick */,
                          Config::MISSILE_TURN_RATE);
        }
        // Carrier vanishes silently (no kill burst — it's a carrier,
        // not a warhead).
        p.alive = false;
        --m_liveProjectiles;
        return;
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
                          Config::MISSILE_DAMAGE * 0.45f,
                          Config::MISSILE_RANGE, Config::MISSILE_SPEED,
                          ProjectileOwner::Player,
                          ProjectileKind::Missile, Config::PLASMA_SPLASH,
                          0, Config::MISSILE_TURN_RATE);
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
      float d = Vector3Distance(p.pos, e.pos);
      if (d < (e.radius + p.radius)) {
        emitHitBurst(p.pos, particles);
        // Direct-impact target takes the full damage.
        applyDamage(e, p.damage, p.pos, particles);
        // Splash weapons (Plasma, Missile) blast everyone else in
        // radius for the same damage figure — simple model, can
        // graduate to falloff later if it feels too generous.
        if (p.splashRadius > 0.0f) {
          applySplashDamage(p.pos, p.splashRadius, p.damage, particles);
          emitKillExplosion(p.pos, particles);
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

  if (hasDirectionalShield(target)) {
    // Sectored path — drain the hit quadrant; overflow goes to hull.
    int s = damageSectorFromHit(target, hitPos);
    target.sectorTimer[s] = 0.0f;
    if (target.sectorHP[s] > 0.0f) {
      if (damage <= target.sectorHP[s]) {
        target.sectorHP[s] -= damage;
        return;
      }
      damage -= target.sectorHP[s];
      target.sectorHP[s] = 0.0f;
    }
  } else if (target.shieldHP > 0.0f) {
    // Scalar (single-sector) path — drain shieldHP first.
    if (damage <= target.shieldHP) {
      target.shieldHP -= damage;
      return;
    }
    damage -= target.shieldHP;
    target.shieldHP = 0.0f;
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
void EntityManager::render() const {
  for (const auto &e : m_entities) {
    if (!e.alive) continue;
    renderEnemy(e);
  }
  for (const auto &p : m_projectiles) {
    if (!p.alive) continue;
    renderProjectile(p);
  }
}

void EntityManager::renderEnemy(const Entity &e) const {
  switch (e.type) {
  case EntityType::Fighter: {
    // Simple red wedge — flat-shaded cube with a forward-pointing cap.
    // Replaced by a proper procedural mesh in a later pass.
    Color body = {200, 50, 50, 255};
    Color tip = {240, 200, 80, 255};
    // Damage flash — fully white for the first ~0.12s after a hit so
    // the player sees impact even when shield/hull bars are off-screen.
    if (e.damageFlashTimer > 0.0f) {
      body = {255, 255, 255, 255};
      tip = {255, 255, 255, 255};
    }
    DrawCubeV(e.pos, {3.0f, 1.5f, 4.0f}, body);
    Vector3 nose = {e.pos.x + sinf(e.yaw) * 2.0f, e.pos.y,
                    e.pos.z + cosf(e.yaw) * 2.0f};
    DrawCubeV(nose, {1.2f, 0.8f, 1.2f}, tip);

    // Health hint — a thin line above the cube whose length scales
    // with hull HP. Quick visual confirmation of damage in v1.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 2.0f, e.pos.y + 2.5f, e.pos.z};
      Vector3 b = {e.pos.x - 2.0f + 4.0f * t, e.pos.y + 2.5f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    if (e.shieldMax > 0.0f && e.shieldHP > 0.0f) {
      float t = e.shieldHP / e.shieldMax;
      Vector3 a = {e.pos.x - 2.0f, e.pos.y + 2.9f, e.pos.z};
      Vector3 b = {e.pos.x - 2.0f + 4.0f * t, e.pos.y + 2.9f, e.pos.z};
      DrawLine3D(a, b, {120, 180, 255, 220});
    }
    break;
  }
  case EntityType::Drone: {
    // Magenta diamond — distinct from red fighters at a glance.
    Color body = {200, 80, 220, 255};
    if (e.damageFlashTimer > 0.0f) body = {255, 255, 255, 255};
    // Stretched cube oriented along velocity gives a "buzzing
    // forward" read against the swarm. Procedural mesh later.
    DrawCubeV(e.pos, {1.6f, 1.0f, 1.6f}, body);
    // Tiny bright pip on top for visibility against terrain.
    DrawCubeV({e.pos.x, e.pos.y + 0.7f, e.pos.z}, {0.4f, 0.4f, 0.4f},
              {255, 200, 255, 255});
    break;
  }
  case EntityType::Bomber: {
    // Heavy blocky fuselage in dark olive — visually obvious it's
    // the slow target. Twin underwing pods + nose tip cue facing.
    Color body = {120, 130, 80, 255};
    Color pod = {90, 100, 60, 255};
    Color nose = {220, 180, 90, 255};
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
    // Forward nose pip.
    Vector3 noseV = {e.pos.x + sinf(e.yaw) * 3.0f, e.pos.y,
                     e.pos.z + cosf(e.yaw) * 3.0f};
    DrawCubeV(noseV, {1.0f, 0.8f, 1.0f}, nose);

    // Hull + shield bars — same convention as Fighter.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 2.5f, e.pos.y + 2.4f, e.pos.z};
      Vector3 b = {e.pos.x - 2.5f + 5.0f * t, e.pos.y + 2.4f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    if (e.shieldMax > 0.0f && e.shieldHP > 0.0f) {
      float t = e.shieldHP / e.shieldMax;
      Vector3 a = {e.pos.x - 2.5f, e.pos.y + 2.8f, e.pos.z};
      Vector3 b = {e.pos.x - 2.5f + 5.0f * t, e.pos.y + 2.8f, e.pos.z};
      DrawLine3D(a, b, {120, 180, 255, 220});
    }
    break;
  }
  case EntityType::Seeder: {
    // Squat dark-purple lozenge — reads as a slow carrier from any
    // angle. Underside hatch hint (lighter band) cues "drone drops
    // come out of here". Damage flash overrides everything.
    Color body = {120, 60, 160, 255};
    Color hatch = {200, 140, 240, 255};
    Color top = {80, 40, 120, 255};
    if (e.damageFlashTimer > 0.0f) {
      body = hatch = top = {255, 255, 255, 255};
    }
    DrawCubeV(e.pos, {6.0f, 1.6f, 4.5f}, body);
    DrawCubeV({e.pos.x, e.pos.y + 0.9f, e.pos.z}, {3.5f, 0.5f, 2.5f}, top);
    // Underside hatch — thin bright band so the player can see drops.
    DrawCubeV({e.pos.x, e.pos.y - 0.85f, e.pos.z}, {2.5f, 0.15f, 1.8f},
              hatch);

    // Hull bar — same convention as Fighter so the player can read
    // damage at a glance.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 3.0f, e.pos.y + 1.6f, e.pos.z};
      Vector3 b = {e.pos.x - 3.0f + 6.0f * t, e.pos.y + 1.6f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::Carrier: {
    // Wide blocky hull + 4 directional shield panels. Whole model is
    // drawn in carrier-local space inside an rlPushMatrix / rlRotatef
    // so the hull AND the shield slabs rotate together — DrawCubeV
    // always renders axis-aligned cubes, so without this the panels
    // would slide across the world axes as the carrier yawed.
    // (Procedural mesh replacement later — see ship-extraction note.)
    Color hull = {70, 80, 110, 255};
    Color belly = {40, 50, 80, 255};
    Color spire = {180, 200, 240, 255};
    if (e.damageFlashTimer > 0.0f) {
      hull = belly = spire = {255, 255, 255, 255};
    }

    rlPushMatrix();
    rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
    // Yaw is around world-up; raylib's rlRotatef takes degrees.
    rlRotatef(e.yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);

    // Hull + belly + spire — all at local origin, axis-aligned in
    // local space which IS the carrier's frame post-rotation.
    DrawCubeV({0.0f, 0.0f, 0.0f}, {10.0f, 3.0f, 12.0f}, hull);
    DrawCubeV({0.0f, -1.8f, 0.0f}, {6.0f, 0.6f, 4.0f}, belly);
    DrawCubeV({0.0f, 1.9f, 0.0f}, {3.0f, 0.9f, 3.0f}, spire);

    // Sector shield panels — one per face, in carrier-local space.
    // Front = +Z, Rear = -Z, Right = +X, Left = -X. Slab thin along
    // the face normal, wide across the face. Alpha tied to HP fraction
    // so a depleted sector visually vanishes.
    struct PanelDef {
      Vector3 pos;
      Vector3 size;
      int sector;
    };
    const PanelDef panels[4] = {
        {{0.0f, 0.0f, 6.5f}, {11.0f, 3.5f, 0.3f},
         static_cast<int>(ShieldSector::Front)},
        {{0.0f, 0.0f, -6.5f}, {11.0f, 3.5f, 0.3f},
         static_cast<int>(ShieldSector::Rear)},
        {{5.5f, 0.0f, 0.0f}, {0.3f, 3.5f, 13.0f},
         static_cast<int>(ShieldSector::Right)},
        {{-5.5f, 0.0f, 0.0f}, {0.3f, 3.5f, 13.0f},
         static_cast<int>(ShieldSector::Left)},
    };
    for (const PanelDef &pd : panels) {
      if (e.sectorMax[pd.sector] <= 0.0f) continue;
      float frac = e.sectorHP[pd.sector] / e.sectorMax[pd.sector];
      if (frac <= 0.01f) continue;
      unsigned char alpha =
          static_cast<unsigned char>(40.0f + 120.0f * frac); // 40..160
      Color shield = {static_cast<unsigned char>(60 + 60 * (1.0f - frac)),
                      static_cast<unsigned char>(180 + 40 * frac),
                      static_cast<unsigned char>(220), alpha};
      DrawCubeV(pd.pos, pd.size, shield);
    }

    rlPopMatrix();

    // Hull bar — drawn in world space (above the carrier centre)
    // because the bar is a UI cue, not a physical fixture; should
    // read identically from any angle.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 4.0f, e.pos.y + 3.5f, e.pos.z};
      Vector3 b = {e.pos.x - 4.0f + 8.0f * t, e.pos.y + 3.5f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::GroundTurret: {
    // Three-piece tower: cylindrical chassis + rotating turret cap +
    // forward-pointing barrel. Barrel rotation uses e.yaw; chassis
    // stays world-aligned so the rotation axis reads visually.
    Color base = {110, 100, 90, 255};
    Color cap = {160, 70, 60, 255};
    Color barrel = {220, 220, 220, 255};
    if (e.damageFlashTimer > 0.0f) {
      base = cap = barrel = {255, 255, 255, 255};
    }
    // Chassis — boxy base flush with terrain.
    DrawCubeV({e.pos.x, e.pos.y, e.pos.z}, {3.0f, Config::TURRET_MOUNT_HEIGHT * 2.0f,
              3.0f}, base);
    // Turret cap at barrel pivot height.
    Vector3 capPos = {e.pos.x,
                      e.pos.y + Config::TURRET_BARREL_HEIGHT,
                      e.pos.z};
    DrawCubeV(capPos, {2.4f, 1.2f, 2.4f}, cap);
    // Barrel — extruded cube forward of cap along yaw.
    float bx = sinf(e.yaw);
    float bz = cosf(e.yaw);
    Vector3 muzzle = {capPos.x + bx * 1.6f, capPos.y, capPos.z + bz * 1.6f};
    DrawCubeV(muzzle, {0.6f, 0.4f, 0.6f}, barrel);
    // Tip pip — bright dot at barrel tip so the firing direction is
    // obvious from any camera angle.
    Vector3 tip = {capPos.x + bx * 2.4f, capPos.y, capPos.z + bz * 2.4f};
    DrawCubeV(tip, {0.35f, 0.35f, 0.35f}, {255, 230, 100, 255});

    // Hull bar above the cap.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 2.0f,
                   e.pos.y + Config::TURRET_BARREL_HEIGHT + 1.5f, e.pos.z};
      Vector3 b = {e.pos.x - 2.0f + 4.0f * t,
                   e.pos.y + Config::TURRET_BARREL_HEIGHT + 1.5f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::Collector: {
    // Low boxy hull with side treads + a small cabin pip. Olive +
    // green palette to read distinct from enemy types at a glance.
    // Cabin pip switches yellow→green when cargo is loaded so the
    // player can tell "this one's heading home" at a glance.
    Color hull = {80, 110, 70, 255};
    Color tread = {40, 50, 35, 255};
    Color cab = e.hasCargo ? Color{120, 220, 100, 255}
                           : Color{220, 200, 80, 255};
    if (e.damageFlashTimer > 0.0f) hull = tread = cab = {255, 255, 255, 255};
    DrawCubeV(e.pos, {3.0f, 1.4f, 4.5f}, hull);
    // Treads to either side of the hull (axis-aligned for now; turret
    // body is small enough that the slide is barely visible).
    DrawCubeV({e.pos.x - 1.8f, e.pos.y - 0.4f, e.pos.z},
              {0.6f, 0.6f, 4.5f}, tread);
    DrawCubeV({e.pos.x + 1.8f, e.pos.y - 0.4f, e.pos.z},
              {0.6f, 0.6f, 4.5f}, tread);
    DrawCubeV({e.pos.x, e.pos.y + 0.9f, e.pos.z}, {1.4f, 0.6f, 1.4f},
              cab);

    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 1.8f, e.pos.y + 1.6f, e.pos.z};
      Vector3 b = {e.pos.x - 1.8f + 3.6f * t, e.pos.y + 1.6f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::RepairStation: {
    // Square pad with a glowing pulse on top — pulses bright when
    // the player is within heal radius so the "I'm healing you" cue
    // reads instantly. Pulse magnitude derived from a sinusoid of
    // the entity's lifespan; cheap and doesn't need extra state.
    Color base = {60, 80, 70, 255};
    Color pad = {120, 200, 140, 255};
    if (e.damageFlashTimer > 0.0f) base = pad = {255, 255, 255, 255};
    // Base pad — squat and wide.
    DrawCubeV(e.pos, {5.0f, 0.8f, 5.0f}, base);
    DrawCubeV({e.pos.x, e.pos.y + 0.5f, e.pos.z}, {3.5f, 0.2f, 3.5f},
              pad);
    // Yellow cross on top — universal "heal" cue.
    Color cross = {240, 220, 80, 255};
    if (e.damageFlashTimer > 0.0f) cross = {255, 255, 255, 255};
    DrawCubeV({e.pos.x, e.pos.y + 0.65f, e.pos.z}, {1.8f, 0.1f, 0.4f},
              cross);
    DrawCubeV({e.pos.x, e.pos.y + 0.65f, e.pos.z}, {0.4f, 0.1f, 1.8f},
              cross);

    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 2.0f, e.pos.y + 1.4f, e.pos.z};
      Vector3 b = {e.pos.x - 2.0f + 4.0f * t, e.pos.y + 1.4f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::RadarBooster: {
    // Tall cylinder-ish tower with a rotating dish on top. yaw is
    // animated in updateRadarBooster — gives the booster a clear
    // "alive" indicator at distance.
    Color tower = {90, 100, 130, 255};
    Color dish = {200, 220, 255, 255};
    if (e.damageFlashTimer > 0.0f) tower = dish = {255, 255, 255, 255};
    // Tower body.
    DrawCubeV({e.pos.x, e.pos.y + 1.8f, e.pos.z}, {1.6f, 3.6f, 1.6f},
              tower);
    // Foundation.
    DrawCubeV({e.pos.x, e.pos.y + 0.1f, e.pos.z}, {2.6f, 0.6f, 2.6f},
              tower);
    // Rotating dish — extruded along yaw.
    float dx = sinf(e.yaw), dz = cosf(e.yaw);
    Vector3 dishPos = {e.pos.x + dx * 0.6f, e.pos.y + 3.6f,
                       e.pos.z + dz * 0.6f};
    DrawCubeV(dishPos, {1.8f, 0.5f, 1.8f}, dish);
    // Bright tip pip so the dish direction is readable.
    Vector3 tip = {e.pos.x + dx * 1.6f, e.pos.y + 3.6f,
                   e.pos.z + dz * 1.6f};
    DrawCubeV(tip, {0.5f, 0.4f, 0.5f}, {255, 220, 100, 255});

    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 2.0f, e.pos.y + 4.4f, e.pos.z};
      Vector3 b = {e.pos.x - 2.0f + 4.0f * t, e.pos.y + 4.4f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
    }
    break;
  }
  case EntityType::Base: {
    // Wide landing pad + control tower + rotating beacon. The
    // beacon yaw animates so the base reads as "active" at distance,
    // and the central control tower gives a clear silhouette
    // collectors are visibly approaching.
    Color pad = {90, 100, 110, 255};
    Color tower = {120, 130, 150, 255};
    Color top = {200, 220, 240, 255};
    if (e.damageFlashTimer > 0.0f) pad = tower = top = {255, 255, 255, 255};

    // Landing pad — wide square.
    DrawCubeV({e.pos.x, e.pos.y + 0.2f, e.pos.z}, {9.0f, 0.6f, 9.0f},
              pad);
    // Edge lighting strips — small bright cubes at each corner.
    Color light = {255, 220, 80, 255};
    DrawCubeV({e.pos.x + 4.0f, e.pos.y + 0.6f, e.pos.z + 4.0f},
              {0.5f, 0.5f, 0.5f}, light);
    DrawCubeV({e.pos.x - 4.0f, e.pos.y + 0.6f, e.pos.z + 4.0f},
              {0.5f, 0.5f, 0.5f}, light);
    DrawCubeV({e.pos.x + 4.0f, e.pos.y + 0.6f, e.pos.z - 4.0f},
              {0.5f, 0.5f, 0.5f}, light);
    DrawCubeV({e.pos.x - 4.0f, e.pos.y + 0.6f, e.pos.z - 4.0f},
              {0.5f, 0.5f, 0.5f}, light);

    // Control tower offset from centre so collectors land on a
    // clear part of the pad.
    DrawCubeV({e.pos.x + 2.5f, e.pos.y + 2.2f, e.pos.z + 2.5f},
              {2.0f, 4.0f, 2.0f}, tower);
    DrawCubeV({e.pos.x + 2.5f, e.pos.y + 4.5f, e.pos.z + 2.5f},
              {2.6f, 0.6f, 2.6f}, top);
    // Rotating beacon on top of the tower — yaw animates.
    float dx = sinf(e.yaw), dz = cosf(e.yaw);
    Vector3 beacon = {e.pos.x + 2.5f + dx * 0.6f, e.pos.y + 5.0f,
                      e.pos.z + 2.5f + dz * 0.6f};
    DrawCubeV(beacon, {0.5f, 0.5f, 0.5f}, {255, 100, 80, 255});

    // Hull bar above the tower so killing the base is visible.
    if (e.hullMax > 0.0f) {
      float t = e.hullHP / e.hullMax;
      if (t < 0.0f) t = 0.0f;
      Vector3 a = {e.pos.x - 4.0f, e.pos.y + 6.0f, e.pos.z};
      Vector3 b = {e.pos.x - 4.0f + 8.0f * t, e.pos.y + 6.0f, e.pos.z};
      DrawLine3D(a, b, {80, 220, 100, 220});
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
