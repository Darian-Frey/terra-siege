#include "Planet.hpp"
#include <cmath>

void Planet::generate(uint32_t seed, ProgressCb cb) {
  if (m_ready)
    unload();

  // ---- Heightmap phase (0% → 40%) ----
  if (cb) cb("Generating heightmap", 0.0f);
  m_heightmap.generate(Config::HEIGHTMAP_SIZE, seed);
  if (cb) cb("Heightmap complete, meshing terrain", 0.4f);

  // ---- Chunk phase (40% → 100%) ----
  const int chunksPerEdge = Config::CHUNK_COUNT;
  const int cellsPerChunk = (Config::HEIGHTMAP_SIZE - 1) / chunksPerEdge;
  const int totalChunks = chunksPerEdge * chunksPerEdge;

  m_chunks.resize(static_cast<size_t>(totalChunks));

  int done = 0;
  for (int cz = 0; cz < chunksPerEdge; ++cz) {
    for (int cx = 0; cx < chunksPerEdge; ++cx) {
      int originX = cx * cellsPerChunk;
      int originZ = cz * cellsPerChunk;
      int idx = cz * chunksPerEdge + cx;
      m_chunks[static_cast<size_t>(idx)].build(m_heightmap, originX, originZ,
                                               cellsPerChunk);
      ++done;
      if (cb)
        cb("Building terrain chunks",
           0.4f + 0.6f * static_cast<float>(done) /
                      static_cast<float>(totalChunks));
    }
  }

  if (cb) cb("Complete", 1.0f);
  m_ready = true;
}

void Planet::draw(Vector3 cameraPos) const {
  if (!m_ready)
    return;

  // Toroidal world: render each chunk at the tile-offset (multiple of
  // worldSize) closest to the camera. Each chunk is drawn exactly once,
  // but always positioned in whichever copy of the world is most useful
  // — so the player and camera see seamless terrain in every direction.
  // Player coordinates are not wrapped; this keeps the camera follow lerp
  // continuous and avoids the visible "edge of map" entirely.
  const float ws = worldSize();
  for (const auto &chunk : m_chunks) {
    Vector3 c = chunk.centre();
    float shiftX = roundf((cameraPos.x - c.x) / ws) * ws;
    float shiftZ = roundf((cameraPos.z - c.z) / ws) * ws;
    chunk.draw({shiftX, 0.0f, shiftZ});
  }
}

void Planet::unload() {
  for (auto &chunk : m_chunks)
    chunk.unload();
  m_chunks.clear();
  m_ready = false;
}

float Planet::heightAt(float worldX, float worldZ) const {
  // Wrap to toroidal world bounds. Heightmap::sample() also wraps, but
  // wrapping at the world-coord layer keeps callers (camera clamp,
  // terrain object placement) safe to query any coordinate.
  const float ws = worldSize();
  worldX = fmodf(worldX, ws);
  if (worldX < 0.0f) worldX += ws;
  worldZ = fmodf(worldZ, ws);
  if (worldZ < 0.0f) worldZ += ws;

  float hmX = worldX / Config::TERRAIN_SCALE;
  float hmZ = worldZ / Config::TERRAIN_SCALE;
  return m_heightmap.sample(hmX, hmZ) * Config::TERRAIN_HEIGHT_MAX;
}