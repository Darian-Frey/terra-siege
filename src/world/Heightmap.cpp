#include "Heightmap.hpp"
#include "core/Config.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <queue>

// ================================================================
// RNG — xorshift32
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
// Toroidal index wrap helper.
// The heightmap stores m_size cells; cell (m_size-1) is geometrically
// identical to cell 0 because the world wraps. We wrap modulo
// (m_size - 1) so the duplicated edge column never causes a stutter.
// ================================================================
static inline int wrapIdx(int v, int period) {
  v %= period;
  if (v < 0) v += period;
  return v;
}

// ================================================================
// generate — full pipeline
// ================================================================
void Heightmap::generate(int size, uint32_t seed) {
  m_size = size;
  m_data.assign(static_cast<size_t>(size * size), 0.0f);
  m_water.assign(static_cast<size_t>(size * size), WaterType::None);
  m_rng = (seed == 0) ? static_cast<uint32_t>(std::time(nullptr)) : seed;

  sineWaveGenerate(m_rng);
  normalise();
  classifyOcean();
  carveRivers();
  floodLakes();
}

// ================================================================
// buildSineTerms — assemble the 16-term Fourier basis from the seed.
//
// Every (kx, kz) pair below is an INTEGER number of cycles over one
// tile period (m_size - 1 cells). With ω = 2π / tilePeriod, each
// term's spatial frequency is k·ω, so sin(k·ω·tilePeriod) = sin(2π·k)
// — identical to sin(0). The terrain therefore wraps EXACTLY at the
// tile boundary. Coprime small primes (5, 7, 11, 13, 19, …, 43) keep
// the visible variation high within each tile.
//
// This is the property the chunk-tiling renderer needs: without it,
// adjacent tile copies disagree about height and slope across the
// seam and a visible cliff appears.
// ================================================================
void Heightmap::buildSineTerms(uint32_t seed,
                               std::vector<SineWaveTerm> &terms) {
  const float omega = 2.0f * 3.14159265f / static_cast<float>(m_size - 1);

  uint32_t rng = seed ^ 0xDEADBEEF;
  auto nextPhase = [&]() -> float {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float>(rng) / static_cast<float>(0xFFFFFFFFu) * 6.28318f;
  };

  terms.clear();
  terms.reserve(static_cast<size_t>(Config::SINE_CONTINENTAL_COUNT +
                                    Config::SINE_REGIONAL_COUNT +
                                    Config::SINE_LOCAL_COUNT));

  // ---- Continental (4) — broad plateaus, deep valleys, ranges ----
  // 1–3 cycles per world. Different X/Z multipliers break grid alignment.
  const int CONT_K[4][2] = {
      {1, 1}, {1, 3}, {3, 1}, {3, 3},
  };
  for (int i = 0; i < Config::SINE_CONTINENTAL_COUNT; ++i) {
    float fx = static_cast<float>(CONT_K[i][0]) * omega;
    float fz = static_cast<float>(CONT_K[i][1]) * omega;
    terms.push_back({fx, fz, Config::SINE_CONTINENTAL_AMP, nextPhase()});
  }

  // ---- Regional (6) — hills, ridges, river valleys (~6–13 cycles/tile) ----
  const int REG_K[6][2] = {
      {5, 7}, {7, 11}, {11, 5}, {5, 13}, {13, 7}, {11, 9},
  };
  for (int i = 0; i < Config::SINE_REGIONAL_COUNT; ++i) {
    float fx = static_cast<float>(REG_K[i][0]) * omega;
    float fz = static_cast<float>(REG_K[i][1]) * omega;
    float amp =
        Config::SINE_REGIONAL_AMP * (1.0f - 0.1f * static_cast<float>(i));
    terms.push_back({fx, fz, amp, nextPhase()});
  }

  // ---- Local (6) — surface texture (~19–43 cycles/tile) ----
  const int LOC_K[6][2] = {
      {19, 23}, {23, 29}, {29, 31}, {31, 37}, {37, 41}, {41, 43},
  };
  for (int i = 0; i < Config::SINE_LOCAL_COUNT; ++i) {
    float fx = static_cast<float>(LOC_K[i][0]) * omega;
    float fz = static_cast<float>(LOC_K[i][1]) * omega;
    float amp =
        Config::SINE_LOCAL_AMP * (1.0f - 0.08f * static_cast<float>(i));
    terms.push_back({fx, fz, amp, nextPhase()});
  }
}

// ================================================================
// sineWaveGenerate — fill m_data[] via Fourier synthesis with domain
// warping. ~16 sinf calls per cell; <100 ms for 1025² on modern CPUs.
// ================================================================
void Heightmap::sineWaveGenerate(uint32_t seed) {
  std::vector<SineWaveTerm> terms;
  buildSineTerms(seed, terms);

  // Per-seed warp phase so the warping pattern varies between worlds.
  uint32_t rng2 = seed ^ 0xCAFEBABE;
  rng2 ^= rng2 << 13;
  rng2 ^= rng2 >> 17;
  rng2 ^= rng2 << 5;
  const float warpPhase =
      static_cast<float>(rng2) / static_cast<float>(0xFFFFFFFFu) * 6.28318f;

  // Warp frequency must also be an integer multiple of ω so the domain
  // warp is itself periodic at the tile boundary. Otherwise the warped
  // sample coordinates disagree across the seam and the wrap is broken.
  const float omega = 2.0f * 3.14159265f / static_cast<float>(m_size - 1);
  const float warpFreq = static_cast<float>(Config::SINE_WARP_CYCLES) * omega;

  float totalAmp = 0.0f;
  for (const auto &t : terms) totalAmp += t.amplitude;
  if (totalAmp <= 0.0f) totalAmp = 1.0f;

  for (int z = 0; z < m_size; ++z) {
    for (int x = 0; x < m_size; ++x) {
      float fx = static_cast<float>(x);
      float fz = static_cast<float>(z);

      float warpX =
          fx + Config::SINE_WARP_AMPLITUDE * sinf(fx * warpFreq + warpPhase);
      float warpZ = fz + Config::SINE_WARP_AMPLITUDE *
                             sinf(fz * warpFreq + warpPhase + 1.5708f);

      float h = 0.0f;
      for (const auto &t : terms)
        h += t.amplitude *
             sinf(t.freqX * warpX + t.freqZ * warpZ + t.phase);

      // Map roughly [-totalAmp, +totalAmp] → [0, 1]
      h = (h / totalAmp) * 0.5f + 0.5f;
      m_data[static_cast<size_t>(z * m_size + x)] = h;
    }
  }
}

// ================================================================
// Normalise to exactly [0,1]
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
// Returns true if path reaches the ocean.
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

    // Reached ocean — success. (No edge termination — toroidal world has
    // no edges; the river either reaches sea or dies in a local minimum.)
    if (waterAt(cx, cz) == WaterType::Ocean)
      return true;

    // Find steepest descent neighbour (using wrapping get())
    float lowestH = get(cx, cz);
    int bestX = -1, bestZ = -1;
    for (int d = 0; d < 8; ++d) {
      int nx = wrapIdx(cx + dx8[d], m_size - 1);
      int nz = wrapIdx(cz + dz8[d], m_size - 1);
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
      return false; // local minimum
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
        int nx = wrapIdx(px + dx, m_size - 1);
        int nz = wrapIdx(pz + dz, m_size - 1);

        // Taper carve depth toward channel edges
        float dist = sqrtf(static_cast<float>(dx * dx + dz * dz));
        float taper = 1.0f - (dist / static_cast<float>(W + 1));
        if (taper < 0.0f)
          taper = 0.0f;

        float &h = m_data[static_cast<size_t>(nz * m_size + nx)];
        h -= depth * taper;
        if (h < 0.0f)
          h = 0.0f;

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

  // High-ground dry-land sources, anywhere on the toroidal map.
  std::vector<std::pair<int, int>> candidates;
  candidates.reserve(2048);
  for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x)
      if (get(x, z) >= sourceMin && waterAt(x, z) == WaterType::None)
        candidates.push_back({x, z});

  if (candidates.empty())
    return;

  int carved = 0;
  int attempts = 0;
  const int maxAttempts = count * 20;

  while (carved < count && attempts < maxAttempts) {
    ++attempts;
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
  float lakeLevel = get(startX, startZ) + 0.02f;

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
      int nx = wrapIdx(cx + dx4[d], m_size - 1);
      int nz = wrapIdx(cz + dz4[d], m_size - 1);
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

  std::vector<std::pair<int, int>> minima;
  const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  const int dz8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

  // No margin needed — toroidal world has no falloff zone to exclude.
  for (int z = 0; z < m_size; ++z)
    for (int x = 0; x < m_size; ++x) {
      if (waterAt(x, z) != WaterType::None)
        continue;
      float h = get(x, z);
      if (h < minH || h > maxH)
        continue;

      bool isMin = true;
      for (int d = 0; d < 8; ++d) {
        int nx = wrapIdx(x + dx8[d], m_size - 1);
        int nz = wrapIdx(z + dz8[d], m_size - 1);
        if (get(nx, nz) < h) {
          isMin = false;
          break;
        }
      }
      if (isMin)
        minima.push_back({x, z});
    }

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
// Accessors — toroidal wrapping.
// Cell column/row (m_size-1) is identical to 0 by construction, so we
// wrap modulo (m_size - 1) to avoid a duplicated-edge stutter.
// ================================================================
bool Heightmap::inBounds(int x, int z) const {
  return x >= 0 && x < m_size && z >= 0 && z < m_size;
}

void Heightmap::set(int x, int z, float value) {
  x = wrapIdx(x, m_size - 1);
  z = wrapIdx(z, m_size - 1);
  m_data[static_cast<size_t>(z * m_size + x)] = value;
}

float Heightmap::get(int x, int z) const {
  int period = m_size - 1;
  x = wrapIdx(x, period);
  z = wrapIdx(z, period);
  return m_data[static_cast<size_t>(z * m_size + x)];
}

float Heightmap::sample(float x, float z) const {
  const float period = static_cast<float>(m_size - 1);
  // fmodf returns a value with the sign of the dividend — add period if negative.
  x = fmodf(x, period);
  if (x < 0.0f) x += period;
  z = fmodf(z, period);
  if (z < 0.0f) z += period;

  int ix = static_cast<int>(x);
  int iz = static_cast<int>(z);
  float fx = x - static_cast<float>(ix);
  float fz = z - static_cast<float>(iz);

  // get() already wraps, so ix+1 / iz+1 will tile correctly even at the seam.
  return get(ix, iz) * (1 - fx) * (1 - fz) +
         get(ix + 1, iz) * fx * (1 - fz) +
         get(ix, iz + 1) * (1 - fx) * fz +
         get(ix + 1, iz + 1) * fx * fz;
}

WaterType &Heightmap::waterRef(int x, int z) {
  x = wrapIdx(x, m_size - 1);
  z = wrapIdx(z, m_size - 1);
  return m_water[static_cast<size_t>(z * m_size + x)];
}

WaterType Heightmap::waterAt(int x, int z) const {
  int period = m_size - 1;
  x = wrapIdx(x, period);
  z = wrapIdx(z, period);
  return m_water[static_cast<size_t>(z * m_size + x)];
}
