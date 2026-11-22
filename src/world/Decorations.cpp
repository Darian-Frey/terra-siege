#include "Decorations.hpp"

#include "FieldMap.hpp"
#include "Planet.hpp"
#include "core/Config.hpp"
#include "mesh/ObjLoader.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
constexpr float kSandMax  = Config::BIOME_SAND_MAX;
constexpr float kGrassMax = Config::BIOME_GRASS_MAX;
constexpr float kRockMax  = Config::BIOME_ROCK_MAX;
constexpr float kHighMax  = Config::BIOME_HIGH_MAX;

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

// Unlit instancing shader: the per-instance model matrix comes in as a
// vertex attribute; colour is the baked palette vertex colour, exactly
// what raylib's default shader gives the other OBJ models.
const char *kInstVs = R"(#version 330
in vec3 vertexPosition;
in vec4 vertexColor;
in mat4 instanceTransform;
uniform mat4 mvp;
uniform vec4 colDiffuse;
out vec4 fragColor;
void main() {
  fragColor = vertexColor * colDiffuse;
  gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
)";
const char *kInstFs = R"(#version 330
in vec4 fragColor;
out vec4 finalColor;
void main() { finalColor = fragColor; }
)";

const char *const kModelFiles[] = {"tree_conifer.obj", "tree_broadleaf.obj",
                                   "tree_poplar.obj",  "tree_birch.obj",
                                   "bush.obj",         "tree_dead.obj",
                                   "grove_conifer_a.obj",   "grove_conifer_b.obj",
                                   "grove_broadleaf_a.obj", "grove_broadleaf_b.obj",
                                   "grove_birch.obj",       "grove_mixed.obj",
                                   "copse_conifer.obj",     "copse_broadleaf.obj",
                                   "copse_birch.obj",       "copse_mixed.obj",
                                   "hedge_a.obj",           "hedge_b.obj",
                                   "hedge_tree.obj",        "hedge_birch.obj",
                                   "wall_a.obj",            "wall_b.obj",
                                   "fence_a.obj",           "fence_b.obj",
                                   "rock_a.obj",       "rock_b.obj",
                                   "rock_c.obj",       "antenna.obj",
                                   "crash_site.obj",   "crash_debris.obj"};

// Scale, then yaw, then translate — the same order as DrawModelEx.
Matrix placeMatrix(Vector3 pos, Vector3 scale, float yaw) {
  return MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z),
                                       MatrixRotateY(yaw)),
                        MatrixTranslate(pos.x, pos.y, pos.z));
}

float wrapCoord(float v, float worldSize) {
  v = fmodf(v, worldSize);
  return v < 0.0f ? v + worldSize : v;
}

int wrapIndex(int i, int n) { return ((i % n) + n) % n; }

} // anonymous namespace

// ====================================================================
// Procedural placement
//
// Trees follow the planet's FieldMap (farmland patchwork + forest mask):
//   * Groves      — whole stands (one model each) on a jittered grid
//                   over deep forest and wood fields, species by stand.
//   * Copses      — small clumps on a finer grid over thinner forest.
//   * Forests     — loose trees fill the ground groves and copses leave,
//                   up to FOREST_TREES_PER_CELL per cell, thinning into
//                   bushes and birches at the fringe and in gaps ringed
//                   by stands. Farmland stays clear, so woods stop at
//                   the field edge.
//   * Boundaries  — every run of edge between two fields with farmland
//                   on either side: hedge, tree-line, wall or fence
//                   segments laid end to end, poplar rows, or open, with
//                   gateways and fenced patches in hedges.
//   * Rocks       — biome-weighted acceptance, mountain-heavy.
//   * Antennas    — flat ground, dense enough to encounter.
//   * Crash sites — anywhere on land, gentle slope, ringed by dead trees.
// ====================================================================
void Decorations::clearSquare(const Planet &planet, float x, float z,
                              float half, float reseatHalf) {
  const Vector3 c{x, 0.0f, z};
  size_t w = 0;
  for (size_t i = 0; i < m_items.size(); ++i) {
    Decoration &d = m_items[i];
    Vector3 p = planet.nearestCopy(d.pos, c);
    float ax = fabsf(p.x - x), az = fabsf(p.z - z);
    // Stands reach out from their centre.
    float reach = 0.0f;
    if (d.kind == Decoration::Kind::Tree && Decoration::isStand(d.tree))
      reach = (Decoration::isGrove(d.tree)   ? Config::GROVE_RADIUS
               : Decoration::isSegment(d.tree) ? 0.5f * Config::HEDGE_SEGMENT * d.stretch
                                             : Config::COPSE_RADIUS) *
              d.scale;
    if (ax <= half + reach && az <= half + reach) continue;
    if (ax <= reseatHalf && az <= reseatHalf)
      d.pos.y = planet.heightAt(d.pos.x, d.pos.z);
    m_items[w++] = d;
  }
  m_items.resize(w);
  buildInstances(planet);
}

void Decorations::generate(const Planet &planet, uint32_t seed) {
  m_items.clear();
  uint32_t rng = seed ? seed : 0x9e3779b9u;
  float worldSize = planet.worldSize();
  float seaY = Config::SEA_LEVEL * Config::TERRAIN_HEIGHT_MAX;

  const FieldMap &fields = planet.fields();
  const float cellSize = Config::TERRAIN_SCALE;
  const int cells = static_cast<int>(worldSize / cellSize);
  using TM = Decoration::TreeModel;
  auto inGrass = [&](float x, float z) {
    float b = landBiome(planet, x, z);
    return b >= kSandMax && b < kGrassMax && !planet.isWater(x, z);
  };
  auto pushTree = [&](TM model, float x, float z, float scaleLo, float scaleHi) {
    Decoration d;
    d.kind = Decoration::Kind::Tree;
    d.tree = model;
    d.pos = {x, planet.heightAt(x, z), z};
    d.yaw = frand(rng) * 6.2832f;
    d.scale = frand(rng, scaleLo, scaleHi);
    d.seed = xorshift32(rng);
    m_items.push_back(d);
  };
  // Stand species from the slow stand noise, so neighbouring trees and
  // groves agree; conifers commoner towards the top of the grass band.
  enum class Stand { Conifer, Mixed, Broadleaf, Birch };
  auto standAt = [&](float x, float z) {
    float t = (landBiome(planet, x, z) - kSandMax) / (kGrassMax - kSandMax);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float conifer = Config::DECOR_CONIFER_FRAC_LOW +
                    (Config::DECOR_CONIFER_FRAC_HIGH - Config::DECOR_CONIFER_FRAC_LOW) * t;
    float v = fields.standNoise(x, z);
    if (v > 1.0f - Config::FOREST_BIRCH_STANDS) return Stand::Birch;
    if (v < conifer - Config::FOREST_MIXED_BAND) return Stand::Conifer;
    if (v < conifer + Config::FOREST_MIXED_BAND) return Stand::Mixed;
    return Stand::Broadleaf;
  };
  auto woodlandTree = [&](float x, float z) -> TM {
    float r = frand(rng);
    if (r < Config::DECOR_DEAD_TREE_CHANCE) return TM::Dead;
    if (r < Config::DECOR_DEAD_TREE_CHANCE + Config::FOREST_BIRCH_FRAC) return TM::Birch;
    switch (standAt(x, z)) {
    case Stand::Conifer: return TM::Conifer;
    case Stand::Broadleaf: return TM::Broadleaf;
    case Stand::Birch: return frand(rng) < 0.7f ? TM::Birch : TM::Broadleaf;
    case Stand::Mixed: break;
    }
    return frand(rng) < 0.5f ? TM::Conifer : TM::Broadleaf;
  };
  auto groveFor = [&](float x, float z) -> TM {
    const bool b = frand(rng) < 0.5f;
    switch (standAt(x, z)) {
    case Stand::Conifer: return b ? TM::GroveConiferA : TM::GroveConiferB;
    case Stand::Broadleaf: return b ? TM::GroveBroadleafA : TM::GroveBroadleafB;
    case Stand::Birch: return TM::GroveBirch;
    case Stand::Mixed: break;
    }
    return TM::GroveMixed;
  };
  // Deep enough forest for a grove: GROVE_MIN_DENSITY of the way up the
  // mask's density ramp, or in a wood field.
  const float groveMask =
      Config::FOREST_NOISE_LOW +
      Config::GROVE_MIN_DENSITY * (Config::FOREST_NOISE_HIGH - Config::FOREST_NOISE_LOW);
  auto deepForest = [&](float x, float z) {
    const int f = fields.fieldAt(x, z);
    if (fields.isFarmland(f)) return false;
    return fields.isWood(f) || fields.forestMask(x, z) >= groveMask;
  };

  // ---- Groves: whole stands filling the deep forest ----
  // A jittered grid of grove spots; a spot is used when its centre and a
  // ring most of the way to the grove's edge are all deep forest on grass, and the ground
  // is gentle enough to shear the stand onto. Covered ground is marked
  // on a coarse grid so the loose-tree pass leaves it alone.
  constexpr float kCover = 4.0f; // coverage grid, m
  const int coverN = static_cast<int>(worldSize / kCover);
  std::vector<uint8_t> covered(static_cast<size_t>(coverN) * coverN, 0);
  auto coverIdx = [&](float x, float z) {
    int cx = static_cast<int>(floorf(x / kCover)) % coverN;
    int cz = static_cast<int>(floorf(z / kCover)) % coverN;
    if (cx < 0) cx += coverN;
    if (cz < 0) cz += coverN;
    return static_cast<size_t>(cz) * coverN + cx;
  };
  const int groveN = static_cast<int>(worldSize / Config::GROVE_SPACING);
  const float groveStep = worldSize / groveN; // tiles the world exactly
  for (int gz = 0; gz < groveN; ++gz) {
    for (int gx = 0; gx < groveN; ++gx) {
      float x = (gx + 0.5f) * groveStep + frand(rng, -Config::GROVE_JITTER, Config::GROVE_JITTER);
      float z = (gz + 0.5f) * groveStep + frand(rng, -Config::GROVE_JITTER, Config::GROVE_JITTER);
      bool ok = deepForest(x, z) && inGrass(x, z) && slopeAt(planet, x, z) <= Config::GROVE_MAX_SLOPE;
      for (int k = 0; k < 8 && ok; ++k) {
        float a = k * 0.785398f;
        const float rr = Config::GROVE_RING_FRAC * Config::GROVE_RADIUS;
        float rx = x + cosf(a) * rr, rz = z + sinf(a) * rr;
        ok = deepForest(rx, rz) && inGrass(rx, rz);
      }
      if (!ok) continue;
      pushTree(groveFor(x, z), x, z, 0.9f, 1.1f);
      const float r = Config::GROVE_COVER_RADIUS * m_items.back().scale;
      for (float oz = -r; oz <= r; oz += kCover)
        for (float ox = -r; ox <= r; ox += kCover)
          if (ox * ox + oz * oz <= r * r) covered[coverIdx(x + ox, z + oz)] = 1;
    }
  }

  // ---- Copses: clumps in the thinner forest round the groves ----
  // Same idea on a finer grid: forest density at least COPSE_MIN_DENSITY
  // at the centre and on a ring near the edge, centre not under a grove.
  // Thin forest leans to birch-and-bush copses, like the old fringe.
  auto density = [&](float x, float z) {
    const int f = fields.fieldAt(x, z);
    if (fields.isFarmland(f)) return -1.0f;
    if (fields.isWood(f)) return 1.0f;
    float t = (fields.forestMask(x, z) - Config::FOREST_NOISE_LOW) /
              (Config::FOREST_NOISE_HIGH - Config::FOREST_NOISE_LOW);
    return t > 1.0f ? 1.0f : t;
  };
  const int copseN = static_cast<int>(worldSize / Config::COPSE_SPACING);
  const float copseStep = worldSize / copseN;
  for (int gz = 0; gz < copseN; ++gz) {
    for (int gx = 0; gx < copseN; ++gx) {
      float x = (gx + 0.5f) * copseStep + frand(rng, -Config::COPSE_JITTER, Config::COPSE_JITTER);
      float z = (gz + 0.5f) * copseStep + frand(rng, -Config::COPSE_JITTER, Config::COPSE_JITTER);
      if (covered[coverIdx(x, z)]) continue;
      const float t = density(x, z);
      bool ok = t >= Config::COPSE_MIN_DENSITY && inGrass(x, z) &&
                slopeAt(planet, x, z) <= Config::COPSE_MAX_SLOPE;
      for (int k = 0; k < 6 && ok; ++k) {
        float a = k * 1.0471976f, rr = 0.75f * Config::COPSE_RADIUS;
        float rx = x + cosf(a) * rr, rz = z + sinf(a) * rr;
        ok = density(rx, rz) >= Config::COPSE_MIN_DENSITY && inGrass(rx, rz);
      }
      if (!ok) continue;
      TM model;
      const float fringe = 1.0f - t;
      if (frand(rng) < (Config::FOREST_FRINGE_BUSH + Config::FOREST_FRINGE_BIRCH) * fringe) {
        model = TM::CopseBirch;
      } else {
        switch (standAt(x, z)) {
        case Stand::Conifer: model = TM::CopseConifer; break;
        case Stand::Broadleaf: model = TM::CopseBroadleaf; break;
        case Stand::Birch: model = TM::CopseBirch; break;
        default: model = TM::CopseMixed; break;
        }
      }
      pushTree(model, x, z, 0.9f, 1.15f);
      const float r = Config::COPSE_COVER_RADIUS * m_items.back().scale;
      for (float oz = -r; oz <= r; oz += kCover)
        for (float ox = -r; ox <= r; ox += kCover)
          if (ox * ox + oz * oz <= r * r) covered[coverIdx(x + ox, z + oz)] = 1;
    }
  }

  // ---- Forests: every terrain cell inside the forest mask ----
  // Density ramps from zero at FOREST_NOISE_LOW to FOREST_TREES_PER_CELL
  // at FOREST_NOISE_HIGH, so the interior closes into a canopy and the
  // fringe thins out into bushes and birches. Farmland cells stay clear,
  // so where a wood meets a field it stops at the field's hedge; wood
  // fields in farming country are planted solid to their edges.
  for (int cz = 0; cz < cells; ++cz) {
    for (int cx = 0; cx < cells; ++cx) {
      const float x0 = cx * cellSize, z0 = cz * cellSize;
      const float xc = x0 + 0.5f * cellSize, zc = z0 + 0.5f * cellSize;
      // A cell can straddle field edges: it is worth filling if the mask
      // says forest or any corner lies in a wood field.
      bool anyWood = false;
      for (int k = 0; k < 4 && !anyWood; ++k)
        anyWood = fields.isWood(fields.fieldAt(x0 + (k & 1) * cellSize,
                                               z0 + (k >> 1) * cellSize));
      const float m = fields.forestMask(xc, zc);
      if (m < Config::FOREST_NOISE_LOW && !anyWood) continue;
      if (slopeAt(planet, xc, zc) > 0.7f) continue;
      float t = (m - Config::FOREST_NOISE_LOW) /
                (Config::FOREST_NOISE_HIGH - Config::FOREST_NOISE_LOW);
      t = t > 1.0f ? 1.0f : t;
      const float tCell = anyWood ? 1.0f : t;
      int n = static_cast<int>(Config::FOREST_TREES_PER_CELL * (0.25f + 0.75f * tCell) +
                               frand(rng));
      for (int i = 0; i < n; ++i) {
        float x = x0 + frand(rng) * cellSize, z = z0 + frand(rng) * cellSize;
        if (covered[coverIdx(x, z)]) continue; // inside a grove or copse
        {
          // A gap mostly ringed by stands is largely under their crowns.
          int ring = 0;
          for (int k = 0; k < 9; ++k)
            if (k != 4)
              ring += covered[coverIdx(x + (k % 3 - 1) * kCover, z + (k / 3 - 1) * kCover)];
          if (ring >= 5 && frand(rng) > Config::FOREST_GAP_KEEP) continue;
        }
        const int field = fields.fieldAt(x, z);
        if (fields.isFarmland(field)) continue;
        const bool wood = fields.isWood(field);
        if (!wood && m < Config::FOREST_NOISE_LOW) continue;
        if (!inGrass(x, z)) continue;
        const float fringe = wood ? 0.0f : 1.0f - t;
        float r = frand(rng);
        if (r < Config::FOREST_FRINGE_BUSH * fringe)
          pushTree(TM::Bush, x, z, 0.8f, 1.3f);
        else if (r < (Config::FOREST_FRINGE_BUSH + Config::FOREST_FRINGE_BIRCH) * fringe)
          pushTree(TM::Birch, x, z, 0.85f, 1.2f);
        else
          pushTree(woodlandTree(x, z), x, z, 0.85f, 1.3f);
      }
    }
  }

  // ---- Hedgerows along farmland field edges ----
  // One style per boundary (hashed from the two fields it divides):
  // open, a bush hedge with the odd tree, a broadleaf tree line over
  // bushes, or a row of poplars. Short gaps read as gateways.
  // One run of boundary between the same two fields, as a polyline
  // (straight, or following a curved edge). Hedge segments are laid end
  // to end along it by arc length, each spanning the chord of its piece,
  // so a whole edge becomes as few segments as possible.
  std::vector<FieldMap::Point> run;
  auto pointAt = [&](float arc) {
    for (size_t k = 0; k + 1 < run.size(); ++k) {
      const float ex = run[k + 1].x - run[k].x, ez = run[k + 1].z - run[k].z;
      const float l = sqrtf(ex * ex + ez * ez);
      if (arc <= l || k + 2 == run.size()) {
        const float t = l > 0.0f ? fminf(arc / l, 1.0f) : 0.0f;
        return FieldMap::Point{run[k].x + ex * t, run[k].z + ez * t};
      }
      arc -= l;
    }
    return run.back();
  };
  auto hedgeRun = [&](uint32_t key) {
    if (run.size() < 2) return;
    float len = 0.0f;
    for (size_t k = 0; k + 1 < run.size(); ++k)
      len += sqrtf((run[k + 1].x - run[k].x) * (run[k + 1].x - run[k].x) +
                   (run[k + 1].z - run[k].z) * (run[k + 1].z - run[k].z));
    if (len < 0.5f) return;
    float style = (key & 0xffffu) / 65536.0f;
    if (style < Config::HEDGE_OPEN_SHARE) return;
    style -= Config::HEDGE_OPEN_SHARE;
    enum { Bushes, TreeLine, Poplars, Wall, Fence } kind = Fence;
    {
      const float shares[] = {Config::HEDGE_BUSH_SHARE, Config::HEDGE_TREELINE_SHARE,
                              Config::HEDGE_POPLAR_SHARE, Config::HEDGE_WALL_SHARE};
      float acc = 0.0f;
      for (int i = 0; i < 4; ++i) {
        acc += shares[i];
        if (style < acc) {
          kind = static_cast<decltype(kind)>(i);
          break;
        }
      }
    }
    const bool wallB = (key >> 20) & 1u, fenceB = (key >> 21) & 1u;
    if (kind == Poplars) {
      for (float arc = frand(rng, 0.0f, 4.0f); arc < len; arc += 4.0f) {
        if (frand(rng) < Config::HEDGE_GAP_CHANCE * 0.5f) continue; // gateway
        FieldMap::Point p = pointAt(arc);
        float x = p.x + frand(rng, -0.2f, 0.2f), z = p.z + frand(rng, -0.2f, 0.2f);
        if (!inGrass(x, z) || slopeAt(planet, x, z) > 0.7f) continue;
        pushTree(frand(rng) < Config::DECOR_DEAD_TREE_CHANCE ? TM::Dead : TM::Poplar, x, z,
                 0.9f, 1.1f);
      }
      return;
    }
    const int n = std::max(1, static_cast<int>(lroundf(len / Config::HEDGE_SEGMENT)));
    const float segArc = len / n;
    int fencePatch = 0; // segments left in a fenced patch of a hedge
    for (int i = 0; i < n; ++i) {
      if (frand(rng) < Config::HEDGE_GAP_CHANCE) continue; // gateway
      const FieldMap::Point p0 = pointAt(i * segArc), p1 = pointAt((i + 1) * segArc);
      const float dx = p1.x - p0.x, dz = p1.z - p0.z, chord = sqrtf(dx * dx + dz * dz);
      if (chord < 0.3f) continue;
      const float x = 0.5f * (p0.x + p1.x), z = 0.5f * (p0.z + p1.z);
      if (!inGrass(x, z) || slopeAt(planet, x, z) > 0.7f) continue;
      TM model;
      if ((kind == Bushes || kind == TreeLine) && fencePatch == 0 &&
          frand(rng) < Config::HEDGE_FENCE_PATCH)
        fencePatch = 1 + static_cast<int>(xorshift32(rng) % 3);
      if (kind == Wall)
        model = wallB ? TM::WallB : TM::WallA;
      else if (kind == Fence || fencePatch > 0)
        model = fenceB ? TM::FenceB : TM::FenceA;
      else if (kind == TreeLine)
        model = frand(rng) < 0.85f ? TM::HedgeTree : TM::HedgeBirch;
      else if (frand(rng) < Config::HEDGE_TREE_SHARE)
        model = frand(rng) < 0.7f ? TM::HedgeTree : TM::HedgeBirch;
      else
        model = frand(rng) < 0.5f ? TM::HedgeA : TM::HedgeB;
      if (fencePatch > 0) --fencePatch;
      // Walls and fences keep one size so neighbouring segments line up.
      const bool built = kind == Wall || model == TM::FenceA || model == TM::FenceB;
      pushTree(model, x, z, built ? 1.0f : 0.95f, built ? 1.0f : 1.1f);
      Decoration &d = m_items.back();
      d.yaw = atan2f(dx, dz) + (frand(rng) < 0.5f ? 0.0f : 3.14159265f);
      d.stretch = chord / (Config::HEDGE_SEGMENT * d.scale);
    }
  };
  auto pairKey = [&](int f0, int f1) {
    uint32_t a = fields.hash(f0 < f1 ? f0 : f1), b = fields.hash(f0 < f1 ? f1 : f0);
    return a ^ (b * 0x85ebca6bu) ^ (b >> 13);
  };
  // Walk every field's outline in stretches of up to a cell, and look
  // just outside each stretch for the neighbouring field. Consecutive
  // stretches with the same neighbour join into one run. The lower-
  // numbered field of each pair plants the hedge, so each boundary is
  // planted once even where several fields meet it from the other side.
  for (int f = 0; f < fields.fieldCount(); ++f) {
    const auto &poly = fields.outline(f);
    // Winding sign: outward is (ez, -ex) for a positive signed area. (A
    // curved edge can make the outline non-convex, so the centroid is no
    // guide.)
    float area2 = 0.0f;
    for (size_t k = 0; k < poly.size(); ++k) {
      const auto a = poly[k], b = poly[(k + 1) % poly.size()];
      area2 += a.x * b.z - b.x * a.z;
    }
    const float wind = area2 >= 0.0f ? 1.0f : -1.0f;
    int runWith = -1;
    run.clear();
    for (size_t k = 0; k < poly.size(); ++k) {
      const auto a = poly[k], b = poly[(k + 1) % poly.size()];
      const float ex = b.x - a.x, ez = b.z - a.z, len = sqrtf(ex * ex + ez * ez);
      if (len < 0.01f) continue;
      const float nx = wind * ez / len, nz = -wind * ex / len; // outward
      const int steps = static_cast<int>(ceilf(len / cellSize));
      for (int i = 0; i < steps; ++i) {
        const float t0 = static_cast<float>(i) / steps, t1 = static_cast<float>(i + 1) / steps;
        const float mx = a.x + ex * (t0 + t1) * 0.5f, mz = a.z + ez * (t0 + t1) * 0.5f;
        int g = fields.fieldAt(mx + nx * 0.5f, mz + nz * 0.5f);
        if (g <= f || (!fields.isFarmland(f) && !fields.isFarmland(g))) g = -1;
        if (g != runWith) {
          if (runWith >= 0) hedgeRun(pairKey(f, runWith));
          run.clear();
          runWith = g;
          if (g >= 0) run.push_back({a.x + ex * t0, a.z + ez * t0});
        }
        if (g >= 0) run.push_back({a.x + ex * t1, a.z + ez * t1});
      }
    }
    if (runWith >= 0) hedgeRun(pairKey(f, runWith));
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
  // A few dead trees round each wreck.
  const size_t itemCount = m_items.size();
  for (size_t i = 0; i < itemCount; ++i) {
    if (m_items[i].kind != Decoration::Kind::CrashSite) continue;
    const Vector3 c = m_items[i].pos;
    for (int k = 0; k < 3; ++k) {
      float a = frand(rng) * 6.2832f, r = frand(rng, 7.0f, 14.0f);
      float x = c.x + cosf(a) * r, z = c.z + sinf(a) * r;
      if (isLand(planet, x, z)) pushTree(TM::Dead, x, z, 0.9f, 1.3f);
    }
  }
#ifdef DEV_MODE
  size_t perTree[24] = {};
  for (const Decoration &d : m_items)
    if (d.kind == Decoration::Kind::Tree) ++perTree[static_cast<int>(d.tree)];
  std::fprintf(stderr,
               "[Decorations] trees: %zu conifer, %zu broadleaf, %zu poplar, "
               "%zu birch, %zu bush, %zu dead; groves: %zu; copses: %zu; hedge "
               "segments: %zu; walls: %zu; fences: %zu (%zu items)\n",
               perTree[0], perTree[1], perTree[2], perTree[3], perTree[4], perTree[5],
               perTree[6] + perTree[7] + perTree[8] + perTree[9] + perTree[10] + perTree[11],
               perTree[12] + perTree[13] + perTree[14] + perTree[15],
               perTree[16] + perTree[17] + perTree[18] + perTree[19],
               perTree[20] + perTree[21], perTree[22] + perTree[23], m_items.size());
#endif
  buildInstances(planet);
}

// ====================================================================
// Instanced trees and rocks
// ====================================================================
void Decorations::loadModels(const std::filesystem::path &meshesDir) {
  for (int m = 0; m < kModelCount; ++m) {
    std::filesystem::path p = meshesDir / kModelFiles[m];
    m_modelLoaded[m] = false;
    if (!std::filesystem::exists(p)) {
      std::fprintf(stderr, "[Decorations] %s not found, using boxes\n",
                   p.string().c_str());
      continue;
    }
    m_models[m] = tsmesh::loadModel(p);
    m_modelLoaded[m] = m_models[m].meshCount > 0;
  }
  m_instShader = LoadShaderFromMemory(kInstVs, kInstFs);
  m_instReady = m_instShader.id != 0 && m_instShader.id != rlGetShaderIdDefault();
  if (!m_instReady) {
    std::fprintf(stderr, "[Decorations] instancing shader failed, using boxes\n");
    return;
  }
  m_instShader.locs[SHADER_LOC_MATRIX_MODEL] =
      GetShaderLocationAttrib(m_instShader, "instanceTransform");
  m_instMaterial = LoadMaterialDefault();
  m_instMaterial.shader = m_instShader;
}

void Decorations::unloadModels() {
  for (int m = 0; m < kModelCount; ++m) {
    if (m_modelLoaded[m]) UnloadModel(m_models[m]);
    m_modelLoaded[m] = false;
  }
  if (m_instReady) {
    UnloadShader(m_instShader);
    MemFree(m_instMaterial.maps); // LoadMaterialDefault's map array
    m_instReady = false;
  }
}

static_assert(static_cast<int>(Decoration::Kind::CrashSite) + 1 == 4,
              "Decorations::kKindCount must match Decoration::Kind");

std::array<bool, Decorations::kKindCount> Decorations::instancedKinds() const {
  std::array<bool, kKindCount> ok;
  ok.fill(m_instReady);
  for (int m = 0; m < kModelCount; ++m)
    if (!m_modelLoaded[m]) ok[static_cast<int>(kSlotKind[m])] = false;
  return ok;
}

void Decorations::buildInstances(const Planet &planet) {
  const float ws = planet.worldSize();
  const float cell = Config::DECOR_CELL_SIZE;
  m_gridN = static_cast<int>(ws / cell);
  const int cells = m_gridN * m_gridN;

  struct Placed {
    uint32_t cell;
    Matrix m;
  };
  std::array<std::vector<Placed>, kModelCount> placed;
  auto add = [&](int model, Vector3 pos, Vector3 scale, float yaw) {
    pos.x = wrapCoord(pos.x, ws);
    pos.z = wrapCoord(pos.z, ws);
    int cx = static_cast<int>(pos.x / cell), cz = static_cast<int>(pos.z / cell);
    if (cx >= m_gridN) cx = m_gridN - 1;
    if (cz >= m_gridN) cz = m_gridN - 1;
    placed[model].push_back({static_cast<uint32_t>(cz * m_gridN + cx),
                             placeMatrix(pos, scale, yaw)});
  };

  for (const Decoration &d : m_items) {
    if (d.kind == Decoration::Kind::Tree && Decoration::isStand(d.tree)) {
      // Shear the stand onto the local ground plane: trees stay upright,
      // their feet follow the slope.
      const float e = 0.75f * Config::GROVE_RADIUS;
      const float sx = (planet.heightAt(d.pos.x + e, d.pos.z) -
                        planet.heightAt(d.pos.x - e, d.pos.z)) / (2.0f * e);
      const float sz = (planet.heightAt(d.pos.x, d.pos.z + e) -
                        planet.heightAt(d.pos.x, d.pos.z - e)) / (2.0f * e);
      Matrix shear = MatrixIdentity();
      shear.m1 = sx;
      shear.m9 = sz;
      Vector3 pos = d.pos;
      pos.x = wrapCoord(pos.x, ws);
      pos.z = wrapCoord(pos.z, ws);
      int cx = static_cast<int>(pos.x / cell), cz = static_cast<int>(pos.z / cell);
      if (cx >= m_gridN) cx = m_gridN - 1;
      if (cz >= m_gridN) cz = m_gridN - 1;
      Matrix m = MatrixMultiply(
          MatrixMultiply(MatrixMultiply(MatrixScale(d.scale, d.scale, d.scale * d.stretch),
                                        MatrixRotateY(d.yaw)),
                         shear),
          MatrixTranslate(pos.x, pos.y, pos.z));
      placed[kConifer + static_cast<int>(d.tree)].push_back(
          {static_cast<uint32_t>(cz * m_gridN + cx), m});
    } else if (d.kind == Decoration::Kind::Tree) {
      add(kConifer + static_cast<int>(d.tree), d.pos, {d.scale, d.scale, d.scale},
          d.yaw);
    } else if (d.kind == Decoration::Kind::Rock) {
      // Same cluster recipe (and RNG sequence) as drawRock(), with each
      // box replaced by a boulder of the same size, plus a random yaw.
      uint32_t rng = d.seed;
      int n = 4 + static_cast<int>(xorshift32(rng) % 29);
      float scatter = 1.2f * d.scale;
      for (int i = 0; i < n; ++i) {
        float w = d.scale * frand(rng, 0.9f, 2.4f);
        float h = d.scale * frand(rng, 0.7f, 2.0f);
        float l = d.scale * frand(rng, 0.9f, 2.4f);
        float ox = frand(rng, -scatter, scatter);
        float oz = frand(rng, -scatter, scatter);
        float yaw = frand(rng) * 6.2832f;
        Vector3 p{d.pos.x + ox, 0.0f, d.pos.z + oz};
        p.y = planet.heightAt(p.x, p.z);
        add(kFirstRock + i % kRockVariants, p, {w, h, l}, yaw);
      }
    } else if (d.kind == Decoration::Kind::Antenna) {
      add(kAntenna, d.pos, {d.scale, d.scale, d.scale}, d.yaw);
    } else if (d.kind == Decoration::Kind::CrashSite) {
      // The wreck, plus debris in the same layout (and RNG sequence) as
      // drawCrashSite()'s boxes, flattened and yawed.
      add(kCrashHull, d.pos, {d.scale, d.scale, d.scale}, d.yaw);
      uint32_t rng = d.seed;
      int n = 4 + static_cast<int>(xorshift32(rng) % 4);
      for (int i = 0; i < n; ++i) {
        float ox = frand(rng, -3.5f, 3.5f) * d.scale;
        float oz = frand(rng, -3.5f, 3.5f) * d.scale;
        float sz = frand(rng, 0.3f, 0.9f) * d.scale;
        float yaw = frand(rng) * 6.2832f;
        Vector3 p{d.pos.x + ox, 0.0f, d.pos.z + oz};
        p.y = planet.heightAt(p.x, p.z);
        add(kCrashDebris, p, {sz, 0.6f * sz, sz}, yaw);
      }
    }
  }

  size_t largest = 0;
  for (int m = 0; m < kModelCount; ++m) {
    // Counting sort by cell.
    std::vector<uint32_t> &start = m_cellStart[m];
    start.assign(cells + 1, 0);
    for (const Placed &p : placed[m]) ++start[p.cell + 1];
    for (int c = 0; c < cells; ++c) start[c + 1] += start[c];
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    m_xf[m].resize(placed[m].size());
    for (const Placed &p : placed[m]) m_xf[m][fill[p.cell]++] = p.m;
    if (m_xf[m].size() > largest) largest = m_xf[m].size();
  }
  m_visible.clear();
  m_visible.reserve(largest);
}

// ====================================================================
// Render — distance-culled. raylib's DrawCube doesn't frustum-cull,
// so we do a cheap radial check before each call.
// ====================================================================
void Decorations::render(const Planet &planet, Vector3 cameraPos) const {
  constexpr float kRenderRadius = Config::DECOR_RENDER_RADIUS;
  constexpr float kRenderRadius2 = kRenderRadius * kRenderRadius;
  const std::array<bool, kKindCount> inst = instancedKinds();
  bool anyInstanced = false;
  for (bool b : inst) anyInstanced = anyInstanced || b;

  // Instanced models. Walk the grid cells under the render
  // circle in unwrapped cell coordinates; a cell's wrapped index picks
  // its instances and the difference is the toroidal offset that moves
  // them next to the (unwrapped) camera.
  if (anyInstanced && m_gridN > 0) {
    const float cell = Config::DECOR_CELL_SIZE;
    const int x0 = static_cast<int>(floorf((cameraPos.x - kRenderRadius) / cell));
    const int x1 = static_cast<int>(floorf((cameraPos.x + kRenderRadius) / cell));
    const int z0 = static_cast<int>(floorf((cameraPos.z - kRenderRadius) / cell));
    const int z1 = static_cast<int>(floorf((cameraPos.z + kRenderRadius) / cell));
    for (int m = 0; m < kModelCount; ++m) {
      if (!inst[static_cast<int>(kSlotKind[m])]) continue;
      if (m_xf[m].empty()) continue;
      m_visible.clear();
      for (int uz = z0; uz <= z1; ++uz) {
        const int wz = wrapIndex(uz, m_gridN);
        const float offZ = static_cast<float>(uz - wz) * cell;
        for (int ux = x0; ux <= x1; ++ux) {
          const int wx = wrapIndex(ux, m_gridN);
          const float offX = static_cast<float>(ux - wx) * cell;
          const uint32_t c = static_cast<uint32_t>(wz * m_gridN + wx);
          for (uint32_t i = m_cellStart[m][c]; i < m_cellStart[m][c + 1]; ++i) {
            Matrix t = m_xf[m][i];
            t.m12 += offX;
            t.m14 += offZ;
            float dx = t.m12 - cameraPos.x, dz = t.m14 - cameraPos.z;
            if (dx * dx + dz * dz > kRenderRadius2) continue;
            m_visible.push_back(t);
          }
        }
      }
      if (!m_visible.empty())
        DrawMeshInstanced(m_models[m].meshes[0], m_instMaterial,
                          m_visible.data(), static_cast<int>(m_visible.size()));
    }
  }

  // Decorations are placed once in [0, worldSize); draw each at the
  // toroidal copy nearest the camera so they tile with the terrain
  // after the (unwrapped) player crosses a world edge.
  for (const Decoration &item : m_items) {
    // Box fallbacks, for any kind whose models didn't load.
    if (inst[static_cast<int>(item.kind)]) continue;
    Vector3 p = planet.nearestCopy(item.pos, cameraPos);
    float dx = p.x - cameraPos.x;
    float dz = p.z - cameraPos.z;
    if (dx * dx + dz * dz > kRenderRadius2) continue;
    Decoration d = item;
    d.pos = p;
    switch (d.kind) {
    case Decoration::Kind::Tree:      drawTree(d.pos, d.scale);   break;
    case Decoration::Kind::Rock:      drawRock(d, planet);        break;
    case Decoration::Kind::Antenna:   drawAntenna(d);             break;
    case Decoration::Kind::CrashSite: drawCrashSite(d, planet);   break;
    }
  }
}
