#pragma once

#include "entity/Entity.hpp"

// ====================================================================
// WaveDef — declarative description of a single wave's enemy load-out.
//
// v1: a single enemy type + count + spawn cadence. When the full
// enemy roster lands (5d) this struct grows to a vector of (type,
// count) pairs so a wave can mix Fighters with Bombers etc.
// ====================================================================

struct WaveDef {
  EntityType enemyType = EntityType::Fighter;
  int count = 3;             // total enemies to spawn this wave
  float spawnInterval = 1.5f; // seconds between consecutive spawns
};
