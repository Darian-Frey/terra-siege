#pragma once

#include "WaveDef.hpp"
#include "raylib.h"
#include <cstdint>

class EntityManager;
class Player;

// ====================================================================
// WaveManager — drives staggered enemy spawning and wave escalation.
//
// State machine:
//   FirstDelay  : at world start, breathe before wave 1 begins
//   Spawning    : actively spawning this wave's enemies on a timer
//   WaveActive  : all enemies spawned, waiting for them to be cleared
//   Intermission: wave cleared, breathing room before next wave
//
// v1 spawns a single EntityType (Fighter) per wave with escalating
// counts and tightening spawn cadence. When the predefined table
// runs out, additional waves grow linearly (count += 4 per wave).
//
// Spawn placement: random point in a ring around the player at a
// modest altitude above the ship — staggered so enemies don't all
// arrive from the same direction at once.
// ====================================================================

class WaveManager {
public:
  enum class State : uint8_t {
    FirstDelay,
    Spawning,
    WaveActive,
    Intermission,
  };

  // Reset to the start of wave 1 with the first-delay timer.
  void reset();

  // Per-tick: advance state machine, spawn pending enemies, detect
  // wave-clear when all spawned enemies are dead.
  void update(float dt, EntityManager &entities, const Player &player);

  // HUD readouts
  int currentWave() const { return m_waveIndex + 1; } // 1-based
  int remainingToSpawn() const { return m_remainingToSpawn; }
  int aliveEnemyCount(const EntityManager &entities) const;
  State state() const { return m_state; }
  float intermissionTimer() const { return m_intermissionTimer; }
  float firstDelayTimer() const { return m_firstDelayTimer; }

private:
  // Resolve a wave definition for the given index. Past the table,
  // each wave grows by 4 enemies and shaves spawn interval slightly.
  WaveDef getWaveDef(int waveIdx) const;

  // Begin the wave at waveIdx — sets remainingToSpawn and timer.
  void startWave(int waveIdx);

  // Pick a random point in the ring around the player.
  Vector3 pickSpawnLocation(const Player &player);

  State m_state = State::FirstDelay;
  int m_waveIndex = 0; // 0-based index into the wave table
  int m_remainingToSpawn = 0;
  float m_spawnTimer = 0.0f;
  float m_spawnInterval = 1.5f;
  float m_intermissionTimer = 0.0f;
  float m_firstDelayTimer = 0.0f;

  // xorshift32 RNG — deterministic per-launch, doesn't depend on world
  // generation seed so the spawn pattern is independent of terrain.
  uint32_t m_rng = 0xACE1u;
  float randF(float lo, float hi);
};
