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
  default:
    // v1 only spawns Fighters; other types added in 5d.
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

    // Damage flash decay — render path tints the body white while > 0.
    if (e.damageFlashTimer > 0.0f) e.damageFlashTimer -= dt;

    if (e.fireTimer > 0.0f) e.fireTimer -= dt;

    switch (e.type) {
    case EntityType::Fighter:
      updateFighter(e, dt, planet, player);
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
                                  const Player &player) {
  Vector3 toPlayer = Vector3Subtract(player.position(), e.pos);
  // Horizontal-only direction for yaw control
  Vector3 toPlayerH = {toPlayer.x, 0.0f, toPlayer.z};
  float distH = Vector3Length(toPlayerH);
  float dist = Vector3Length(toPlayer);

  // Desired yaw — point at player
  if (distH > 0.001f) {
    float desiredYaw = atan2f(toPlayerH.x, toPlayerH.z);
    float yawErr = desiredYaw - e.yaw;
    while (yawErr > 3.14159f) yawErr -= 6.28318f;
    while (yawErr < -3.14159f) yawErr += 6.28318f;
    float maxStep = Config::FIGHTER_TURN_RATE * dt;
    if (yawErr > maxStep) yawErr = maxStep;
    else if (yawErr < -maxStep) yawErr = -maxStep;
    e.yaw += yawErr;
  }

  // State transition: ATTACK when in range and nose roughly aligned
  Vector3 fwd = {sinf(e.yaw), 0.0f, cosf(e.yaw)};
  if (dist < Config::AI_ATTACK_RANGE) {
    e.aiState = AIState::Attack;
  } else if (dist < Config::AI_PURSUE_RANGE) {
    e.aiState = AIState::Pursue;
  }

  // Forward thrust — applied in either state, slowing slightly inside
  // attack range so the fighter doesn't immediately fly past the player.
  float thrustScale = (e.aiState == AIState::Attack) ? 0.4f : 1.0f;
  e.vel.x += fwd.x * Config::FIGHTER_THRUST * thrustScale * dt;
  e.vel.z += fwd.z * Config::FIGHTER_THRUST * thrustScale * dt;

  // Altitude hold — try to stay at FIGHTER_PREFERRED_ALT above terrain
  float ground = planet.heightAt(e.pos.x, e.pos.z);
  float targetY = ground + Config::FIGHTER_PREFERRED_ALT;
  float altErr = targetY - e.pos.y;
  e.vel.y += altErr * 0.5f * dt; // gentle restoring force
  e.vel.y *= 1.0f - 1.0f * dt;   // damping so it doesn't oscillate

  // Speed cap
  float spd = Vector3Length(e.vel);
  if (spd > Config::FIGHTER_MAX_SPEED) {
    e.vel = Vector3Scale(e.vel, Config::FIGHTER_MAX_SPEED / spd);
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
        applyDamage(e, p.damage, particles);
        p.alive = false;
        --m_liveProjectiles;
        return;
      }
    }
  } else {
    Vector3 ppos = player.position();
    float d = Vector3Distance(p.pos, ppos);
    if (d < (Config::HIT_RADIUS_PLAYER + p.radius)) {
      // Player damage (v1: damage hull directly — directional shields
      // come in 5g when the shield system is wired into Player).
      emitHitBurst(p.pos, particles);
      player.applyDamage(p.damage);
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
// ====================================================================
void EntityManager::applyDamage(Entity &target, float damage,
                                ParticleSystem &particles) {
  target.timeSinceHit = 0.0f;
  target.damageFlashTimer = 0.12f;
  if (target.shieldHP > 0.0f) {
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
