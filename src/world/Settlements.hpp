#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Heightmap;

// ====================================================================
// Settlement sites (Settlements track R.1,
// project-status/ROAD_VILLAGE_TOWN_ROADMAP.md).
//
// Picks where towns, villages and farmsteads go, once per world, right
// after the heightmap. Later phases route roads between the sites and
// lay out buildings on them; for now they are data, shown by the DEV
// overlay (F8) and marked on the F6 map dump.
//
// Each site needs gentle, dry grassland over its whole footprint and
// keeps a minimum distance from other sites (wrap-aware). Candidates on
// a coarse grid are scored, then taken greedily best-first: towns,
// then villages (preferring the land between towns), then farmsteads.
// Deterministic per seed.
// ====================================================================
struct SettlementSite {
  enum class Kind : uint8_t { Town, Village, Farm };
  Kind kind = Kind::Farm;
  float x = 0.0f, z = 0.0f; // world position of the centre, in [0, WORLD_SIZE)
  float y = 0.0f;           // terrain height at the centre
  float radius = 0.0f;      // footprint radius, m
};

class Settlements {
public:
  void generate(const Heightmap &hmap, uint32_t seed);

  const std::vector<SettlementSite> &sites() const { return m_sites; }
  int count(SettlementSite::Kind k) const;

  // True when (x, z) lies within `margin` of any site's footprint
  // (wrap-aware). Bases and other placement use it to keep clear.
  bool occupied(float x, float z, float margin) const;
  // As occupied(), but towns and villages only (farmsteads sit in fields).
  bool inTownOrVillage(float x, float z, float margin) const;

  // Top-down map: biome colours with every site circled (red towns,
  // yellow villages, cyan farmsteads). Written with the F6 dump.
  void exportMap(const Heightmap &hmap, const std::string &path) const;

private:
  std::vector<SettlementSite> m_sites;
};
