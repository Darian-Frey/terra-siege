#include "TerrainChunk.hpp"
#include "FieldMap.hpp"
#include "Roads.hpp"
#include <cmath>
#include <cstring>
#include <vector>

// ================================================================
// Colour helpers
// ================================================================

// Farmland tints (FieldMap::tint): meadow, two greens, young crop,
// ripe wheat and ploughed earth. Kept close to the grass green so the
// patchwork reads as fields, not paint.
static constexpr Color kFieldTints[] = {
    {100, 155, 65, 255}, {84, 140, 56, 255},  {118, 166, 70, 255},
    {140, 165, 72, 255}, {178, 160, 84, 255}, {128, 104, 70, 255}};
static_assert(sizeof(kFieldTints) / sizeof(kFieldTints[0]) == FieldMap::kTintCount);
// Road surfaces by RoadNetwork::Class: asphalt main roads, lighter
// tarmac lanes, gravel farm tracks.
static constexpr Color kRoadColours[] = {
    {72, 72, 76, 255}, {108, 106, 102, 255}, {150, 132, 100, 255},
    {84, 84, 88, 255},    // town street
    {156, 148, 136, 255}  // paved square
};

Color TerrainChunk::landColor(float h, int fieldTint) const {
  // h is normalised [0,1]. Remap land portion (above SEA_LEVEL) to [0,1]
  // so colour bands track sea-level changes without re-tuning thresholds.
  float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
  if (landH < 0.0f) landH = 0.0f;

  if (landH < Config::BIOME_SAND_MAX)
    return {210, 190, 120, 255}; // sand / beach
  else if (landH < Config::BIOME_GRASS_MAX)
    return fieldTint >= 0 ? kFieldTints[fieldTint]
                          : Color{100, 155, 65, 255}; // grassland
  else if (landH < Config::BIOME_ROCK_MAX)
    return {145, 125, 95, 255}; // rock
  else if (landH < Config::BIOME_HIGH_MAX)
    return {120, 115, 115, 255}; // high rock
  else
    return {240, 245, 255, 255}; // snow
}

Color TerrainChunk::waterColor(WaterType wt) const {
  switch (wt) {
  case WaterType::Ocean:
    return {25, 70, 170, 255}; // deep blue
  case WaterType::Lake:
    return {40, 110, 185, 255}; // calmer mid-blue
  case WaterType::River:
    return {65, 145, 200, 255}; // lighter, flowing blue
  default:
    return {25, 70, 170, 255};
  }
}

// ================================================================
// Directional lighting
// Sun direction normalised: (0.57, 0.74, 0.36)
// ================================================================
static Color applyLight(Color base, Vector3 normal, bool isWater) {
  const float sx = 0.57f, sy = 0.74f, sz = 0.36f;
  float diff = sx * normal.x + sy * normal.y + sz * normal.z;
  if (diff < 0.0f)
    diff = 0.0f;

  // Water surfaces are flatter so they get a stronger specular-like boost
  float ambient = isWater ? 0.60f : 0.45f;
  float light = ambient + (1.0f - ambient) * diff;

  auto cu = [](float v) -> unsigned char {
    int i = static_cast<int>(v);
    return static_cast<unsigned char>(i < 0 ? 0 : i > 255 ? 255 : i);
  };
  return {cu(base.r * light), cu(base.g * light), cu(base.b * light), 255};
}

// ================================================================
// build
// ================================================================
void TerrainChunk::build(const Heightmap &hmap, const FieldMap *fields,
                         const RoadNetwork *roads, int originX, int originZ,
                         int cellsPerEdge) {
  if (m_built)
    unload();

  const float S = Config::TERRAIN_SCALE;
  const float Hm = Config::TERRAIN_HEIGHT_MAX;
  const float seaWorld = Config::SEA_LEVEL * Hm;
  const int N = cellsPerEdge;

  // Two triangles per quad, plus extra ones where field edges split a
  // grass triangle; collected here, then copied into the mesh.
  std::vector<float> verts, norms;
  std::vector<unsigned char> cols;
  verts.reserve(static_cast<size_t>(N) * N * 18);
  norms.reserve(static_cast<size_t>(N) * N * 18);
  cols.reserve(static_cast<size_t>(N) * N * 24);
  float cx = 0.0f, cz = 0.0f;
  std::vector<FieldMap::Piece> pieces;
  std::vector<RoadNetwork::Piece> roadPieces;

  // World height: clamp anything below sea level to flat ocean surface
  auto worldH = [&](int x, int z) -> float {
    float h = hmap.get(x, z) * Hm;
    return h < seaWorld ? seaWorld : h;
  };

  // Dominant water type for a quad face (any water wins over land)
  auto dominantWater = [&](int x0, int z0, int x1, int z1, int x2,
                           int z2) -> WaterType {
    WaterType w0 = hmap.waterAt(x0, z0);
    WaterType w1 = hmap.waterAt(x1, z1);
    WaterType w2 = hmap.waterAt(x2, z2);
    // Priority: River > Lake > Ocean > None
    if (w0 == WaterType::River || w1 == WaterType::River ||
        w2 == WaterType::River)
      return WaterType::River;
    if (w0 == WaterType::Lake || w1 == WaterType::Lake || w2 == WaterType::Lake)
      return WaterType::Lake;
    if (w0 == WaterType::Ocean || w1 == WaterType::Ocean ||
        w2 == WaterType::Ocean)
      return WaterType::Ocean;
    return WaterType::None;
  };

  for (int qz = 0; qz < N; ++qz) {
    for (int qx = 0; qx < N; ++qx) {
      int hx = originX + qx;
      int hz = originZ + qz;

      float h00 = worldH(hx, hz);
      float h10 = worldH(hx + 1, hz);
      float h01 = worldH(hx, hz + 1);
      float h11 = worldH(hx + 1, hz + 1);

      float wx = static_cast<float>(hx) * S;
      float wz = static_cast<float>(hz) * S;

      Vector3 v00 = {wx, h00, wz};
      Vector3 v10 = {wx + S, h10, wz};
      Vector3 v01 = {wx, h01, wz + S};
      Vector3 v11 = {wx + S, h11, wz + S};

      auto faceNormal = [](Vector3 a, Vector3 b, Vector3 c) -> Vector3 {
        Vector3 ea = {b.x - a.x, b.y - a.y, b.z - a.z};
        Vector3 eb = {c.x - a.x, c.y - a.y, c.z - a.z};
        Vector3 n = {ea.y * eb.z - ea.z * eb.y, ea.z * eb.x - ea.x * eb.z,
                     ea.x * eb.y - ea.y * eb.x};
        float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
          n.x /= len;
          n.y /= len;
          n.z /= len;
        }
        return n;
      };

      auto writeVert = [&](Vector3 v, Vector3 n, Color col) {
        verts.insert(verts.end(), {v.x, v.y, v.z});
        norms.insert(norms.end(), {n.x, n.y, n.z});
        cols.insert(cols.end(), {col.r, col.g, col.b, col.a});
      };
      // One terrain triangle (CCW from above). A grass triangle that
      // crosses field edges is split along them so each piece takes its
      // field's tint; the pieces stay in the triangle's plane.
      auto emitTri = [&](Vector3 a, Vector3 b, Vector3 c, WaterType wt) {
        Vector3 n = faceNormal(a, b, c);
        float avg = (a.y + b.y + c.y) / 3.0f / Hm;
        if (wt != WaterType::None) {
          Color col = applyLight(waterColor(wt), n, true);
          writeVert(a, n, col);
          writeVert(b, n, col);
          writeVert(c, n, col);
          return;
        }
        // Land: split along field edges (grass only), then along road
        // edges; each piece takes its road, field or biome colour. The
        // pieces stay in the triangle's plane.
        const float landH = (avg - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
        const bool grass = landH >= Config::BIOME_SAND_MAX && landH < Config::BIOME_GRASS_MAX;
        pieces.clear();
        if (fields && grass)
          fields->clip({{a.x, a.z}, {b.x, b.z}, {c.x, c.z}}, pieces);
        else
          pieces.push_back({-1, {{a.x, a.z}, {b.x, b.z}, {c.x, c.z}}});
        const float det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        auto onPlane = [&](FieldMap::Point p) -> Vector3 {
          float wa = ((b.z - c.z) * (p.x - c.x) + (c.x - b.x) * (p.z - c.z)) / det;
          float wb = ((c.z - a.z) * (p.x - c.x) + (a.x - c.x) * (p.z - c.z)) / det;
          return {p.x, wa * a.y + wb * b.y + (1.0f - wa - wb) * c.y, p.z};
        };
        auto emitPoly = [&](const std::vector<FieldMap::Point> &poly, Color col) {
          Vector3 p0 = onPlane(poly[0]);
          for (size_t k = 1; k + 1 < poly.size(); ++k) {
            writeVert(p0, n, col);
            writeVert(onPlane(poly[k]), n, col);
            writeVert(onPlane(poly[k + 1]), n, col);
          }
        };
        // Farmland tint at a piece's centre: the field's own tint, or
        // another one across a road from the field's centre.
        auto landAt = [&](int field, const std::vector<FieldMap::Point> &poly) {
          int tint = -1;
          if (field >= 0 && fields->isFarmland(field)) {
            float px = 0.0f, pz = 0.0f;
            for (const FieldMap::Point &q : poly) {
              px += q.x;
              pz += q.z;
            }
            tint = fields->tintAt(field, px / poly.size(), pz / poly.size());
          }
          return applyLight(landColor(avg, tint), n, false);
        };
        for (const FieldMap::Piece &pc : pieces) {
          if (!roads) {
            emitPoly(pc.poly, landAt(pc.field, pc.poly));
            continue;
          }
          roadPieces.clear();
          roads->classify(pc.poly, roadPieces);
          for (const RoadNetwork::Piece &rp : roadPieces)
            emitPoly(rp.poly, rp.cls >= 0 ? applyLight(kRoadColours[rp.cls], n, false)
                                          : landAt(pc.field, rp.poly));
        }
      };
      emitTri(v00, v01, v10, dominantWater(hx, hz, hx, hz + 1, hx + 1, hz));
      emitTri(v10, v01, v11, dominantWater(hx + 1, hz, hx, hz + 1, hx + 1, hz + 1));

      cx += wx + S * 0.5f;
      cz += wz + S * 0.5f;
    }
  }

  const int vertCount = static_cast<int>(verts.size() / 3);
  m_mesh.vertexCount = vertCount;
  m_mesh.triangleCount = vertCount / 3;
  m_mesh.vertices = static_cast<float *>(RL_MALLOC(verts.size() * sizeof(float)));
  m_mesh.normals = static_cast<float *>(RL_MALLOC(norms.size() * sizeof(float)));
  m_mesh.colors = static_cast<unsigned char *>(RL_MALLOC(cols.size()));
  std::memcpy(m_mesh.vertices, verts.data(), verts.size() * sizeof(float));
  std::memcpy(m_mesh.normals, norms.data(), norms.size() * sizeof(float));
  std::memcpy(m_mesh.colors, cols.data(), cols.size());

  float quads = static_cast<float>(N * N);
  float avgCY = 0.0f;
  for (int i = 1; i < vertCount * 3; i += 3)
    avgCY += m_mesh.vertices[i];
  avgCY /= static_cast<float>(vertCount);
  m_centre = {cx / quads, avgCY, cz / quads};

  UploadMesh(&m_mesh, false);
  m_model = LoadModelFromMesh(m_mesh);
  m_built = true;
}

// ================================================================
// draw / unload
// ================================================================
void TerrainChunk::draw(Vector3 offset) const {
  if (!m_built)
    return;
  DrawModel(m_model, offset, 1.0f, WHITE);
}

void TerrainChunk::unload() {
  if (m_built) {
    UnloadModel(m_model);
    // UnloadModel frees the GPU buffers and vertex arrays, but m_mesh is
    // a separate copy still holding the old ids/pointers. Clear both so
    // the next build() uploads a fresh mesh instead of raylib refusing
    // ("already loaded") and the model referencing freed memory.
    m_mesh = {};
    m_model = {};
    m_built = false;
  }
}