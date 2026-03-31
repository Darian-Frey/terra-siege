#include "TerrainChunk.hpp"
#include <cmath>
#include <cstring>

// ----------------------------------------------------------------
// Height-to-colour bands (h is normalised 0..1)
// ----------------------------------------------------------------
Color TerrainChunk::heightToColor(float h) const {
  if (h <= Config::SEA_LEVEL + 0.01f)
    return {30, 80, 180, 255}; // ocean
  else if (h < 0.28f)
    return {55, 120, 200, 255}; // shallow water
  else if (h < 0.35f)
    return {210, 190, 120, 255}; // sand / beach
  else if (h < 0.58f)
    return {100, 155, 65, 255}; // grassland
  else if (h < 0.72f)
    return {145, 125, 95, 255}; // rock
  else if (h < 0.86f)
    return {120, 115, 115, 255}; // high rock
  else
    return {240, 245, 255, 255}; // snow
}

// ----------------------------------------------------------------
// Directional light — sun at (0.57, 0.74, 0.36) normalised
// ----------------------------------------------------------------
static Color applyLight(Color base, Vector3 normal) {
  const float sx = 0.5700f, sy = 0.7400f, sz = 0.3600f;
  float diff = sx * normal.x + sy * normal.y + sz * normal.z;
  if (diff < 0.0f)
    diff = 0.0f;

  const float ambient = 0.45f;
  float light = ambient + (1.0f - ambient) * diff;

  auto clampU = [](float v) -> unsigned char {
    int i = static_cast<int>(v);
    return static_cast<unsigned char>(i < 0 ? 0 : i > 255 ? 255 : i);
  };
  return {clampU(base.r * light), clampU(base.g * light),
          clampU(base.b * light), 255};
}

// ----------------------------------------------------------------
// build
// ----------------------------------------------------------------
void TerrainChunk::build(const Heightmap &hmap, int originX, int originZ,
                         int cellsPerEdge) {
  if (m_built)
    unload();

  const float S = Config::TERRAIN_SCALE;
  const float Hm = Config::TERRAIN_HEIGHT_MAX;
  const float seaWorld = Config::SEA_LEVEL * Hm; // sea level in world units
  const int N = cellsPerEdge;

  const int vertCount = N * N * 6;

  m_mesh.vertexCount = vertCount;
  m_mesh.triangleCount = N * N * 2;

  m_mesh.vertices =
      static_cast<float *>(RL_MALLOC(vertCount * 3 * sizeof(float)));
  m_mesh.colors = static_cast<unsigned char *>(
      RL_MALLOC(vertCount * 4 * sizeof(unsigned char)));
  m_mesh.normals =
      static_cast<float *>(RL_MALLOC(vertCount * 3 * sizeof(float)));

  int vi = 0, ci = 0, ni = 0;
  float cx = 0.0f, cz = 0.0f;

  // Helper: clamp raw height to sea level so ocean is a flat surface
  auto worldH = [&](int x, int z) -> float {
    float h = hmap.get(x, z) * Hm;
    return h < seaWorld ? seaWorld : h;
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

      // Triangle 1: v00, v01, v10  (CCW from above — normal points up)
      Vector3 n1 = faceNormal(v00, v01, v10);
      float a1 = (h00 + h10 + h01) / 3.0f / Hm;
      Color c1 = applyLight(heightToColor(a1), n1);

      // Triangle 2: v10, v01, v11  (CCW from above — normal points up)
      Vector3 n2 = faceNormal(v10, v01, v11);
      float a2 = (h10 + h11 + h01) / 3.0f / Hm;
      Color c2 = applyLight(heightToColor(a2), n2);

      auto writeVert = [&](Vector3 v, Vector3 n, Color col) {
        m_mesh.vertices[vi] = v.x;
        m_mesh.vertices[vi + 1] = v.y;
        m_mesh.vertices[vi + 2] = v.z;
        m_mesh.normals[ni] = n.x;
        m_mesh.normals[ni + 1] = n.y;
        m_mesh.normals[ni + 2] = n.z;
        m_mesh.colors[ci] = col.r;
        m_mesh.colors[ci + 1] = col.g;
        m_mesh.colors[ci + 2] = col.b;
        m_mesh.colors[ci + 3] = col.a;
        vi += 3;
        ni += 3;
        ci += 4;
      };

      writeVert(v00, n1, c1);
      writeVert(v01, n1, c1);
      writeVert(v10, n1, c1);

      writeVert(v10, n2, c2);
      writeVert(v01, n2, c2);
      writeVert(v11, n2, c2);

      cx += wx + S * 0.5f;
      cz += wz + S * 0.5f;
    }
  }

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

// ----------------------------------------------------------------
// draw / unload
// ----------------------------------------------------------------
void TerrainChunk::draw(Vector3 /*cameraPos*/) const {
  if (!m_built)
    return;
  DrawModel(m_model, {0, 0, 0}, 1.0f, WHITE);
}

void TerrainChunk::unload() {
  if (m_built) {
    UnloadModel(m_model);
    m_built = false;
  }
}