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
                                       ProjectileOwner owner) {
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

  ++m_liveProjectiles;
  return p;
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
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // EVADE entry/exit — same fraction threshold as Fighter so all
  // shielded fliers behave consistently when wounded.
  bool wantEvade = (e.hullMax > 0.0f) &&
                   ((e.hullHP / e.hullMax) < Config::AI_EVADE_HEALTH);
  if (wantEvade && dist < Config::AI_PURSUE_RANGE * 1.5f) {
    e.aiState = AIState::Evade;
  } else if (e.aiState == AIState::Evade && !wantEvade) {
    e.aiState = AIState::Pursue;
  }

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
  // a tight cone makes them effectively non-threatening.
  if (e.aiState == AIState::Attack && e.fireTimer <= 0.0f) {
    Vector3 toPlayerN =
        Vector3Normalize(Vector3Subtract(player.position(), e.pos));
    Vector3 fwd3 = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
    float dot = Vector3DotProduct(fwd3, toPlayerN);
    if (dot > 0.90f) { // ~25° cone
      fireBomberShot(e, player);
      e.fireTimer = Config::BOMBER_FIRE_RATE;
    }
  }
}

void EntityManager::fireBomberShot(Entity &e, const Player &player) {
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  float d = Vector3Length(toPlayer);
  if (d < 0.001f) return;
  Vector3 dir = Vector3Scale(toPlayer, 1.0f / d);
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
  p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));

  // Terrain hit — emit a small spark burst at impact, then kill it.
  // Reuses the same hit-burst helper as enemy contacts so the player
  // gets consistent feedback whether they're shooting craft or dirt.
  float ground = planet.heightAt(p.pos.x, p.pos.z);
  if (p.pos.y < ground) {
    Vector3 impact = {p.pos.x, ground + 0.2f, p.pos.z};
    emitHitBurst(impact, particles);
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
        applyDamage(e, p.damage, p.pos, particles);
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
  default:
    DrawCubeV(e.pos, {2.0f, 2.0f, 2.0f}, {200, 200, 200, 255});
    break;
  }
}

void EntityManager::renderProjectile(const Entity &p) const {
  Color c = (p.owner == ProjectileOwner::Player)
                ? Color{255, 230, 80, 255}
                : Color{255, 80, 80, 255};
  // Small bright cube — reads as a tracer at chase-cam distances.
  DrawCubeV(p.pos, {0.6f, 0.6f, 0.6f}, c);
}
