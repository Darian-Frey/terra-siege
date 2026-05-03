#pragma once

#include "Heightmap.hpp"
#include "core/Config.hpp"
#include "raylib.h"

// ====================================================================
// TerrainChunk
// Flat-shaded mesh built from a heightmap region.
// Reads WaterType map to colour ocean / lake / river distinctly.
// ====================================================================

class TerrainChunk {
public:
  TerrainChunk() = default;
  ~TerrainChunk() = default;

  void build(const Heightmap &hmap, int originX, int originZ, int cellsPerEdge);
  // Draw at a world-space offset. Planet picks the tile offset closest to
  // the camera so the toroidal world appears infinite in every direction.
  void draw(Vector3 offset) const;
  void unload();

  bool isBuilt() const { return m_built; }
  Vector3 centre() const { return m_centre; }

private:
  Color landColor(float h) const;
  Color waterColor(WaterType wt) const;

  Mesh m_mesh = {};
  Model m_model = {};
  Vector3 m_centre = {};
  bool m_built = false;
};