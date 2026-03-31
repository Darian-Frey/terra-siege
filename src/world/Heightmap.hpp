#pragma once

#include <cstdint>
#include <vector>

// ====================================================================
// Heightmap
// Generates a fractal height field using the Diamond-Square algorithm,
// followed by a smoothing pass to produce connected landmasses rather
// than isolated spikes.
// Size must be (2^n + 1) e.g. 129, 257, 513.
// Heights are stored normalised [0,1].
// ====================================================================

class Heightmap {
public:
  Heightmap() = default;

  // Generate a new heightmap. seed=0 uses a random seed.
  void generate(int size, float roughness = 0.40f, uint32_t seed = 0);

  // Sample height at integer grid coordinates (clamped to edges)
  float get(int x, int z) const;

  // Bilinear sample at fractional coordinates
  float sample(float x, float z) const;

  int size() const { return m_size; }

private:
  void diamondSquare(float roughness);
  void smooth(int passes);   // box-blur to merge spikes into landmasses
  void applyRadialFalloff(); // taper edges to ocean for island continent feel
  void set(int x, int z, float value);
  void normalise();

  int m_size = 0;
  std::vector<float> m_data;
  uint32_t m_rng = 0;

  float randF(float lo, float hi);
  uint32_t nextRand();
};