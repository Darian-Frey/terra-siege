#include "WaveManager.hpp"

#include "core/Config.hpp"
#include "entity/EntityManager.hpp"
#include "entity/Player.hpp"
#include "raymath.h"
#include "world/Planet.hpp"
#include <cmath>

// ====================================================================
// Wave table — v1 escalation. Past the last entry, the manager
// extrapolates linearly (count += 4 per wave, interval -= 0.05 per
// wave clamped to 0.4 minimum). Player breathes in WAVE_INTERMISSION
// seconds between waves.
// ====================================================================
namespace {

// Wave 1: drone swarm — fast spawn, harmless individually but
// dangerous in numbers. Wave 2: fighters — track + shoot pressure.
// Wave 3: first ground turret pair — introduces low-altitude risk in
// isolation so the player learns to suppress them. Wave 4: drones +
// turrets — combined air/ground pressure. Wave 5: first seeder —
// player has to prioritise the carrier or the swarm never ends.
// Later waves stack types and tighten cadence. When the full roster
// (Bomber/Carrier) lands these tables grow into mixed-type waves.
constexpr WaveDef WAVE_TABLE[] = {
    {EntityType::Drone, 8, 0.5f},
    {EntityType::Fighter, 3, 1.5f},
    {EntityType::GroundTurret, 2, 1.5f},
    {EntityType::Drone, 12, 0.4f},
    {EntityType::Fighter, 5, 1.2f},
    {EntityType::Seeder, 1, 0.0f},
    {EntityType::GroundTurret, 3, 1.5f},
    {EntityType::Drone, 16, 0.35f},
    {EntityType::Fighter, 8, 1.0f},
    {EntityType::Seeder, 2, 3.0f},
    {EntityType::Drone, 24, 0.3f},
    {EntityType::Fighter, 12, 0.7f},
};
constexpr int WAVE_TABLE_SIZE =
    static_cast<int>(sizeof(WAVE_TABLE) / sizeof(WAVE_TABLE[0]));

} // namespace

// xorshift32 RNG — local to the wave manager so reseeding the world
// doesn't disturb spawn patterns.
float WaveManager::randF(float lo, float hi) {
  m_rng ^= m_rng << 13;
  m_rng ^= m_rng >> 17;
  m_rng ^= m_rng << 5;
  float t = static_cast<float>(m_rng) / static_cast<float>(0xFFFFFFFFu);
  return lo + t * (hi - lo);
}

void WaveManager::reset() {
  m_state = State::FirstDelay;
  m_waveIndex = 0;
  m_remainingToSpawn = 0;
  m_spawnTimer = 0.0f;
  m_intermissionTimer = 0.0f;
  m_firstDelayTimer = Config::WAVE_FIRST_DELAY;
}

void WaveManager::skipRemainingSpawns() {
  m_remainingToSpawn = 0;
  // If we were mid-spawn the WaveActive check fires next tick. If
  // we were already WaveActive, killAllEnemies() drives the
  // wave-clear transition. Either way, no special-casing needed.
  if (m_state == State::Spawning) {
    m_state = State::WaveActive;
  }
}

WaveDef WaveManager::getWaveDef(int waveIdx) const {
  if (waveIdx < WAVE_TABLE_SIZE) return WAVE_TABLE[waveIdx];

  // Past the table: extrapolate from the final entry.
  WaveDef def = WAVE_TABLE[WAVE_TABLE_SIZE - 1];
  int over = waveIdx - (WAVE_TABLE_SIZE - 1);
  def.count += 4 * over;
  def.spawnInterval -= 0.05f * over;
  if (def.spawnInterval < 0.4f) def.spawnInterval = 0.4f;
  return def;
}

void WaveManager::startWave(int waveIdx) {
  WaveDef def = getWaveDef(waveIdx);
  m_remainingToSpawn = def.count;
  m_spawnInterval = def.spawnInterval;
  // Spawn the first one almost immediately so the player has something
  // to track right after the intermission ends.
  m_spawnTimer = 0.2f;
  m_state = State::Spawning;
}

Vector3 WaveManager::pickSpawnLocation(const Player &player) {
  // Random angle around the player + random radius in [min, max].
  float a = randF(0.0f, 6.28318f);
  float r = randF(Config::WAVE_SPAWN_RING_MIN, Config::WAVE_SPAWN_RING_MAX);
  Vector3 ppos = player.position();
  return {ppos.x + r * sinf(a), ppos.y + Config::WAVE_SPAWN_ALT_OFFSET,
          ppos.z + r * cosf(a)};
}

// Ground-anchored spawn — used for stationary terrain enemies. We
// place at TURRET_GROUND_SPAWN_DIST around the player so the turret
// is in cannon range from the start (otherwise the player flies past
// without ever engaging it). Y is set on terrain; updateGroundTurret
// re-snaps every tick anyway, so a coarse heightAt here is fine.
Vector3 WaveManager::pickGroundSpawnLocation(const Player &player,
                                             const Planet &planet) {
  float a = randF(0.0f, 6.28318f);
  float r = Config::TURRET_GROUND_SPAWN_DIST +
            randF(-20.0f, 20.0f); // small jitter so they don't ring up
  Vector3 ppos = player.position();
  float x = ppos.x + r * sinf(a);
  float z = ppos.z + r * cosf(a);
  float y = planet.heightAt(x, z) + Config::TURRET_MOUNT_HEIGHT;
  return {x, y, z};
}

int WaveManager::aliveEnemyCount(const EntityManager &entities) const {
  // Count enemies in the manager's pool. Includes any not-yet-spawned
  // count externally — the caller compares against m_remainingToSpawn.
  int n = 0;
  for (const Entity &e : entities.entities()) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;
    if (e.type == EntityType::Collector ||
        e.type == EntityType::RepairStation ||
        e.type == EntityType::RadarBooster)
      continue; // friendlies don't count toward wave-clear
    ++n;
  }
  return n;
}

// ====================================================================
// update — drive the state machine.
// ====================================================================
void WaveManager::update(float dt, EntityManager &entities,
                         const Player &player, const Planet &planet) {
  switch (m_state) {
  case State::FirstDelay: {
    m_firstDelayTimer -= dt;
    if (m_firstDelayTimer <= 0.0f) {
      startWave(0);
    }
    break;
  }

  case State::Spawning: {
    m_spawnTimer -= dt;
    while (m_spawnTimer <= 0.0f && m_remainingToSpawn > 0) {
      WaveDef def = getWaveDef(m_waveIndex);
      // Ground turrets spawn on terrain at a fixed close ring so the
      // player engages them; everything else spawns in the airborne
      // ring around the player at altitude.
      Vector3 spawnPos = (def.enemyType == EntityType::GroundTurret)
                             ? pickGroundSpawnLocation(player, planet)
                             : pickSpawnLocation(player);
      entities.spawnEnemy(def.enemyType, spawnPos);
      --m_remainingToSpawn;
      m_spawnTimer += m_spawnInterval;
    }
    if (m_remainingToSpawn == 0) {
      m_state = State::WaveActive;
    }
    break;
  }

  case State::WaveActive: {
    if (aliveEnemyCount(entities) == 0) {
      // Wave cleared — start intermission.
      m_state = State::Intermission;
      m_intermissionTimer = Config::WAVE_INTERMISSION;
    }
    break;
  }

  case State::Intermission: {
    m_intermissionTimer -= dt;
    if (m_intermissionTimer <= 0.0f) {
      ++m_waveIndex;
      startWave(m_waveIndex);
    }
    break;
  }
  }
}
