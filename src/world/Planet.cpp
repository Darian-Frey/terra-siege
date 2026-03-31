#include "Planet.hpp"
#include <cmath>

void Planet::generate(uint32_t seed) {
  if (m_ready)
    unload();

  // Generate heightmap
  m_heightmap.generate(Config::HEIGHTMAP_SIZE, Config::TERRAIN_ROUGHNESS, seed);

  // Build chunks
  // CHUNK_COUNT x CHUNK_COUNT grid, each chunk covers
  // (HEIGHTMAP_SIZE-1)/CHUNK_COUNT cells per edge
  const int chunksPerEdge = Config::CHUNK_COUNT;
  const int cellsPerChunk = (Config::HEIGHTMAP_SIZE - 1) / chunksPerEdge;

  m_chunks.resize(static_cast<size_t>(chunksPerEdge * chunksPerEdge));

  for (int cz = 0; cz < chunksPerEdge; ++cz) {
    for (int cx = 0; cx < chunksPerEdge; ++cx) {
      int originX = cx * cellsPerChunk;
      int originZ = cz * cellsPerChunk;
      int idx = cz * chunksPerEdge + cx;
      m_chunks[static_cast<size_t>(idx)].build(m_heightmap, originX, originZ,
                                               cellsPerChunk);
    }
  }

  m_ready = true;
}

void Planet::draw(Vector3 cameraPos) const {
  if (!m_ready)
    return;
  for (const auto &chunk : m_chunks)
    chunk.draw(cameraPos);
}

void Planet::unload() {
  for (auto &chunk : m_chunks)
    chunk.unload();
  m_chunks.clear();
  m_ready = false;
}

float Planet::heightAt(float worldX, float worldZ) const {
  float hmX = worldX / Config::TERRAIN_SCALE;
  float hmZ = worldZ / Config::TERRAIN_SCALE;
  return m_heightmap.sample(hmX, hmZ) * Config::TERRAIN_HEIGHT_MAX;
}