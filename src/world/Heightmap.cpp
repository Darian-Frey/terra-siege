#include "Heightmap.hpp"
#include "core/Config.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <queue>

// ================================================================
// RNG
// ================================================================
uint32_t Heightmap::nextRand() {
  m_rng ^= m_rng << 13;
  m_rng ^= m_rng >> 17;
  m_rng ^= m_rng << 5;
  return m_rng;
}

float Heightmap::randF(float lo, float hi) {
  float t = static_cast<float>(nextRand()) / static_cast<float>(0xFFFFFFFFu);
  return lo + t * (hi - lo);
}

// ================================================================
// generate — full pipeline
// ================================================================
void Heightmap::generate(int size, float roughness, uint32_t seed) {
  m_size = size;
  m_data.assign(static_cast<size_t>(size * size), 0.0f);
  m_water.assign(static_cast<size_t>(size * size), WaterType::None);
  m_rng = (seed == 0) ? static_cast<uint32_t>(std::time(nullptr)) : seed;

  int last = size - 1;
  set(0, 0, randF(0.3f, 0.7f));
  set(last, 0, randF(0.3f, 0.7f));
  set(0, last, randF(0.3f, 0.7f));
  set(last, last, randF(0.3f, 0.7f));

  diamondSquare(roughness);
  smooth(12);
  applyRadialFalloff();
  normalise();
  classifyOcean();
  carveRivers();
  floodLakes();
}

// ================================================================
// Diamond-Square
// ================================================================
void Heightmap::diamondSquare(float roughness) {
  int step = m_size - 1;
  float scale = 1.0f;

  while (step > 1) {
    int half = step / 2;

    for (int z = 0; z < m_size - 1; z += step)
      for (int x = 0; x < m_size - 1; x += step) {
        float avg = (get(x, z) + get(x + step, z) + get(x, z + step) +
                     get(x + step, z + step)) *
                    0.25f;
        set(x + half, z + half, avg + randF(-scale, scale));
      }

    for (int z = 0; z < m_size; z += half)
      for (int x = (z + half) % step; x < m_size; x += step) {
        float sum = 0.0f;
        int cnt = 0;
        if (z - half >= 0) {
          sum += get(x, z - half);
          ++cnt;
        }
        if (z + half < m_size) {
          sum += get(x, z + half);
          ++cnt;
        }
        if (x - half >= 0) {
          sum += get(x - half, z);
          ++cnt;
        }
        if (x + half < m_size) {
          sum += get(x + half, z);
          ++cnt;
        }
        set(x, z, (sum / static_cast<float>(cnt)) + randF(-scale, scale));
      }

    step = half;
    scale *= std::pow(2.0f, -roughness);
  }
}

// ================================================================
// Smooth — box blur
// ================================================================
void Heightmap::smooth(int passes) {
  std::vector<float> tmp(m_data.size());
  for (int p = 0; p < passes; ++p) {
    for (int z = 0; z < m_size; ++z)
      for (int x = 0; x < m_size; ++x) {
        float sum = 0.0f;
        int cnt = 0;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            int nx = x + dx, nz = z + dz;
            if (inBounds(nx, nz)) {
              sum += m_data[static_cast<size_t>(nz * m_size + nx)];
              ++cnt;
            }
          }
        tmp[static_cast<size_t>(z * m_size + x)] =
            sum / static_cast<float>(cnt);
      }
    m_data = tmp;
  }
}

// ================================================================
// Radial falloff — taper edges to ocean
// ================================================================
void Heightmap::applyRadialFalloff() {
  float centre = static_cast<float>(m_size - 1) * 0.5f;
  float maxR = centre * 0.82f;

  for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x) {
      float dx = static_cast<float>(x) - centre;
      float dz = static_cast<float>(z) - centre;
      float r = sqrtf(dx * dx + dz * dz);
      float factor = 1.0f;
      if (r > maxR) {
        float t = std::min((r - maxR) / (centre - maxR), 1.0f);
        factor = (cosf(t * 3.14159f) + 1.0f) * 0.5f;
      }
      m_data[static_cast<size_t>(z * m_size + x)] *= factor;
    }
}

// ================================================================
// Normalise
// ================================================================
void Heightmap::normalise() {
  float lo = 1e9f, hi = -1e9f;
  for (float v : m_data) {
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  float range = (hi - lo) > 1e-6f ? (hi - lo) : 1.0f;
  for (float &v : m_data)
    v = (v - lo) / range;
}

// ================================================================
// Classify ocean cells
// ================================================================
void Heightmap::classifyOcean() {
  for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
      if (get(x, z) < Config::SEA_LEVEL)
        waterRef(x, z) = WaterType::Ocean;
}

// ================================================================
// River carving
// ================================================================

// Walk downhill from (startX,startZ), recording path.
// Returns true if path reaches the ocean or map edge.
bool Heightmap::flowDownhill(int startX, int startZ,
                             std::vector<std::pair<int, int>> &path) {
  const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  const int dz8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

  int cx = startX, cz = startZ;
  int maxSteps = m_size * 4;

  std::vector<bool> visited(static_cast<size_t>(m_size * m_size), false);

  for (int step = 0; step < maxSteps; ++step) {
    path.push_back({cx, cz});
    visited[static_cast<size_t>(cz * m_size + cx)] = true;

    // Reached ocean or edge — success
    if (waterAt(cx, cz) == WaterType::Ocean || cx <= 0 || cx >= m_size - 1 ||
        cz <= 0 || cz >= m_size - 1)
      return true;

    // Find steepest descent neighbour
    float lowestH = get(cx, cz);
    int bestX = -1, bestZ = -1;
    for (int d = 0; d < 8; ++d) {
      int nx = cx + dx8[d], nz = cz + dz8[d];
      if (!inBounds(nx, nz))
        continue;
      if (visited[static_cast<size_t>(nz * m_size + nx)])
        continue;
      float h = get(nx, nz);
      if (h < lowestH) {
        lowestH = h;
        bestX = nx;
        bestZ = nz;
      }
    }

    if (bestX < 0)
      return false; // trapped in local minimum
    cx = bestX;
    cz = bestZ;
  }
  return false;
}

void Heightmap::carveChannel(const std::vector<std::pair<int, int>> &path) {
  const int W = Config::RIVER_WIDTH;
  const float depth = Config::RIVER_CARVE_DEPTH;

  for (auto &[px, pz] : path) {
    for (int dz = -W; dz <= W; ++dz)
      for (int dx = -W; dx <= W; ++dx) {
        int nx = px + dx, nz = pz + dz;
        if (!inBounds(nx, nz))
          continue;

        // Taper carve depth toward channel edges
        float dist = sqrtf(static_cast<float>(dx * dx + dz * dz));
        float taper = 1.0f - (dist / static_cast<float>(W + 1));
        if (taper < 0.0f)
          taper = 0.0f;

        float &h = m_data[static_cast<size_t>(nz * m_size + nx)];
        h -= depth * taper;
        if (h < 0.0f)
          h = 0.0f;

        // Mark as river if above ocean level, else ocean takes over
        if (h >= Config::SEA_LEVEL)
          waterRef(nx, nz) = WaterType::River;
        else
          waterRef(nx, nz) = WaterType::Ocean;
      }
  }
}

void Heightmap::carveRivers() {
  const int count = Config::RIVER_COUNT;
  const float sourceMin = Config::RIVER_SOURCE_MIN_H;
  const int minLen = Config::RIVER_MIN_LENGTH;

  // Collect candidate source cells (high ground, dry land)
  std::vector<std::pair<int, int>> candidates;
  candidates.reserve(1024);
  for (int z = m_size / 4; z < 3 * m_size / 4; ++z)
    for (int x = m_size / 4; x < 3 * m_size / 4; ++x)
      if (get(x, z) >= sourceMin && waterAt(x, z) == WaterType::None)
        candidates.push_back({x, z});

  if (candidates.empty())
    return;

  int carved = 0;
  int attempts = 0;
  const int maxAttempts = count * 20;

  while (carved < count && attempts < maxAttempts) {
    ++attempts;
    // Pick a random candidate
    size_t idx = static_cast<size_t>(nextRand() %
                                     static_cast<uint32_t>(candidates.size()));
    auto [sx, sz] = candidates[idx];

    std::vector<std::pair<int, int>> path;
    path.reserve(512);

    if (flowDownhill(sx, sz, path) && static_cast<int>(path.size()) >= minLen) {
      carveChannel(path);
      ++carved;
    }
  }
}

// ================================================================
// Lake flooding
// ================================================================
void Heightmap::floodFillLake(int startX, int startZ) {
  const int maxCells = Config::LAKE_MAX_CELLS;
  float lakeLevel =
      get(startX, startZ) + 0.02f; // fill slightly above local min

  std::queue<std::pair<int, int>> q;
  std::vector<bool> visited(static_cast<size_t>(m_size * m_size), false);

  q.push({startX, startZ});
  visited[static_cast<size_t>(startZ * m_size + startX)] = true;
  int filled = 0;

  const int dx4[] = {0, 1, 0, -1};
  const int dz4[] = {-1, 0, 1, 0};

  while (!q.empty() && filled < maxCells) {
    auto [cx, cz] = q.front();
    q.pop();

    if (waterAt(cx, cz) != WaterType::None)
      continue;
    if (get(cx, cz) > lakeLevel)
      continue;

    waterRef(cx, cz) = WaterType::Lake;
    ++filled;

    for (int d = 0; d < 4; ++d) {
      int nx = cx + dx4[d], nz = cz + dz4[d];
      if (!inBounds(nx, nz))
        continue;
      size_t ni = static_cast<size_t>(nz * m_size + nx);
      if (visited[ni])
        continue;
      visited[ni] = true;
      if (waterAt(nx, nz) == WaterType::None && get(nx, nz) <= lakeLevel)
        q.push({nx, nz});
    }
  }
}

void Heightmap::floodLakes() {
  const int count = Config::LAKE_COUNT;
  const float minH = Config::LAKE_MIN_H;
  const float maxH = Config::LAKE_MAX_H;

  // Find local minima on dry land in the valid height band
  std::vector<std::pair<int, int>> minima;
  const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  const int dz8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

  const int margin = m_size / 6; // exclude outer ~17% — falloff zone
  for (int z = margin; z < m_size - margin; ++z)
    for (int x = margin; x < m_size - margin; ++x) {
      if (waterAt(x, z) != WaterType::None)
        continue;
      float h = get(x, z);
      if (h < minH || h > maxH)
        continue;

      bool isMin = true;
      for (int d = 0; d < 8; ++d) {
        if (get(x + dx8[d], z + dz8[d]) < h) {
          isMin = false;
          break;
        }
      }
      if (isMin)
        minima.push_back({x, z});
    }

  // Shuffle and take up to count
  for (size_t i = minima.size(); i > 1; --i) {
    size_t j = static_cast<size_t>(nextRand() % static_cast<uint32_t>(i));
    std::swap(minima[i - 1], minima[j]);
  }

  int placed = 0;
  for (auto &[lx, lz] : minima) {
    if (placed >= count)
      break;
    if (waterAt(lx, lz) != WaterType::None)
      continue;
    floodFillLake(lx, lz);
    ++placed;
  }
}

// ================================================================
// Accessors
// ================================================================
bool Heightmap::inBounds(int x, int z) const {
  return x >= 0 && x < m_size && z >= 0 && z < m_size;
}

void Heightmap::set(int x, int z, float value) {
  x = std::clamp(x, 0, m_size - 1);
  z = std::clamp(z, 0, m_size - 1);
  m_data[static_cast<size_t>(z * m_size + x)] = value;
}

float Heightmap::get(int x, int z) const {
  x = std::clamp(x, 0, m_size - 1);
  z = std::clamp(z, 0, m_size - 1);
  return m_data[static_cast<size_t>(z * m_size + x)];
}

float Heightmap::sample(float x, float z) const {
  int ix = static_cast<int>(x);
  int iz = static_cast<int>(z);
  float fx = x - static_cast<float>(ix);
  float fz = z - static_cast<float>(iz);
  return get(ix, iz) * (1 - fx) * (1 - fz) + get(ix + 1, iz) * fx * (1 - fz) +
         get(ix, iz + 1) * (1 - fx) * fz + get(ix + 1, iz + 1) * fx * fz;
}

WaterType &Heightmap::waterRef(int x, int z) {
  x = std::clamp(x, 0, m_size - 1);
  z = std::clamp(z, 0, m_size - 1);
  return m_water[static_cast<size_t>(z * m_size + x)];
}

WaterType Heightmap::waterAt(int x, int z) const {
  x = std::clamp(x, 0, m_size - 1);
  z = std::clamp(z, 0, m_size - 1);
  return m_water[static_cast<size_t>(z * m_size + x)];
}