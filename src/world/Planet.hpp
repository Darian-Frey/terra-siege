#pragma once

#include "Heightmap.hpp"
#include "TerrainChunk.hpp"
#include "core/Config.hpp"
#include <cstdint>
#include <functional>
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
  float worldSize() const {
    return static_cast<float>(Config::HEIGHTMAP_SIZE - 1) *
           Config::TERRAIN_SCALE;
  }

private:
  Heightmap m_heightmap;
  std::vector<TerrainChunk> m_chunks;
  bool m_ready = false;
};