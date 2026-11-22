#pragma once

#include "Decorations.hpp"

#include <cstdint>
#include <vector>

class Planet;

// ====================================================================
// Village and farmstead layout (Settlements track R.5, F-049).
//
// Runs first in the decoration pass. Lays out buildings on every
// village and farmstead site and records their footprints, so forests,
// hedges and the rest keep clear of them (blocked()).
//
//   * Villages: a church or chapel and a war memorial at the core, then
//     houses, cottages and terraces fronting both sides of the roads
//     through the village (a pub, a shop and a hall near the core), one
//     front-garden boundary style per village, and a few farm buildings
//     on the fringe.
//   * Farmsteads: a farmhouse facing the track, a barn and an
//     outbuilding, plus a mix of extras (Dutch barn, silo, hay, tractor,
//     glasshouse, the odd windmill).
//
// Buildings face the nearest road and stay level: a footprint must be
// dry, off the roads, clear of other buildings and within
// BUILDING_MAX_RISE of level (the 2 m foundations cover the rest).
// ====================================================================
class SettlementLayout {
public:
  using BM = Decoration::BuildingModel;
  // Oriented footprint; (x, z) is the footprint centre in world
  // coordinates (may run past the world edge), hw across the front and
  // hd front to back, yaw as for the model.
  struct Footprint {
    float x, z, yaw, hw, hd;
  };

  // Lay out every village and farmstead; appends Building decorations.
  void build(const Planet &planet, uint32_t seed, std::vector<Decoration> &out);

  // True when (x, z) is within `margin` of any building footprint.
  bool blocked(float x, float z, float margin) const;

  int buildingCount() const { return static_cast<int>(m_prints.size()); }

  // Footprint of a model placed at (x, z) with yaw (and, for boundary
  // segments, stretch along its length).
  static Footprint footprintOf(BM model, float x, float z, float yaw, float stretch = 1.0f);

private:
  void index(const Footprint &f);
  bool overlaps(const Footprint &f, float margin) const;

  std::vector<Footprint> m_prints;
  int m_gridN = 0;
  std::vector<std::vector<int>> m_grid; // footprint ids per BUILDING_INDEX_CELL
};
