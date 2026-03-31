#include "Heightmap.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>

// ----------------------------------------------------------------
// RNG — xorshift32
// ----------------------------------------------------------------
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

// ----------------------------------------------------------------
// generate
// ----------------------------------------------------------------
void Heightmap::generate(int size, float roughness, uint32_t seed) {
  m_size = size;
  m_data.assign(static_cast<size_t>(size * size), 0.0f);
  m_rng = (seed == 0) ? static_cast<uint32_t>(std::time(nullptr)) : seed;

  // Seed corners
  int last = size - 1;
  set(0, 0, randF(0.3f, 0.7f));
  set(last, 0, randF(0.3f, 0.7f));
  set(0, last, randF(0.3f, 0.7f));
  set(last, last, randF(0.3f, 0.7f));

  diamondSquare(roughness);

  // Smooth heavily to turn spike-field into connected landmasses
  smooth(12);

  // Taper edges to sea so the planet feels like a world with ocean borders
  applyRadialFalloff();

  normalise();
}

// ----------------------------------------------------------------
// Diamond-Square
// ----------------------------------------------------------------
void Heightmap::diamondSquare(float roughness) {
  int step = m_size - 1;
  float scale = 1.0f;

  while (step > 1) {
    int half = step / 2;

    // Diamond step
    for (int z = 0; z < m_size - 1; z += step)
      for (int x = 0; x < m_size - 1; x += step) {
        float avg = (get(x, z) + get(x + step, z) + get(x, z + step) +
                     get(x + step, z + step)) *
                    0.25f;
        set(x + half, z + half, avg + randF(-scale, scale));
      }

    // Square step
    for (int z = 0; z < m_size; z += half)
      for (int x = (z + half) % step; x < m_size; x += step) {
        float sum = 0.0f;
        int count = 0;
        if (z - half >= 0) {
          sum += get(x, z - half);
          ++count;
        }
        if (z + half < m_size) {
          sum += get(x, z + half);
          ++count;
        }
        if (x - half >= 0) {
          sum += get(x - half, z);
          ++count;
        }
        if (x + half < m_size) {
          sum += get(x + half, z);
          ++count;
        }
        set(x, z, (sum / static_cast<float>(count)) + randF(-scale, scale));
      }

    step = half;
    scale *= std::pow(2.0f, -roughness);
  }
}

// ----------------------------------------------------------------
// Smooth — repeated 3x3 box blur
// Converts spike-field into connected rolling landmasses
// ----------------------------------------------------------------
void Heightmap::smooth(int passes) {
  std::vector<float> tmp(m_data.size());

  for (int p = 0; p < passes; ++p) {
    for (int z = 0; z < m_size; ++z) {
      for (int x = 0; x < m_size; ++x) {
        float sum = 0.0f;
        int cnt = 0;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) {
            int nx = x + dx, nz = z + dz;
            if (nx >= 0 && nx < m_size && nz >= 0 && nz < m_size) {
              sum += m_data[static_cast<size_t>(nz * m_size + nx)];
              ++cnt;
            }
          }
        tmp[static_cast<size_t>(z * m_size + x)] =
            sum / static_cast<float>(cnt);
      }
    }
    m_data = tmp;
  }
}

// ----------------------------------------------------------------
// Radial falloff — taper heights toward the heightmap edges
// Creates a natural ocean border so the continent sits in the middle
// ----------------------------------------------------------------
void Heightmap::applyRadialFalloff() {
  float centre = static_cast<float>(m_size - 1) * 0.5f;
  float maxR = centre * 0.85f; // falloff starts at 85% of half-width

  for (int z = 0; z < m_size; ++z) {
    for (int x = 0; x < m_size; ++x) {
      float dx = static_cast<float>(x) - centre;
      float dz = static_cast<float>(z) - centre;
      float r = sqrtf(dx * dx + dz * dz);

      float factor = 1.0f;
      if (r > maxR) {
        // Smooth cosine falloff from maxR to edge
        float t = (r - maxR) / (centre - maxR);
        t = std::min(t, 1.0f);
        factor = (cosf(t * 3.14159f) + 1.0f) * 0.5f;
      }

      size_t idx = static_cast<size_t>(z * m_size + x);
      m_data[idx] *= factor;
    }
  }
}

// ----------------------------------------------------------------
// Accessors
// ----------------------------------------------------------------
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

  float h00 = get(ix, iz);
  float h10 = get(ix + 1, iz);
  float h01 = get(ix, iz + 1);
  float h11 = get(ix + 1, iz + 1);

  return h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz +
         h11 * fx * fz;
}

// ----------------------------------------------------------------
// Normalise to [0,1]
// ----------------------------------------------------------------
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