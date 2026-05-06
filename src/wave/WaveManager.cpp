#include "WaveManager.hpp"

#include "core/Config.hpp"
#include "entity/EntityManager.hpp"
#include "entity/Player.hpp"
#include "raymath.h"
#include <cmath>

// ====================================================================
// Wave table — v1 escalation. Past the last entry, the manager
// extrapolates linearly (count += 4 per wave, interval -= 0.05 per
// wave clamped to 0.4 minimum). Player breathes in WAVE_INTERMISSION
// seconds between waves.
// ====================================================================
namespace {

constexpr WaveDef WAVE_TABLE[] = {
    {EntityType::Fighter, 3, 1.5f},
    {EntityType::Fighter, 5, 1.2f},
    {EntityType::Fighter, 7, 1.0f},
    {EntityType::Fighter, 10, 0.8f},
    {EntityType::Fighter, 14, 0.6f},
    {EntityType::Fighter, 20, 0.5f},
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
                         const Player &player) {
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
      Vector3 spawnPos = pickSpawnLocation(player);
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
