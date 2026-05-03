#include "Heightmap.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
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

  // noiseGenerate handles base normalisation, symmetric shape contrast,
  // and mountain layering internally — no post-pipeline normalise() or
  // shapeContrast() pass is needed (and they would re-shift sea level).
  noiseGenerate(m_rng);
  classifyOcean();
  // Rivers disabled — lakes only. carveRivers() / flowDownhill() /
  // carveChannel() remain in the file for re-enabling later.
  floodLakes();
}

// ================================================================
// Tileable Perlin (gradient) noise — anonymous namespace helpers.
//
// Standard 2D gradient noise but with the lattice index wrapped modulo
// the per-octave grid period. Every octave's lattice cell size divides
// the heightmap tile period (m_size - 1) evenly, so every octave wraps
// exactly at the tile boundary — preserving the seamless toroidal
// world the chunk-tiling renderer relies on.
// ================================================================
namespace {

// Mix-and-rotate hash. Cheap, deterministic, no axis bias.
inline uint32_t hash2(int x, int z, int period, uint32_t seed) {
  // Wrap to [0, period) so the noise lattice is periodic.
  x = ((x % period) + period) % period;
  z = ((z % period) + period) % period;
  uint32_t h = seed;
  h ^= static_cast<uint32_t>(x) * 0x6C50B47Cu;
  h = (h << 13) | (h >> 19);
  h ^= static_cast<uint32_t>(z) * 0x9E3779B1u;
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

// 8 fixed unit gradient directions — Perlin's standard 2D gradient set.
// Selecting from a small set is faster than computing trig per lattice
// point and gives the same visual result.
inline void gradAt(int gx, int gz, int period, uint32_t seed, float &gx_out,
                   float &gz_out) {
  uint32_t h = hash2(gx, gz, period, seed);
  static const float G[8][2] = {
      {1.0f, 0.0f},        {-1.0f, 0.0f},      {0.0f, 1.0f},
      {0.0f, -1.0f},       {0.7071f, 0.7071f}, {-0.7071f, 0.7071f},
      {0.7071f, -0.7071f}, {-0.7071f, -0.7071f},
  };
  gx_out = G[h & 7][0];
  gz_out = G[h & 7][1];
}

// Quintic Hermite — Perlin's "improved" interpolant, C2 continuous so
// derivatives don't kink at lattice lines. Bilinear smoothstep would
// reintroduce visible grid artifacts.
inline float fade(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Tileable 2D Perlin noise. cellSize is the lattice spacing in
// heightmap cells; tilePeriod / cellSize must be an integer.
// Output is roughly in [-1, 1].
inline float perlin2D(float x, float z, int cellSize, int tilePeriod,
                      uint32_t seed) {
  const int period = tilePeriod / cellSize;
  const float fx = x / static_cast<float>(cellSize);
  const float fz = z / static_cast<float>(cellSize);
  const int ix = static_cast<int>(floorf(fx));
  const int iz = static_cast<int>(floorf(fz));
  const float dx = fx - static_cast<float>(ix);
  const float dz = fz - static_cast<float>(iz);

  float g00x, g00z, g10x, g10z, g01x, g01z, g11x, g11z;
  gradAt(ix, iz, period, seed, g00x, g00z);
  gradAt(ix + 1, iz, period, seed, g10x, g10z);
  gradAt(ix, iz + 1, period, seed, g01x, g01z);
  gradAt(ix + 1, iz + 1, period, seed, g11x, g11z);

  // Dot products of gradient with corner-relative offset.
  const float d00 = g00x * dx + g00z * dz;
  const float d10 = g10x * (dx - 1.0f) + g10z * dz;
  const float d01 = g01x * dx + g01z * (dz - 1.0f);
  const float d11 = g11x * (dx - 1.0f) + g11z * (dz - 1.0f);

  const float u = fade(dx);
  const float v = fade(dz);
  const float a = d00 + u * (d10 - d00);
  const float b = d01 + u * (d11 - d01);
  return a + v * (b - a);
}

} // namespace

// ================================================================
// noiseGenerate — fill m_data[] via tileable Perlin fBM with a 2D
// coupled domain warp applied beforehand. Six octaves at lattice
// spacings 256→8 cells; persistence Config::FBM_PERSISTENCE.
//
// Why this and not sine sums: pure sums of sin(fx·x + fz·z) produce
// straight constructive-interference ridges no matter how many terms
// or how wild the domain warp. Gradient noise has no preferred
// direction — its output is statistically isotropic — so the
// "chaotic" hill structures of a real landscape emerge naturally.
// ================================================================
void Heightmap::noiseGenerate(uint32_t seed) {
  const int tilePeriod = m_size - 1;

  // 2D coupled domain warp — same rationale as before. warpX and warpZ
  // each depend on both x and z, so the perturbation bends features
  // diagonally instead of stretching along the grid axes. Frequencies
  // are integer cycles per tile period so the warp itself wraps.
  const float omega = 2.0f * 3.14159265f / static_cast<float>(tilePeriod);
  const float warpFxX = 1.0f * omega;
  const float warpFxZ = 2.0f * omega;
  const float warpFzX = 2.0f * omega;
  const float warpFzZ = 1.0f * omega;

  uint32_t rng2 = seed ^ 0xCAFEBABE;
  rng2 ^= rng2 << 13;
  rng2 ^= rng2 >> 17;
  rng2 ^= rng2 << 5;
  const float warpPhase =
      static_cast<float>(rng2) / static_cast<float>(0xFFFFFFFFu) * 6.28318f;

  // Pre-compute base fBM octave tables.
  const int O = Config::FBM_OCTAVES;
  std::vector<int> cellSizes(O);
  std::vector<float> amps(O);
  {
    int cs = Config::FBM_LARGEST_CELL;
    float amp = 1.0f;
    float totalAmp = 0.0f;
    for (int o = 0; o < O; ++o) {
      cellSizes[o] = cs;
      amps[o] = amp;
      totalAmp += amp;
      cs /= 2;
      amp *= Config::FBM_PERSISTENCE;
    }
    if (totalAmp <= 0.0f) totalAmp = 1.0f;
    // Pre-divide so we can map directly to [0,1] without a per-cell divide.
    for (int o = 0; o < O; ++o) amps[o] /= totalAmp;
  }

  // Pre-compute mountain (ridged) layer octave tables. Standard 0.5
  // persistence keeps the ridges crisp without high-frequency noise
  // dominating the layer.
  const int MO = Config::FBM_MOUNTAIN_OCTAVES;
  std::vector<int> mountCellSizes(MO);
  std::vector<float> mountAmps(MO);
  {
    int cs = Config::FBM_MOUNTAIN_LARGEST_CELL;
    float amp = 1.0f;
    float totalAmp = 0.0f;
    for (int o = 0; o < MO; ++o) {
      mountCellSizes[o] = cs;
      mountAmps[o] = amp;
      totalAmp += amp;
      cs /= 2;
      amp *= 0.5f;
    }
    if (totalAmp <= 0.0f) totalAmp = 1.0f;
    for (int o = 0; o < MO; ++o) mountAmps[o] /= totalAmp;
  }

  const float mLow = Config::FBM_MOUNTAIN_THRESHOLD_LOW;
  const float mHigh = Config::FBM_MOUNTAIN_THRESHOLD_HIGH;
  const float mountAmp = Config::FBM_MOUNTAIN_AMP;
  const float shapeE = Config::FBM_SHAPE_EXPONENT;
  const float invShape = 1.0f / shapeE;

  // ---- Pass 1: compute base fBM, track min/max for normalisation ----
  // Storing base separately lets us stretch it to exactly [0,1] BEFORE
  // adding mountains. Otherwise the mountain layer pushes max above 1.0
  // and a subsequent normalise() would shift the entire distribution
  // down — drowning the map (which is exactly what was happening at
  // 79% ocean coverage in the previous pipeline).
  std::vector<float> base(static_cast<size_t>(m_size * m_size));
  float baseMin = 1e9f, baseMax = -1e9f;
  for (int z = 0; z < m_size; ++z) {
    for (int x = 0; x < m_size; ++x) {
      const float fx = static_cast<float>(x);
      const float fz = static_cast<float>(z);
      const float warpX =
          fx + Config::SINE_WARP_AMPLITUDE *
                   sinf(fx * warpFxX + fz * warpFxZ + warpPhase);
      const float warpZ = fz + Config::SINE_WARP_AMPLITUDE *
                                   sinf(fx * warpFzX + fz * warpFzZ +
                                        warpPhase + 1.5708f);
      float bh = 0.0f;
      for (int o = 0; o < O; ++o) {
        bh += amps[o] * perlin2D(warpX, warpZ, cellSizes[o], tilePeriod,
                                 seed + static_cast<uint32_t>(o) * 1009u);
      }
      bh = bh * 0.5f + 0.5f;
      base[static_cast<size_t>(z * m_size + x)] = bh;
      if (bh < baseMin) baseMin = bh;
      if (bh > baseMax) baseMax = bh;
    }
  }
  const float baseRange =
      (baseMax - baseMin) > 1e-6f ? (baseMax - baseMin) : 1.0f;

  // ---- Pass 2: stretch base, apply symmetric shape contrast,
  //              add mountain layer, clamp, write final ----
  for (int z = 0; z < m_size; ++z) {
    for (int x = 0; x < m_size; ++x) {
      const float fx = static_cast<float>(x);
      const float fz = static_cast<float>(z);
      const float warpX =
          fx + Config::SINE_WARP_AMPLITUDE *
                   sinf(fx * warpFxX + fz * warpFxZ + warpPhase);
      const float warpZ = fz + Config::SINE_WARP_AMPLITUDE *
                                   sinf(fx * warpFzX + fz * warpFzZ +
                                        warpPhase + 1.5708f);

      // Stretch base to exactly [0,1].
      float bh = (base[static_cast<size_t>(z * m_size + x)] - baseMin) /
                 baseRange;

      // Symmetric contrast around 0.5 — pushes peaks higher and valleys
      // deeper while keeping the median at 0.5 so SEA_LEVEL holds its
      // intended percentile coverage.
      float shaped;
      if (bh < 0.5f) {
        shaped = 0.5f - 0.5f * powf(1.0f - 2.0f * bh, invShape);
      } else {
        shaped = 0.5f + 0.5f * powf(2.0f * bh - 1.0f, invShape);
      }

      // Smoothstep mountain mask on the shaped base.
      float mask = (shaped - mLow) / (mHigh - mLow);
      if (mask < 0.0f) mask = 0.0f;
      else if (mask > 1.0f) mask = 1.0f;
      mask = mask * mask * (3.0f - 2.0f * mask);

      float mountainH = 0.0f;
      if (mask > 0.0f) {
        for (int o = 0; o < MO; ++o) {
          float n = perlin2D(warpX, warpZ, mountCellSizes[o], tilePeriod,
                             seed + static_cast<uint32_t>(o) * 7919u +
                                 0xBADCAFEu);
          float r = 1.0f - fabsf(n);
          r = r * r;
          mountainH += mountAmps[o] * r;
        }
      }

      float h = shaped + mountAmp * mask * mountainH;
      if (h < 0.0f) h = 0.0f;
      // No upper clamp — letting peaks exceed 1.0 keeps the per-cell
      // detail intact at the highest elevations. Clamping caused ridge
      // saturation regions to plateau into flat snowy tabletops.
      // landColor() handles landH > 1.0 correctly (still snow), and the
      // chunk renderer just multiplies by HEIGHT_MAX, so peaks above
      // ~1.3 just become slightly taller mountains.
      m_data[static_cast<size_t>(z * m_size + x)] = h;
    }
  }

  // shapeE silences the unused warning when the shape pow is the only
  // consumer; nothing else to do here.
  (void)shapeE;
}

// ================================================================
// shapeContrast — non-linear post-process on the [0,1] heightmap.
// FBM_SHAPE_EXPONENT > 1 flattens lowlands and sharpens peaks, giving
// more visible separation between coast, plains and mountains than
// pure fBM (which is gaussian-distributed and looks "smushed").
// ================================================================
void Heightmap::shapeContrast() {
  const float e = Config::FBM_SHAPE_EXPONENT;
  if (e == 1.0f) return;
  for (float &v : m_data)
    v = powf(v, e);
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
  const int minCells = Config::LAKE_MIN_SIZE;
  float lakeLevel = get(startX, startZ) + Config::LAKE_FILL_EPSILON;

  std::queue<std::pair<int, int>> q;
  std::vector<bool> visited(static_cast<size_t>(m_size * m_size), false);
  // Collect candidate cells first; commit only if the basin is large
  // enough. This drops single-cell "speck" minima that smooth Perlin
  // terrain produces, without needing an undo pass.
  std::vector<std::pair<int, int>> candidates;
  candidates.reserve(static_cast<size_t>(maxCells));

  q.push({startX, startZ});
  visited[static_cast<size_t>(startZ * m_size + startX)] = true;

  const int dx4[] = {0, 1, 0, -1};
  const int dz4[] = {-1, 0, 1, 0};

  while (!q.empty() && static_cast<int>(candidates.size()) < maxCells) {
    auto [cx, cz] = q.front();
    q.pop();

    if (waterAt(cx, cz) != WaterType::None)
      continue;
    if (get(cx, cz) > lakeLevel)
      continue;

    candidates.push_back({cx, cz});

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

  if (static_cast<int>(candidates.size()) < minCells) return;
  for (auto &[cx, cz] : candidates)
    waterRef(cx, cz) = WaterType::Lake;
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

      // Lake candidate: at least LAKE_MIN_HIGHER_NEIGHBOURS of the 8
      // neighbours must be at least LAKE_MIN_DEPTH higher than this
      // cell. Allowing one or two equal/lower neighbours lets lakes
      // form on smooth Perlin basins, while still rejecting pure
      // saddle points (where 4+ neighbours would be lower).
      int higher = 0;
      for (int d = 0; d < 8; ++d) {
        int nx = wrapIdx(x + dx8[d], m_size - 1);
        int nz = wrapIdx(z + dz8[d], m_size - 1);
        if (get(nx, nz) >= h + Config::LAKE_MIN_DEPTH) ++higher;
      }
      if (higher >= Config::LAKE_MIN_HIGHER_NEIGHBOURS)
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

// ================================================================
// Dev export — F6 in DEV_MODE writes a greyscale PNG of the heightmap
// plus a text dump (stats, histogram, ASCII preview) for inspection.
// ================================================================
void Heightmap::exportPng(const std::string &path) const {
  if (m_data.empty()) return;
  unsigned char *bytes = static_cast<unsigned char *>(
      RL_MALLOC(static_cast<size_t>(m_size * m_size)));
  for (int i = 0; i < m_size * m_size; ++i) {
    int v = static_cast<int>(m_data[static_cast<size_t>(i)] * 255.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    bytes[i] = static_cast<unsigned char>(v);
  }
  Image img{};
  img.data = bytes;
  img.width = m_size;
  img.height = m_size;
  img.mipmaps = 1;
  img.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
  ExportImage(img, path.c_str());
  RL_FREE(bytes);
}

void Heightmap::exportStats(const std::string &path) const {
  if (m_data.empty()) return;
  std::ofstream f(path);
  if (!f.is_open()) return;

  // Statistics
  std::vector<float> sorted = m_data;
  std::sort(sorted.begin(), sorted.end());
  float lo = sorted.front();
  float hi = sorted.back();
  double sum = 0.0;
  for (float v : m_data) sum += v;
  double mean = sum / m_data.size();
  double variance = 0.0;
  for (float v : m_data) {
    double d = v - mean;
    variance += d * d;
  }
  double stddev = sqrt(variance / m_data.size());

  auto pct = [&](double p) {
    size_t i = static_cast<size_t>(p * (sorted.size() - 1));
    return sorted[i];
  };

  // Land/water/lake/ocean composition
  size_t nOcean = 0, nLake = 0, nRiver = 0;
  for (auto w : m_water) {
    if (w == WaterType::Ocean) ++nOcean;
    else if (w == WaterType::Lake) ++nLake;
    else if (w == WaterType::River) ++nRiver;
  }
  double total = static_cast<double>(m_data.size());

  f << "Heightmap analysis\n";
  f << "==================\n";
  f << "size:         " << m_size << "x" << m_size << "\n";
  f << "min:          " << lo << "\n";
  f << "max:          " << hi << "\n";
  f << "mean:         " << mean << "\n";
  f << "stddev:       " << stddev << "\n";
  f << "P10:          " << pct(0.10) << "\n";
  f << "P25:          " << pct(0.25) << "\n";
  f << "P50 (median): " << pct(0.50) << "\n";
  f << "P75:          " << pct(0.75) << "\n";
  f << "P90:          " << pct(0.90) << "\n";
  f << "P99:          " << pct(0.99) << "\n";
  f << "\n";
  f << "Coverage (% of cells):\n";
  f << "  ocean:      " << (100.0 * nOcean / total) << "%\n";
  f << "  lake:       " << (100.0 * nLake / total) << "%\n";
  f << "  river:      " << (100.0 * nRiver / total) << "%\n";
  f << "  land:       "
    << (100.0 * (total - nOcean - nLake - nRiver) / total) << "%\n";
  f << "\n";

  // Histogram (20 bins over [0,1])
  const int BINS = 20;
  int hist[BINS] = {0};
  for (float v : m_data) {
    int b = static_cast<int>(v * BINS);
    if (b < 0) b = 0;
    if (b >= BINS) b = BINS - 1;
    ++hist[b];
  }
  int maxCount = 0;
  for (int i = 0; i < BINS; ++i)
    if (hist[i] > maxCount) maxCount = hist[i];
  f << "Histogram (20 bins, max bar = 50 chars):\n";
  for (int i = 0; i < BINS; ++i) {
    float blo = static_cast<float>(i) / BINS;
    float bhi = static_cast<float>(i + 1) / BINS;
    int barW = maxCount > 0 ? (50 * hist[i] / maxCount) : 0;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[%4.2f-%4.2f] ", blo, bhi);
    f << buf;
    for (int j = 0; j < barW; ++j) f << '#';
    f << ' ' << hist[i] << '\n';
  }
  f << '\n';

  // ASCII preview, downsampled to 64x64 of the full heightmap.
  // Sea-level cells render as '~', then ten shades from low to high land.
  const int PREVIEW = 64;
  const int step = m_size / PREVIEW;
  const char shades[] = " .:-=+*#%@";
  const int shadeCount = static_cast<int>(sizeof(shades)) - 1;
  const float seaLevel = Config::SEA_LEVEL;
  f << "ASCII preview (" << PREVIEW << "x" << PREVIEW
    << ", '~' = ocean/lake, ' .:-=+*#%@' = low→high):\n";
  for (int z = 0; z < PREVIEW; ++z) {
    for (int x = 0; x < PREVIEW; ++x) {
      int hx = x * step;
      int hz = z * step;
      WaterType w = m_water[static_cast<size_t>(hz * m_size + hx)];
      if (w == WaterType::Ocean || w == WaterType::Lake ||
          w == WaterType::River) {
        f << '~';
      } else {
        float v = m_data[static_cast<size_t>(hz * m_size + hx)];
        float landH = (v - seaLevel) / (1.0f - seaLevel);
        if (landH < 0.0f) landH = 0.0f;
        if (landH > 1.0f) landH = 1.0f;
        int s = static_cast<int>(landH * (shadeCount - 1) + 0.5f);
        if (s < 0) s = 0;
        if (s >= shadeCount) s = shadeCount - 1;
        f << shades[s];
      }
    }
    f << '\n';
  }

  f.close();
}
