#include "Decorations.hpp"

#include "Planet.hpp"
#include "core/Config.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <cmath>

namespace {

// xorshift32 — same pattern used elsewhere in the project. Deterministic
// given the seed so F5 + reload reproduces the world layout.
uint32_t xorshift32(uint32_t &state) {
  uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

float frand(uint32_t &state) {
  return (xorshift32(state) & 0xffffff) / 16777216.0f;
}

float frand(uint32_t &state, float lo, float hi) {
  return lo + frand(state) * (hi - lo);
}

// "Anywhere on land" gate — 1 unit above sea level. Below that and we
// treat as water / beach (decorations on the jittery shoreline look
// bad).
bool isLand(const Planet &planet, float x, float z) {
  float y = planet.heightAt(x, z);
  float seaY = Config::SEA_LEVEL * Config::TERRAIN_HEIGHT_MAX;
  return y > seaY + 1.0f;
}

// Returns the normalised "landH" used by TerrainChunk::landColor —
// the same metric the visual biome bands track. Lets decoration
// placement use the exact same thresholds the terrain renders with
// (grass / rock / snow), so trees never bleed into the brown bands
// the player sees as mountain.
//
// landH = (h/HMAX - SEA_LEVEL) / (1 - SEA_LEVEL), clamped to [0, 1].
// Returns -1.0 for points below sea level (water).
float landBiome(const Planet &planet, float x, float z) {
  float y = planet.heightAt(x, z);
  float seaAbs = Config::SEA_LEVEL * Config::TERRAIN_HEIGHT_MAX;
  if (y <= seaAbs) return -1.0f;
  float h = y / Config::TERRAIN_HEIGHT_MAX;
  float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
  if (landH < 0.0f) return 0.0f;
  if (landH > 1.0f) return 1.0f;
  return landH;
}

// Biome bands match TerrainChunk::landColor exactly:
//   sand   = 0.00 .. 0.08
//   grass  = 0.08 .. 0.40
//   rock   = 0.40 .. 0.65   (the "brown" mountain colour)
//   high   = 0.65 .. 0.82
//   snow   = 0.82 .. 1.00
constexpr float kSandMax  = 0.08f;
constexpr float kGrassMax = 0.40f;
constexpr float kRockMax  = 0.65f;
constexpr float kHighMax  = 0.82f;

// 2D value noise — single-octave, smoothstep-interpolated. Used as a
// forest mask. Algorithm per Red Blob Games + Potter Programming:
//   * hash the 4 lattice corners surrounding (x, z) at the chosen freq
//   * bilinear blend with smoothstep easing
// Returns a value in [0, 1) that varies smoothly across the world.
// Frequency = 1/featureSize, so freq = 1/128 gives ~128m blobs (about
// the right scale for a forest patch).
float hashCorner(int x, int z, uint32_t seed) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u +
               static_cast<uint32_t>(z) * 668265263u +
               seed * 2654435761u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffffffu) / 16777216.0f;
}
float valueNoise2D(float x, float z, float freq, uint32_t seed) {
  float fx = x * freq;
  float fz = z * freq;
  int ix0 = static_cast<int>(floorf(fx));
  int iz0 = static_cast<int>(floorf(fz));
  float tx = fx - ix0;
  float tz = fz - iz0;
  tx = tx * tx * (3.0f - 2.0f * tx); // smoothstep
  tz = tz * tz * (3.0f - 2.0f * tz);
  float a = hashCorner(ix0,     iz0,     seed);
  float b = hashCorner(ix0 + 1, iz0,     seed);
  float c = hashCorner(ix0,     iz0 + 1, seed);
  float d = hashCorner(ix0 + 1, iz0 + 1, seed);
  float ab = a + (b - a) * tx;
  float cd = c + (d - c) * tx;
  return ab + (cd - ab) * tz;
}
// 2-octave fractal — adds finer detail to break up the smooth blobs
// at their boundaries. Coarse octave = forest body, fine octave =
// jagged edge / scattered outliers.
float forestMask(float x, float z, uint32_t seed) {
  float a = valueNoise2D(x, z, 1.0f / 140.0f, seed);            // coarse
  float b = valueNoise2D(x, z, 1.0f / 45.0f,  seed ^ 0x5a17u);  // edge detail
  return a * 0.75f + b * 0.25f;
}

// Approximate slope via finite differences. Returns |gradient| —
// 0 = flat, 1 ≈ 45°.
float slopeAt(const Planet &planet, float x, float z) {
  const float d = 4.0f;
  float h0 = planet.heightAt(x, z);
  float hx = planet.heightAt(x + d, z);
  float hz = planet.heightAt(x, z + d);
  float dx = (hx - h0) / d;
  float dz = (hz - h0) / d;
  return sqrtf(dx * dx + dz * dz);
}

// ====================================================================
// Per-kind render — kept as free functions. Geometry is intentionally
// low-poly to match the 1988 flat-shaded aesthetic.
// ====================================================================

void drawTree(Vector3 pos, float scale) {
  float trunkH = 1.2f * scale;
  float trunkW = 0.3f * scale;
  DrawCubeV({pos.x, pos.y + trunkH * 0.5f, pos.z},
            {trunkW, trunkH, trunkW}, {86, 56, 32, 255});
  float canopyH = 2.0f * scale;
  float canopyW = 1.8f * scale;
  DrawCubeV({pos.x, pos.y + trunkH + canopyH * 0.5f, pos.z},
            {canopyW, canopyH, canopyW}, {52, 110, 42, 255});
  DrawCubeV({pos.x, pos.y + trunkH + canopyH + 0.5f * scale, pos.z},
            {1.0f * scale, 1.0f * scale, 1.0f * scale},
            {64, 130, 50, 255});
}

void drawRock(const Decoration &d, const Planet &planet) {
  uint32_t rng = d.seed;
  // 4..32 boxes per outcropping — big mountain formations end up as
  // proper boulder fields. With biome-weighted scale this means
  // grassland rocks stay modest (4ish small boxes) while mountain
  // outcroppings pile dozens of mid-sized boulders together.
  int n = 4 + static_cast<int>(xorshift32(rng) % 29);
  Color stone{110, 108, 100, 255};
  Color shadow{78, 76, 70, 255};
  Color light{145, 142, 132, 255};
  // Scatter offset scales with the formation's scale (bigger
  // formations spread their boulders wider).
  float scatter = 1.2f * d.scale;
  for (int i = 0; i < n; ++i) {
    // Box sizes scale linearly with d.scale — big mountain
    // outcroppings end up with proportionally bigger boulders.
    float w = d.scale * frand(rng, 0.9f, 2.4f);
    float h = d.scale * frand(rng, 0.7f, 2.0f);
    float l = d.scale * frand(rng, 0.9f, 2.4f);
    float ox = frand(rng, -scatter, scatter);
    float oz = frand(rng, -scatter, scatter);
    float groundY = planet.heightAt(d.pos.x + ox, d.pos.z + oz);
    // Three-tone palette so adjacent boulders read as distinct.
    Color c = (i % 3 == 0) ? stone : (i % 3 == 1) ? shadow : light;
    DrawCubeV({d.pos.x + ox, groundY + h * 0.4f, d.pos.z + oz},
              {w, h, l}, c);
  }
}

void drawAntenna(const Decoration &d) {
  float h = 8.0f * d.scale;
  Vector3 base{d.pos.x, d.pos.y, d.pos.z};
  Vector3 top{d.pos.x, d.pos.y + h, d.pos.z};
  DrawCubeV({base.x, base.y + h * 0.5f, base.z},
            {0.25f * d.scale, h, 0.25f * d.scale},
            {180, 180, 195, 255});
  DrawCubeV({top.x, top.y + 0.4f * d.scale, top.z},
            {0.9f * d.scale, 0.4f * d.scale, 0.9f * d.scale},
            {200, 200, 215, 255});
  DrawCubeV({top.x, top.y + 0.95f * d.scale, top.z},
            {0.25f * d.scale, 0.3f * d.scale, 0.25f * d.scale},
            {230, 60, 60, 255});
  float gx = 1.8f * d.scale;
  DrawLine3D(top, {base.x + gx, base.y + 0.05f, base.z},
             {180, 180, 195, 200});
  DrawLine3D(top, {base.x - gx, base.y + 0.05f, base.z + gx * 0.4f},
             {180, 180, 195, 200});
}

void drawCrashSite(const Decoration &d, const Planet &planet) {
  uint32_t rng = d.seed;
  Color hull{38, 36, 34, 255};
  Color glow{180, 80, 40, 255};
  float w = 3.0f * d.scale;
  float h = 1.2f * d.scale;
  float l = 5.0f * d.scale;
  DrawCubeV({d.pos.x, d.pos.y + h * 0.4f, d.pos.z}, {w, h, l}, hull);
  int n = 4 + static_cast<int>(xorshift32(rng) % 4);
  for (int i = 0; i < n; ++i) {
    float ox = frand(rng, -3.5f, 3.5f) * d.scale;
    float oz = frand(rng, -3.5f, 3.5f) * d.scale;
    float sz = frand(rng, 0.3f, 0.9f) * d.scale;
    float gy = planet.heightAt(d.pos.x + ox, d.pos.z + oz);
    DrawCubeV({d.pos.x + ox, gy + sz * 0.3f, d.pos.z + oz},
              {sz, sz * 0.6f, sz}, hull);
  }
  // Faint orange ember inside the wreck.
  DrawCubeV({d.pos.x, d.pos.y + h * 0.3f, d.pos.z},
            {0.4f * d.scale, 0.3f * d.scale, 0.4f * d.scale}, glow);
}

// Inline helper — sample candidates until `accept` passes or we hit
// `maxAttempts`. On success, push the accepted Decoration with the
// caller-provided kind + scale randomness.
template <typename Accept>
bool sampleAndPush(std::vector<Decoration> &out,
                   const Planet &planet, uint32_t &rng,
                   float worldSize, Decoration::Kind kind,
                   float scaleLo, float scaleHi,
                   Accept accept, int maxAttempts) {
  for (int t = 0; t < maxAttempts; ++t) {
    Vector3 p{frand(rng) * worldSize, 0.0f, frand(rng) * worldSize};
    p.y = planet.heightAt(p.x, p.z);
    if (!accept(p)) continue;
    Decoration d;
    d.kind = kind;
    d.pos = p;
    d.yaw = frand(rng) * 6.2832f;
    d.scale = frand(rng, scaleLo, scaleHi);
    d.seed = xorshift32(rng);
    out.push_back(d);
    return true;
  }
  return false;
}

} // anonymous namespace

// ====================================================================
// Procedural placement
//
// Forest pass uses a NOISE MASK — the canonical approach per Red Blob
// Games + Potter Programming + the GameDev.net "trees and bushes"
// thread. Each candidate point's acceptance is gated by a low-freq
// value-noise field, with a smooth probability ramp around the cutoff
// instead of a hard threshold. Two consequences:
//   * Forests take on organic, irregular shapes (not discs) because
//     the noise field is organic
//   * Neighboring forests CONNECT naturally where the noise field's
//     high regions touch — no special "blend" logic needed
//   * Density falls off gradually at edges (the falloff zone) so
//     forests don't end with a hard line
//
// Algorithm summary:
//   * Trees       — per-point noise-mask rejection sampling. Each
//                   accepted point places ONE tree (was: clusters).
//                   Biome + slope gates still apply.
//   * Rocks       — biome-weighted acceptance, mountain-heavy.
//   * Antennas    — flat ground, dense enough to encounter.
//   * Crash sites — anywhere on land, gentle slope.
// ====================================================================
void Decorations::generate(const Planet &planet, uint32_t seed) {
  m_items.clear();
  uint32_t rng = seed ? seed : 0x9e3779b9u;
  float worldSize = planet.worldSize();
  float seaY = Config::SEA_LEVEL * Config::TERRAIN_HEIGHT_MAX;

  // ---- Forests: noise-mask placement + per-cell dense packing ----
  // Two-stage approach:
  //   (a) Sample N candidates; gate each on biome / slope / noise so
  //       only forest-core points survive. This defines the forest
  //       SHAPE (organic, biome-respecting, irregular boundary).
  //   (b) Every surviving candidate fans out into kFanout sub-trees
  //       with small jitter, scaling density inside forests by
  //       kFanout× without re-running the expensive noise/biome
  //       lookups. This is where "can't see the ground" density
  //       comes from.
  //
  // Math at the current numbers:
  //   * 350k candidates × 30% grass × 80% slope × ~20% noise pass =
  //     ~17k surviving cores × 8 fanout = ~135k trees in forest
  //     blobs. Spacing inside cores ≈ 1.8m → canopies fully overlap
  //     (canopy is 1.8m wide).
  constexpr int kTreeAttempts = 350000;
  constexpr int kFanout = 8;
  constexpr float kFanoutRadius = 2.4f; // small jitter so canopies overlap
  // Noise cutoff: above kNoiseLow we're in a forest blob; ramp up
  // to kNoiseHigh gives a soft edge. Both above 0.5 (the noise
  // mean) so MOST of grassland is open — only the noise peaks
  // become forest.
  constexpr float kNoiseLow  = 0.56f;
  constexpr float kNoiseHigh = 0.64f;
  uint32_t forestSeed = rng ^ 0xb5297a4du;
  for (int i = 0; i < kTreeAttempts; ++i) {
    float tx = frand(rng) * worldSize;
    float tz = frand(rng) * worldSize;
    float b = landBiome(planet, tx, tz);
    if (b < kSandMax || b > kGrassMax) continue;     // grassland only
    if (slopeAt(planet, tx, tz) > 0.7f) continue;     // skip steep
    float n = forestMask(tx, tz, forestSeed);
    if (n < kNoiseLow) continue;                      // outside forest
    // Probability ramp inside the falloff zone — smooth boundary.
    float p = (n - kNoiseLow) / (kNoiseHigh - kNoiseLow);
    if (p > 1.0f) p = 1.0f;
    if (frand(rng) > p) continue;
    // Fan out into kFanout sub-trees. Each sub-tree re-checks the
    // biome cheaply (no slope, no noise) so canopies don't spill
    // off the green band — they just stop at it.
    for (int j = 0; j < kFanout; ++j) {
      float jx = tx + frand(rng, -kFanoutRadius, kFanoutRadius);
      float jz = tz + frand(rng, -kFanoutRadius, kFanoutRadius);
      float jb = landBiome(planet, jx, jz);
      if (jb < kSandMax || jb > kGrassMax) continue;
      Decoration d;
      d.kind = Decoration::Kind::Tree;
      d.pos = {jx, planet.heightAt(jx, jz), jz};
      d.yaw = frand(rng) * 6.2832f;
      d.scale = frand(rng, 0.85f, 1.25f);
      d.seed = xorshift32(rng);
      m_items.push_back(d);
    }
  }

  // Density targets for the remaining kinds.
  constexpr int kRocks = 240;
  constexpr int kAntennas = 28;
  constexpr int kCrashSites = 10;

  // ---- Rocks: biome-weighted single-pass ----
  // Acceptance probability climbs with the biome band so rocks visibly
  // concentrate on the brown mountain mass.
  auto rockProbability = [](float b) -> float {
    if (b < 0.0f) return 0.0f;              // water
    if (b < kSandMax)  return 0.05f;         // beach
    if (b < kGrassMax) return 0.25f;         // grassland — sprinkle
    if (b < kRockMax)  return 1.00f;         // mountain brown — dense
    if (b < kHighMax)  return 0.90f;         // high rock — dense
    return 0.20f;                            // snow — rare
  };
  // Biome-weighted size — mountain outcroppings end up substantially
  // larger than grassland sprinkles, so the brown rock band reads as
  // a "rocky mountain" mass rather than scattered pebbles.
  auto rockScaleFor = [&](float b) -> float {
    if (b < kGrassMax)      return frand(rng, 0.7f, 1.0f); // grassland — small
    if (b < kRockMax)       return frand(rng, 1.8f, 3.0f); // mountain — large
    if (b < kHighMax)       return frand(rng, 1.5f, 2.5f); // high rock — medium-large
    return frand(rng, 1.0f, 1.6f);                         // snow — medium
  };
  int rocksPlaced = 0, rockAttempts = 0;
  while (rocksPlaced < kRocks && rockAttempts < kRocks * 30) {
    ++rockAttempts;
    Vector3 p{frand(rng) * worldSize, 0.0f, frand(rng) * worldSize};
    float b = landBiome(planet, p.x, p.z);
    if (b < 0.0f) continue;
    if (frand(rng) > rockProbability(b)) continue;
    p.y = planet.heightAt(p.x, p.z);
    Decoration d;
    d.kind = Decoration::Kind::Rock;
    d.pos = p;
    d.yaw = frand(rng) * 6.2832f;
    d.scale = rockScaleFor(b);
    d.seed = xorshift32(rng);
    m_items.push_back(d);
    ++rocksPlaced;
  }

  // ---- Antennas: flat ground, denser than before ----
  // Player needs to actually encounter these — bumped from 8 to 28
  // and loosened the slope cap. Still elevation-gated above sea+4.
  for (int i = 0; i < kAntennas; ++i) {
    sampleAndPush(m_items, planet, rng, worldSize,
                  Decoration::Kind::Antenna, 0.9f, 1.3f,
                  [&](Vector3 p) {
                    if (!isLand(planet, p.x, p.z)) return false;
                    if (slopeAt(planet, p.x, p.z) > 0.25f) return false;
                    if (p.y < seaY + 4.0f) return false;
                    return true;
                  },
                  60);
  }

  // ---- Crash sites: anywhere on land, gentle slope ----
  for (int i = 0; i < kCrashSites; ++i) {
    sampleAndPush(m_items, planet, rng, worldSize,
                  Decoration::Kind::CrashSite, 1.2f, 1.7f,
                  [&](Vector3 p) {
                    if (!isLand(planet, p.x, p.z)) return false;
                    if (slopeAt(planet, p.x, p.z) > 0.30f) return false;
                    return true;
                  },
                  40);
  }
}

// ====================================================================
// Render — distance-culled. raylib's DrawCube doesn't frustum-cull,
// so we do a cheap radial check before each call.
// ====================================================================
void Decorations::render(const Planet &planet, Vector3 cameraPos) const {
  constexpr float kRenderRadius = 800.0f;
  constexpr float kRenderRadius2 = kRenderRadius * kRenderRadius;

  for (const Decoration &d : m_items) {
    float dx = d.pos.x - cameraPos.x;
    float dz = d.pos.z - cameraPos.z;
    if (dx * dx + dz * dz > kRenderRadius2) continue;
    switch (d.kind) {
    case Decoration::Kind::Tree:      drawTree(d.pos, d.scale);   break;
    case Decoration::Kind::Rock:      drawRock(d, planet);        break;
    case Decoration::Kind::Antenna:   drawAntenna(d);             break;
    case Decoration::Kind::CrashSite: drawCrashSite(d, planet);   break;
    }
  }
}
