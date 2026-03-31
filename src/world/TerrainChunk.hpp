#pragma once

#include "Heightmap.hpp"
#include "core/Config.hpp"
#include "raylib.h"

// ====================================================================
// TerrainChunk
// A single NxN quad mesh built from a region of the heightmap.
// Flat-shaded by duplicating vertices per triangle.
// Owns its raylib Mesh and Model — call unload() before destruction.
// ====================================================================

class TerrainChunk {
public:
  TerrainChunk() = default;
  ~TerrainChunk() = default;

  // Build mesh from heightmap region.
  // originX/Z are heightmap cell coordinates of the chunk's top-left corner.
  void build(const Heightmap &hmap, int originX, int originZ, int cellsPerEdge);

  void draw(Vector3 cameraPos) const;
  void unload();

  bool isBuilt() const { return m_built; }
  Vector3 centre() const { return m_centre; }

private:
  Color heightToColor(float h) const;

  Mesh m_mesh = {};
  Model m_model = {};
  Vector3 m_centre = {};
  bool m_built = false;
};