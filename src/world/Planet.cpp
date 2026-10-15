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

float Planet::flattenPad(float worldX, float worldZ, float halfSize,
                         float blend) {
  // Mean height over the footprint keeps cut and fill balanced.
  float sum = 0.0f;
  int n = 0;
  for (int iz = -2; iz <= 2; ++iz)
    for (int ix = -2; ix <= 2; ++ix, ++n)
      sum += heightAt(worldX + ix * halfSize * 0.5f, worldZ + iz * halfSize * 0.5f);
  const float h = sum / static_cast<float>(n);

  const float s = Config::TERRAIN_SCALE;
  m_heightmap.flattenSquare(worldX / s, worldZ / s, halfSize / s, blend / s,
                            h / Config::TERRAIN_HEIGHT_MAX);

  // Rebuild every chunk the flattened area (plus a cell) touches.
  const int chunksPerEdge = Config::CHUNK_COUNT;
  const int cellsPerChunk = (Config::HEIGHTMAP_SIZE - 1) / chunksPerEdge;
  const float r = (halfSize + blend) / s + 1.0f;
  const int c0x = static_cast<int>(floorf((worldX / s - r) / cellsPerChunk));
  const int c1x = static_cast<int>(floorf((worldX / s + r) / cellsPerChunk));
  const int c0z = static_cast<int>(floorf((worldZ / s - r) / cellsPerChunk));
  const int c1z = static_cast<int>(floorf((worldZ / s + r) / cellsPerChunk));
  for (int cz = c0z; cz <= c1z; ++cz) {
    for (int cx = c0x; cx <= c1x; ++cx) {
      int wx = ((cx % chunksPerEdge) + chunksPerEdge) % chunksPerEdge;
      int wz = ((cz % chunksPerEdge) + chunksPerEdge) % chunksPerEdge;
      m_chunks[static_cast<size_t>(wz * chunksPerEdge + wx)].build(
          m_heightmap, wx * cellsPerChunk, wz * cellsPerChunk, cellsPerChunk);
    }
  }
  return h;
}

bool Planet::isWater(float worldX, float worldZ) const {
  if (heightAt(worldX, worldZ) < Config::SEA_LEVEL * Config::TERRAIN_HEIGHT_MAX)
    return true;
  const float s = Config::TERRAIN_SCALE;
  return m_heightmap.waterAt(static_cast<int>(roundf(worldX / s)),
                             static_cast<int>(roundf(worldZ / s))) !=
         WaterType::None;
}

bool Planet::isGrassland(float worldX, float worldZ) const {
  float h = heightAt(worldX, worldZ) / Config::TERRAIN_HEIGHT_MAX;
  float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
  return landH >= Config::BIOME_SAND_MAX && landH < Config::BIOME_GRASS_MAX;
}

void Planet::unload() {
  for (auto &chunk : m_chunks)
    chunk.unload();
  m_chunks.clear();
  m_ready = false;
}

void Planet::exportHeightmap(const std::string &pathStem) const {
  if (!m_ready) return;
  m_heightmap.exportPng(pathStem + ".png");
  m_heightmap.exportStats(pathStem + ".txt");
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