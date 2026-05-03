#pragma once

#include <cstdint>
#include <vector>

// ====================================================================
// Heightmap
// Fourier-synthesis (sine wave) terrain generation, plus river carving
// and lake flooding. Produces a water-type map alongside height data so
// the renderer can colour ocean / lake / river distinctly.
// Size must be (2^n + 1): 129, 257, 513, 1025 etc.
// Heights stored normalised [0,1].
// Coordinates wrap toroidally — sample() handles negative and out-of-
// range queries.
// ====================================================================

enum class WaterType : uint8_t {
  None = 0,  // dry land
  Ocean = 1, // below sea level
  Lake = 2,  // inland lake (calm, darker blue)
  River = 3, // flowing river (lighter, animated later)
};

struct SineWaveTerm {
  float freqX;     // spatial frequency in X (rad per cell)
  float freqZ;     // spatial frequency in Z (rad per cell)
  float amplitude; // contribution weight
  float phase;     // seed-derived offset (radians)
};

class Heightmap {
public:
  Heightmap() = default;

  void generate(int size, uint32_t seed = 0);

  float get(int x, int z) const;        // wraps toroidally
  float sample(float x, float z) const; // bilinear, wraps toroidally
  WaterType waterAt(int x, int z) const;

  int size() const { return m_size; }

private:
  // Generation pipeline
  void buildSineTerms(uint32_t seed, std::vector<SineWaveTerm> &terms);
  void sineWaveGenerate(uint32_t seed);
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
