#include "Planet.hpp"
#include <cmath>
#include <string>

namespace {
// Terrain: baked vertex colours (lighting included), blended towards
// alien purple by the ground infection, sampled once per triangle at
// its provoking vertex (flat) so the flat-shaded look holds.
const char *kTerrainVs = R"(#version 330
in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform sampler2D texture0;
out vec4 fragColor;
flat out float infect;
void main() {
  vec4 wp = matModel * vec4(vertexPosition, 1.0);
  infect = textureLod(texture0, wp.xz / WORLD_SIZE, 0.0).r;
  fragColor = vertexColor;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
const char *kTerrainFs = R"(#version 330
in vec4 fragColor;
flat in float infect;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
  float lum = dot(fragColor.rgb, vec3(0.3, 0.59, 0.11));
  vec3 alien = vec3(0.42, 0.12, 0.50) * (0.55 + 0.9 * lum);
  finalColor = vec4(mix(fragColor.rgb, alien, infect * 0.85), fragColor.a) * colDiffuse;
}
)";
} // namespace

void Planet::generate(uint32_t seed, ProgressCb cb) {
  if (m_ready)
    unload();

  // ---- Heightmap phase (0% → 40%) ----
  if (cb) cb("Generating heightmap", 0.0f);
  m_heightmap.generate(Config::HEIGHTMAP_SIZE, seed);
  // ---- Settlements and roads (0.40 → 0.50) ----
  if (cb) cb("Choosing settlement sites", 0.4f);
  m_settlements.generate(m_heightmap, seed);
  m_roads.generate(m_heightmap, m_settlements, seed, [&](const char *step, float p) {
    if (cb) cb(step, 0.4f + 0.1f * p);
  });
  m_fields.generate(m_heightmap, seed, &m_roads, &m_settlements);

  // ---- Chunk phase (50% → 100%) ----
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
      m_chunks[static_cast<size_t>(idx)].build(m_heightmap, &m_fields, &m_roads,
                                               originX, originZ, cellsPerChunk);
      ++done;
      if (cb)
        cb("Building terrain chunks",
           0.5f + 0.5f * static_cast<float>(done) /
                      static_cast<float>(totalChunks));
    }
  }

  // Infection overlay: a clean field and the terrain shader on every chunk.
  if (!m_infection.ready()) m_infection.init();
  else m_infection.clear();
  if (m_terrainShader.id == 0) {
    std::string vs = kTerrainVs;
    const std::string key = "WORLD_SIZE";
    vs.replace(vs.find(key), key.size(), std::to_string(Config::WORLD_SIZE));
    m_terrainShader = LoadShaderFromMemory(vs.c_str(), kTerrainFs);
  }
  applyOverlay();

  if (cb) cb("Complete", 1.0f);
  m_ready = true;
}

void Planet::applyOverlay() {
  if (m_terrainShader.id == 0 || !m_infection.ready()) return;
  for (auto &chunk : m_chunks) chunk.setOverlay(m_terrainShader, m_infection.texture());
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
          m_heightmap, &m_fields, &m_roads, wx * cellsPerChunk,
          wz * cellsPerChunk, cellsPerChunk);
    }
  }
  applyOverlay(); // rebuilt chunks get fresh materials
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
  m_infection.unload();
  if (m_terrainShader.id != 0) UnloadShader(m_terrainShader);
  m_terrainShader = {};
  m_ready = false;
}

void Planet::exportHeightmap(const std::string &pathStem) const {
  if (!m_ready) return;
  m_heightmap.exportPng(pathStem + ".png");
  m_heightmap.exportStats(pathStem + ".txt");
  m_settlements.exportMap(m_heightmap, pathStem + "-settlements.png");
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