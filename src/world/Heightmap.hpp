#pragma once

#include <cstdint>
#include <vector>

// ====================================================================
// Heightmap
// Diamond-Square fractal generation + smoothing + river carving +
// lake flooding. Produces a water-type map alongside height data so
// the renderer can colour ocean / lake / river differently.
// Size must be (2^n + 1): 129, 257, 513, 1025 etc.
// Heights stored normalised [0,1].
// ====================================================================

enum class WaterType : uint8_t {
  None = 0,  // dry land
  Ocean = 1, // below sea level
  Lake = 2,  // inland lake (calm, darker blue)
  River = 3, // flowing river (lighter, animated later)
};

class Heightmap {
public:
  Heightmap() = default;

  void generate(int size, float roughness = 0.55f, uint32_t seed = 0);

  float get(int x, int z) const;
  float sample(float x, float z) const;
  WaterType waterAt(int x, int z) const;

  int size() const { return m_size; }

private:
  // Generation pipeline
  void diamondSquare(float roughness);
  void smooth(int passes);
  void applyRadialFalloff();
  void normalise();
  void classifyOcean();
  void carveRivers();
  void floodLakes();

  // River helpers
  bool flowDownhill(int startX, int startZ,
                    std::vector<std::pair<int, int>> &path);
  void carveChannel(const std::vector<std::pair<int, int>> &path);

  // Lake helpers
  void floodFillLake(int x, int z);

  // Data access
  void set(int x, int z, float value);
  WaterType &waterRef(int x, int z);
  bool inBounds(int x, int z) const;

  // RNG
  float randF(float lo, float hi);
  uint32_t nextRand();
  uint32_t m_rng = 0;

  int m_size = 0;
  std::vector<float> m_data;
  std::vector<WaterType> m_water;
};