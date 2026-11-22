#pragma once

#include "Heightmap.hpp"
#include "core/Config.hpp"
#include "raylib.h"

class FieldMap;
class RoadNetwork;

// ====================================================================
// TerrainChunk
// Flat-shaded mesh built from a heightmap region.
// Reads WaterType map to colour ocean / lake / river distinctly.
// ====================================================================

class TerrainChunk {
public:
  TerrainChunk() = default;
  ~TerrainChunk() = default;

  // fields (optional) tints farmland in the grass band; roads (optional)
  // are cut into the terrain as their own coloured pieces.
  void build(const Heightmap &hmap, const FieldMap *fields, const RoadNetwork *roads,
             int originX, int originZ, int cellsPerEdge);
  // Draw at a world-space offset. Planet picks the tile offset closest to
  // the camera so the toroidal world appears infinite in every direction.
  void draw(Vector3 offset) const;
  // Draw with this shader and its texture0 (the infection overlay, R.8a).
  void setOverlay(Shader shader, Texture2D texture) {
    if (!m_built) return;
    m_model.materials[0].shader = shader;
    m_model.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = texture;
  }
  void unload();

  bool isBuilt() const { return m_built; }
  Vector3 centre() const { return m_centre; }

private:
  // field >= 0: farmland tint index, used in the grass band only.
  Color landColor(float h, int fieldTint = -1) const;
  Color waterColor(WaterType wt) const;

  Mesh m_mesh = {};
  Model m_model = {};
  Vector3 m_centre = {};
  bool m_built = false;
};