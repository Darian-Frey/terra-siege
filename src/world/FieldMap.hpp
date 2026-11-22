#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

class Heightmap;
class RoadNetwork;

// ====================================================================
// Farmland patchwork and forest mask, shared by terrain colouring and
// decoration placement so field tints, hedgerows and forest edges agree.
//
// Fields are polygons from a binary space partition of the world
// square: each piece is cut by a straight line at a random angle and
// position across its longest dimension until it is field-sized, giving
// irregular four- to six-sided fields. Some cuts carry a sine wave along
// their length, bowing or S-bending that boundary. The same tree clips
// terrain triangles along field edges, so tints meet on a clean line or
// curve at any angle. The world edge is just one more boundary, so the
// layout tiles the toroidal world.
//
// A farm-region noise splits the grassland into farming country and
// wild country. In farming country each gentle, dry field is either
// farmland (tinted and hedged) or, by chance, a wood that fills the
// field to its hedges. Wild country keeps the forest mask's natural
// woods and open meadow.
//
// Roads (R.3) split fields: the land between roads is flooded into
// regions on a FIELD_REGION_CELL raster, and a field crossed by a road
// takes a different tint on the far side (tintAt), so roads read as
// field edges.
// ====================================================================
class FieldMap {
public:
  static constexpr int kTintCount = 6;

  struct Point {
    float x, z;
  };
  // A convex piece of a clipped polygon and the field it lies in.
  struct Piece {
    int field;
    std::vector<Point> poly;
  };

  void generate(const Heightmap &hmap, uint32_t seed, const RoadNetwork *roads = nullptr);

  // Field containing a world position (wrapped into the world square).
  int fieldAt(float worldX, float worldZ) const;
  bool isFarmland(int field) const { return m_fields[field].farmland; }
  // A wood planted to the field's edges, inside farming country.
  bool isWood(int field) const { return m_fields[field].wood; }
  // 0..kTintCount-1; TerrainChunk owns the colours.
  int tint(int field) const { return m_fields[field].tint; }
  // Tint at a point of a field: the field's own tint on the side of any
  // road its centre is on, another tint across the road.
  int tintAt(int field, float worldX, float worldZ) const;
  // Road-bounded region at a point (the nearest one off the road when
  // the point is on a road's barrier band); -1 with no roads.
  int regionAt(float worldX, float worldZ) const;
  // Stable per-field hash, for hedge styles and similar choices.
  uint32_t hash(int field) const { return m_fields[field].hash; }

  int fieldCount() const { return static_cast<int>(m_fields.size()); }
  // Field outline in world coordinates inside [0, WORLD_SIZE]². Convex
  // unless a curved cut bows an edge; curves are sampled every few metres.
  const std::vector<Point> &outline(int field) const { return m_fields[field].poly; }

  // Split a convex polygon lying inside the world square into the pieces
  // that fall in each field, appended to out. Load-time only (allocates).
  void clip(const std::vector<Point> &poly, std::vector<Piece> &out) const;

  // Smooth forest-density noise in [0, 1) at a world position. Forest
  // where it exceeds Config::FOREST_NOISE_LOW.
  float forestMask(float worldX, float worldZ) const;
  // Slow noise in [0, 1) that picks the species of a forest stand, so
  // neighbouring trees and groves share it (~FOREST_STAND_PERIOD blobs).
  float standNoise(float worldX, float worldZ) const;

private:
  struct Node {
    // Inner node: points with side(p) >= 0 go to front, where
    // side(p) = n·p + d + amp * sin(pi * waves * u), u = (T·p - t0) / span
    // along the cut (T = (-nz, nx)), and the wave term is zero outside
    // 0 < u < 1. Over the cut's chord through its piece that is one
    // half-wave (a bow) or a full wave (an S-bend) fading to nothing at
    // both ends, so the curve meets the piece's edges where the straight
    // cut would. amp = 0 is a straight cut.
    float nx = 0, nz = 0, d = 0;
    float amp = 0, t0 = 0, invSpan = 0, waves = 1;
    int front = -1, back = -1;
    int field = -1; // leaf when >= 0
    float wave(float t) const {
      float u = (t - t0) * invSpan;
      return u > 0.0f && u < 1.0f ? amp * sinf(3.14159265f * waves * u) : 0.0f;
    }
    float side(Point p) const {
      float s = nx * p.x + nz * p.z + d;
      return amp == 0.0f ? s : s + wave(nx * p.z - nz * p.x);
    }
  };
  struct Field {
    bool farmland = false;
    bool wood = false;
    uint8_t tint = 0;
    uint32_t hash = 0;
    int homeRegion = -1;
    std::vector<Point> poly;
  };
  void clipNode(int node, const std::vector<Point> &poly,
                std::vector<Piece> &out) const;

  uint32_t m_forestSeed = 0;
  int m_regionN = 0;                // raster cells per world edge
  std::vector<uint16_t> m_region;   // 0xffff = road barrier
  void buildRegions(const RoadNetwork &roads);
  std::vector<Node> m_nodes;
  std::vector<Field> m_fields;
};
