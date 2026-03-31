#pragma once

#include "Heightmap.hpp"
#include "TerrainChunk.hpp"
#include "core/Config.hpp"
#include <cstdint>
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

  void generate(uint32_t seed = 0);
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