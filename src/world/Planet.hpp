#pragma once

#include "Heightmap.hpp"
#include "TerrainChunk.hpp"
#include "core/Config.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ====================================================================
// Planet
// Owns the heightmap and the grid of TerrainChunks.
// Applies the curvature trick: far vertices are depressed to simulate
// a curved planetary surface.
// ====================================================================

class Planet {
public:
  Planet() = default;
  ~Planet() = default;

  // Progress callback receives a step label and a 0..1 fraction.
  // Called once before heightmap generation, after heightmap completes,
  // and after each terrain chunk is built.
  using ProgressCb = std::function<void(const char *, float)>;
  void generate(uint32_t seed = 0, ProgressCb progressCb = nullptr);
  void draw(Vector3 cameraPos) const;
  void unload();

  // Query world-space height at (x, z)
  float heightAt(float worldX, float worldZ) const;

  // Total world width/depth of the terrain
  float worldSize() const { return Config::WORLD_SIZE; }

  // Toroidal world: the copy of p (shifted by whole multiples of
  // worldSize) nearest to ref. World-fixed objects are drawn here so
  // they tile alongside the terrain around the unwrapped camera.
  Vector3 nearestCopy(Vector3 p, Vector3 ref) const {
    const float ws = worldSize();
    p.x -= roundf((p.x - ref.x) / ws) * ws;
    p.z -= roundf((p.z - ref.z) / ws) * ws;
    return p;
  }

  // Flatten a square landing area centred on (worldX, worldZ) to its mean
  // height, blend the edges back into the terrain, and rebuild the
  // affected chunks. Returns the flattened world height. Load-time only
  // (re-uploads chunk meshes).
  float flattenPad(float worldX, float worldZ, float halfSize, float blend);

  // True when (x, z) is open water: ocean below sea level, or a lake.
  bool isWater(float worldX, float worldZ) const;

  // True when (x, z) is in the green grassland biome band.
  bool isGrassland(float worldX, float worldZ) const;

  // Dev introspection — write <stem>.png and <stem>.txt with the
  // current heightmap as a greyscale image plus stats/histogram/ASCII
  // preview. F6 in DEV_MODE calls this.
  void exportHeightmap(const std::string &pathStem) const;

private:
  Heightmap m_heightmap;
  std::vector<TerrainChunk> m_chunks;
  bool m_ready = false;
};