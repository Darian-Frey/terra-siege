#pragma once

#include "FieldMap.hpp"

#include <cstdint>
#include <functional>
#include <vector>

class Heightmap;
class Settlements;

// ====================================================================
// Road network (Settlements track R.2, F-048,
// project-status/ROAD_VILLAGE_TOWN_ROADMAP.md).
//
// Routed once per world, after the settlement sites. Roads are found
// with A* on a ROAD_GRID_STEP grid over the heightmap: steep and high
// ground costs more, water (with ROAD_WATER_CLEARANCE) is impassable,
// and existing road is cheaper so routes merge. The grid wraps with
// the world.
//
//   * Main roads join the towns: a minimum spanning tree plus a few
//     extra links for loops.
//   * Lanes join each village to the nearest connected town or village,
//     stopping where they meet existing road.
//   * Tracks join each farmstead to the nearest road.
//   * Streets (R.7): a rough grid round each town centre, aligned with
//     the main road, plus a paved square at the centre.
//
// Grid paths are smoothed into polylines in unwrapped coordinates (a
// road may run past the world edge). For drawing, each road becomes
// convex pieces (a quad per segment, an octagon per bend) that
// TerrainChunk clips terrain triangles against, so roads are part of
// the terrain mesh and can't z-fight.
// ====================================================================
class RoadNetwork {
public:
  using Point = FieldMap::Point;
  // Street and Square are town streets (R.7): a rough grid round each
  // town centre and a paved square (a single-point road, drawn as a disc).
  enum class Class : uint8_t { Main, Lane, Track, Street, Square };
  static float halfWidthOf(Class c);
  struct Road {
    Class cls;
    std::vector<Point> pts; // centre line, unwrapped world coordinates
  };
  using ProgressCb = std::function<void(const char *, float)>;

  void generate(const Heightmap &hmap, const Settlements &settlements, uint32_t seed,
                const ProgressCb &progress = {});

  const std::vector<Road> &roads() const { return m_roads; }
  // Settlements the network could not reach (on islands; bridges are R.6).
  int unconnected() const { return m_unconnected; }
  float length(Class c) const;

  // Split a convex polygon (world coordinates inside [0, WORLD_SIZE]²)
  // into pieces on a road (cls >= 0, the widest class wins) and off it
  // (cls = -1), appended to out. Load-time only (allocates).
  struct Piece {
    int cls;
    std::vector<Point> poly;
  };
  void classify(const std::vector<Point> &poly, std::vector<Piece> &out) const;

  // Distance from (x, z) to the nearest road edge (negative inside a
  // road), looking no further than maxDist; returns maxDist when no road
  // is that close. Wrap-aware.
  float edgeDistance(float x, float z, float maxDist) const;
  // Nearest point on any road centre line within maxDist of (x, z), in
  // the same frame as the query, with the road's unit direction there
  // and its half-width. False when no road is that close.
  bool nearestPoint(float x, float z, float maxDist, Point &p, Point &dir, float &halfWidth) const;

private:
  struct Shape {
    int cls;
    std::vector<Point> poly; // convex, counter-clockwise in (x, z)
    float x0, z0, x1, z1;    // bounds
  };
  void buildShapes();
  void addShape(int cls, std::vector<Point> poly);
  struct Segment {
    Point a, b;
    float halfWidth;
  };
  void addSegment(Segment s);

  std::vector<Road> m_roads;
  int m_unconnected = 0;
  std::vector<Shape> m_shapes;
  // Spatial index over shapes: ROAD_INDEX_CELL buckets covering the world
  // square (shapes past the edge are added again, shifted by WORLD_SIZE).
  int m_indexN = 0;
  std::vector<std::vector<int>> m_index;
  std::vector<Segment> m_segments;           // folded into the world square
  std::vector<std::vector<int>> m_segIndex;  // same cells as m_index
  mutable std::vector<uint32_t> m_stamp;
  mutable uint32_t m_stampGen = 0;
};
