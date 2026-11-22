#include "FieldMap.hpp"

#include "Heightmap.hpp"
#include "Roads.hpp"
#include "Settlements.hpp"
#include "core/Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {

uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

uint32_t xorshift32(uint32_t &state) {
  uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

float frand(uint32_t &rng, float lo, float hi) {
  return lo + (xorshift32(rng) & 0xffffffu) / 16777216.0f * (hi - lo);
}

using Point = FieldMap::Point;
using Poly = std::vector<Point>;

// Where s(a + (b - a)t) crosses zero, for sa and sb of opposite signs.
// Straight cuts are linear along an edge; curved ones are bisected.
template <typename Eval>
Point crossing(Point a, Point b, float sa, float sb, bool curved, const Eval &eval) {
  float t = sa / (sa - sb);
  if (curved) {
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 24; ++i) {
      t = 0.5f * (lo + hi);
      float st = eval({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t});
      if ((st >= 0.0f) == (sa >= 0.0f)) lo = t;
      else hi = t;
    }
  }
  return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
}

// Split a polygon by the cut s(p) = 0 into its front (s >= 0) and back
// parts; either may come back empty. Crossing points are flagged in
// frontOn / backOn so a curved cut's chord can be replaced by the curve.
template <typename Eval>
void splitPoly(const Poly &p, bool curved, const Eval &eval, Poly &front, Poly &back,
               std::vector<bool> *frontOn = nullptr, std::vector<bool> *backOn = nullptr) {
  front.clear();
  back.clear();
  if (frontOn) frontOn->clear();
  if (backOn) backOn->clear();
  const size_t n = p.size();
  float sb = eval(p[0]);
  for (size_t i = 0; i < n; ++i) {
    const Point a = p[i], b = p[(i + 1) % n];
    const float sa = sb;
    sb = eval(b);
    if (sa >= 0.0f) {
      front.push_back(a);
      if (frontOn) frontOn->push_back(sa == 0.0f);
    }
    if (sa <= 0.0f) {
      back.push_back(a);
      if (backOn) backOn->push_back(sa == 0.0f);
    }
    if ((sa > 0.0f && sb < 0.0f) || (sa < 0.0f && sb > 0.0f)) {
      const Point c = crossing(a, b, sa, sb, curved, eval);
      front.push_back(c);
      back.push_back(c);
      if (frontOn) frontOn->push_back(true);
      if (backOn) backOn->push_back(true);
    }
  }
  if (front.size() < 3) front.clear();
  if (back.size() < 3) back.clear();
}

// Replace each chord a split left between two consecutive crossing
// points with the curve itself, sampled every FIELD_CURVE_STEP metres.
// On the cut, p = t·T + (-d - wave(t))·n with T = (-nz, nx).
template <typename Cut>
void bowChords(const Cut &c, Poly &p, const std::vector<bool> &on) {
  Poly out;
  const size_t n = p.size();
  for (size_t i = 0; i < n; ++i) {
    out.push_back(p[i]);
    const size_t j = (i + 1) % n;
    if (!on[i] || !on[j]) continue;
    const float t0 = c.nx * p[i].z - c.nz * p[i].x, t1 = c.nx * p[j].z - c.nz * p[j].x;
    const int steps = static_cast<int>(fabsf(t1 - t0) / Config::FIELD_CURVE_STEP);
    for (int s = 1; s < steps; ++s) {
      const float t = t0 + (t1 - t0) * s / steps;
      const float off = -c.d - c.wave(t);
      out.push_back({-c.nz * t + c.nx * off, c.nx * t + c.nz * off});
    }
  }
  p.swap(out);
}

// Narrowest width of a polygon (convex, or nearly so where a curved cut
// bows an edge): over its edges, the furthest any vertex lies from that
// edge's line; the smallest of those. Edges under 4 m (curve samples)
// are skipped.
float minWidth(const Poly &p) {
  float best = 1e30f;
  const size_t n = p.size();
  for (size_t i = 0; i < n; ++i) {
    const Point a = p[i], b = p[(i + 1) % n];
    const float ex = b.x - a.x, ez = b.z - a.z, len = sqrtf(ex * ex + ez * ez);
    if (len < 4.0f) continue;
    float far = 0.0f;
    for (const Point &q : p)
      far = fmaxf(far, fabsf((q.x - a.x) * ez - (q.z - a.z) * ex) / len);
    best = fminf(best, far);
  }
  return best;
}

Point centroid(const Poly &p) {
  Point c{0.0f, 0.0f};
  for (const Point &q : p) {
    c.x += q.x;
    c.z += q.z;
  }
  return {c.x / p.size(), c.z / p.size()};
}

// Value noise on a lattice of `period` cells per world edge, so it tiles
// with the toroidal world.
float latticeValue(int x, int z, int period, uint32_t seed) {
  x = ((x % period) + period) % period;
  z = ((z % period) + period) % period;
  uint32_t h = mix(static_cast<uint32_t>(x) * 374761393u +
                   static_cast<uint32_t>(z) * 668265263u + seed * 2654435761u);
  return (h & 0xffffffu) / 16777216.0f;
}

float tiledNoise(float x, float z, int period, uint32_t seed) {
  const float f = static_cast<float>(period) / Config::WORLD_SIZE;
  float fx = x * f, fz = z * f;
  int ix = static_cast<int>(floorf(fx)), iz = static_cast<int>(floorf(fz));
  float tx = fx - ix, tz = fz - iz;
  tx = tx * tx * (3.0f - 2.0f * tx);
  tz = tz * tz * (3.0f - 2.0f * tz);
  float a = latticeValue(ix, iz, period, seed);
  float b = latticeValue(ix + 1, iz, period, seed);
  float c = latticeValue(ix, iz + 1, period, seed);
  float d = latticeValue(ix + 1, iz + 1, period, seed);
  float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
  return ab + (cd - ab) * tz;
}

} // namespace

float FieldMap::forestMask(float worldX, float worldZ) const {
  // Coarse octave = forest body, fine octave = ragged edges and outliers.
  float a = tiledNoise(worldX, worldZ, Config::FOREST_NOISE_PERIOD, m_forestSeed);
  float b = tiledNoise(worldX, worldZ, Config::FOREST_NOISE_PERIOD * 3,
                       m_forestSeed ^ 0x5a17u);
  return a * 0.75f + b * 0.25f;
}

float FieldMap::standNoise(float worldX, float worldZ) const {
  return tiledNoise(worldX, worldZ, Config::FOREST_STAND_PERIOD, m_forestSeed ^ 0x2545f491u);
}

int FieldMap::fieldAt(float worldX, float worldZ) const {
  const float ws = Config::WORLD_SIZE;
  worldX = fmodf(worldX, ws);
  worldZ = fmodf(worldZ, ws);
  if (worldX < 0.0f) worldX += ws;
  if (worldZ < 0.0f) worldZ += ws;
  int n = 0;
  while (m_nodes[n].field < 0) {
    const Node &k = m_nodes[n];
    n = k.side({worldX, worldZ}) >= 0.0f ? k.front : k.back;
  }
  return m_nodes[n].field;
}

void FieldMap::clip(const std::vector<Point> &poly, std::vector<Piece> &out) const {
  if (!m_nodes.empty()) clipNode(0, poly, out);
}

void FieldMap::clipNode(int node, const std::vector<Point> &poly,
                        std::vector<Piece> &out) const {
  const Node &k = m_nodes[node];
  if (k.field >= 0) {
    out.push_back({k.field, poly});
    return;
  }
  // Most polygons lie wholly on one side; only edge-straddlers split.
  constexpr float kEps = 1e-4f;
  bool anyFront = false, anyBack = false;
  for (const Point &p : poly) {
    const float s = k.side(p);
    anyFront = anyFront || s > kEps;
    anyBack = anyBack || s < -kEps;
  }
  if (!anyBack) return clipNode(k.front, poly, out);
  if (!anyFront) return clipNode(k.back, poly, out);
  Poly f, b;
  splitPoly(poly, k.amp != 0.0f, [&](Point p) { return k.side(p); }, f, b);
  if (!f.empty()) clipNode(k.front, f, out);
  if (!b.empty()) clipNode(k.back, b, out);
}

void FieldMap::buildRegions(const RoadNetwork &roads) {
  // Mark every raster cell within a road's width plus the barrier band,
  // then flood the rest (4-connected, wrapping) into regions.
  const float cell = Config::FIELD_REGION_CELL;
  const int N = static_cast<int>(Config::WORLD_SIZE / cell);
  m_regionN = N;
  constexpr uint16_t kRoad = 0xffff;
  m_region.assign(static_cast<size_t>(N) * N, 0xfffe); // unvisited
  auto at = [&](int x, int z) -> uint16_t & {
    return m_region[static_cast<size_t>((z % N + N) % N) * N + ((x % N + N) % N)];
  };
  for (const RoadNetwork::Road &r : roads.roads()) {
    const float w = RoadNetwork::halfWidthOf(r.cls) + Config::FIELD_REGION_BARRIER;
    const size_t segs = r.pts.size() == 1 ? 1 : r.pts.size() - 1; // a square is one point
    for (size_t i = 0; i < segs; ++i) {
      const Point a = r.pts[i], b = r.pts[std::min(i + 1, r.pts.size() - 1)];
      const int x0 = static_cast<int>(floorf((fminf(a.x, b.x) - w) / cell));
      const int x1 = static_cast<int>(floorf((fmaxf(a.x, b.x) + w) / cell));
      const int z0 = static_cast<int>(floorf((fminf(a.z, b.z) - w) / cell));
      const int z1 = static_cast<int>(floorf((fmaxf(a.z, b.z) + w) / cell));
      const float ex = b.x - a.x, ez = b.z - a.z, l2 = ex * ex + ez * ez;
      for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
          const float px = (x + 0.5f) * cell, pz = (z + 0.5f) * cell;
          float t = l2 > 0.0f ? ((px - a.x) * ex + (pz - a.z) * ez) / l2 : 0.0f;
          t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
          const float dx = px - (a.x + ex * t), dz = pz - (a.z + ez * t);
          if (dx * dx + dz * dz <= w * w) at(x, z) = kRoad;
        }
    }
  }
  uint16_t next = 0;
  std::vector<int> q;
  for (int i = 0; i < N * N; ++i) {
    if (m_region[i] != 0xfffe) continue;
    const uint16_t id = next < 0xfffd ? next++ : next; // cap; regions past it merge
    m_region[i] = id;
    q.assign(1, i);
    for (size_t k = 0; k < q.size(); ++k) {
      const int x = q[k] % N, z = q[k] / N;
      for (int d = 0; d < 4; ++d) {
        const int nx = x + (d == 0) - (d == 1), nz = z + (d == 2) - (d == 3);
        uint16_t &v = at(nx, nz);
        if (v == 0xfffe) {
          v = id;
          q.push_back(((nz % N + N) % N) * N + ((nx % N + N) % N));
        }
      }
    }
  }
}

int FieldMap::regionAt(float worldX, float worldZ) const {
  if (m_region.empty()) return -1;
  const int N = m_regionN;
  const float cell = Config::FIELD_REGION_CELL;
  const int x0 = static_cast<int>(floorf(worldX / cell)), z0 = static_cast<int>(floorf(worldZ / cell));
  auto get = [&](int x, int z) {
    return m_region[static_cast<size_t>((z % N + N) % N) * N + ((x % N + N) % N)];
  };
  // On the barrier band: the nearest cell off it decides.
  for (int r = 0; r <= 4; ++r) {
    int best = -1;
    float bestD = 1e30f;
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(abs(dx), abs(dz)) != r) continue;
        const uint16_t v = get(x0 + dx, z0 + dz);
        if (v >= 0xfffe) continue;
        const float cx = (x0 + dx + 0.5f) * cell - worldX, cz = (z0 + dz + 0.5f) * cell - worldZ;
        if (cx * cx + cz * cz < bestD) {
          bestD = cx * cx + cz * cz;
          best = v;
        }
      }
    if (best >= 0) return best;
  }
  return -1;
}

int FieldMap::tintAt(int field, float worldX, float worldZ) const {
  const Field &f = m_fields[field];
  const int region = regionAt(worldX, worldZ);
  if (region < 0 || f.homeRegion < 0 || region == f.homeRegion) return f.tint;
  const uint32_t h = mix(f.hash ^ (static_cast<uint32_t>(region) * 0x9e3779b9u));
  return (f.tint + 1 + static_cast<int>(h % (kTintCount - 1))) % kTintCount;
}

void FieldMap::generate(const Heightmap &hmap, uint32_t seed, const RoadNetwork *roads,
                        const Settlements *settlements) {
  uint32_t rng = mix(seed ^ 0x6f1e1d5u) | 1u;
  m_forestSeed = mix(seed ^ 0xb5297a4du);
  m_nodes.clear();
  m_fields.clear();
  m_region.clear();
  m_regionN = 0;
  if (roads) buildRegions(*roads);

  // Binary space partition. Each piece too long for a field is cut across
  // its longer axis (measured along and across its longest edge), the cut
  // tilted by up to FIELD_CUT_JITTER and placed in the middle 40%. A cut that would leave
  // a sliver narrower than FIELD_MIN_SIZE is retried, then given up.
  const float ws = Config::WORLD_SIZE;
  struct Work {
    int node;
    Poly poly;
  };
  std::vector<Work> stack;
  m_nodes.push_back({});
  stack.push_back({0, Poly{{0.0f, 0.0f}, {ws, 0.0f}, {ws, ws}, {0.0f, ws}}});
  Poly f, b;
  std::vector<bool> fOn, bOn;
  while (!stack.empty()) {
    Work w = std::move(stack.back());
    stack.pop_back();
    // Field axes: along its longest edge (u) and across it (v).
    float ux = 1.0f, uz = 0.0f, best = 0.0f;
    for (size_t i = 0; i < w.poly.size(); ++i) {
      const Point a = w.poly[i], c = w.poly[(i + 1) % w.poly.size()];
      const float dx = c.x - a.x, dz = c.z - a.z, l = dx * dx + dz * dz;
      if (l > best) {
        best = l;
        ux = dx / sqrtf(l);
        uz = dz / sqrtf(l);
      }
    }
    auto extent = [&](float ax, float az, float &lo, float &hi) {
      lo = 1e30f;
      hi = -1e30f;
      for (const Point &q : w.poly) {
        const float t = q.x * ax + q.z * az;
        lo = fminf(lo, t);
        hi = fmaxf(hi, t);
      }
      return hi - lo;
    };
    float uLo, uHi, vLo, vHi;
    const float lu = extent(ux, uz, uLo, uHi), lv = extent(-uz, ux, vLo, vHi);
    bool cut = false;
    if (fmaxf(lu, lv) > Config::FIELD_MAX_SIZE) {
      // Cut across the longer axis, now and then the other if it is
      // also too long.
      bool alongU = lu >= lv;
      if (fminf(lu, lv) > Config::FIELD_MAX_SIZE && xorshift32(rng) % 4 == 0) alongU = !alongU;
      const float axx = alongU ? ux : -uz, axz = alongU ? uz : ux;
      const float lo = alongU ? uLo : vLo, hi = alongU ? uHi : vHi;
      for (int attempt = 0; attempt < 8 && !cut; ++attempt) {
        const float th = frand(rng, -Config::FIELD_CUT_JITTER, Config::FIELD_CUT_JITTER);
        const float nx = axx * cosf(th) - axz * sinf(th), nz = axx * sinf(th) + axz * cosf(th);
        const float at = lo + (hi - lo) * frand(rng, 0.3f, 0.7f);
        const Point c = centroid(w.poly);
        const float shift = at - (c.x * axx + c.z * axz);
        const Point p{c.x + axx * shift, c.z + axz * shift};
        Node cutNode;
        cutNode.nx = nx;
        cutNode.nz = nz;
        cutNode.d = -(nx * p.x + nz * p.z);
        // Split straight first; a curve then bows the chord between the
        // two crossing points, so the pieces keep the same corners.
        splitPoly(w.poly, false, [&](Point q) { return cutNode.side(q); }, f, b, &fOn, &bOn);
        if (f.empty() || b.empty()) continue;
        if (frand(rng, 0.0f, 1.0f) < Config::FIELD_CURVE_CHANCE) {
          float lo = 1e30f, hi = -1e30f;
          for (size_t i = 0; i < f.size(); ++i)
            if (fOn[i]) {
              const float t = nx * f[i].z - nz * f[i].x;
              lo = fminf(lo, t);
              hi = fmaxf(hi, t);
            }
          const float span = hi - lo;
          if (span > 2.0f * Config::FIELD_CURVE_STEP) {
            cutNode.amp = fminf(Config::FIELD_CURVE_AMP_MAX,
                                span * frand(rng, 0.3f, 1.0f) * Config::FIELD_CURVE_AMP);
            if (xorshift32(rng) & 1u) cutNode.amp = -cutNode.amp;
            cutNode.t0 = lo;
            cutNode.invSpan = 1.0f / span;
            cutNode.waves = frand(rng, 0.0f, 1.0f) < 0.6f ? 1.0f : 2.0f;
            bowChords(cutNode, f, fOn);
            bowChords(cutNode, b, bOn);
          }
        }
        if (minWidth(f) < Config::FIELD_MIN_SIZE || minWidth(b) < Config::FIELD_MIN_SIZE)
          continue;
        cut = true;
        const int fi = static_cast<int>(m_nodes.size());
        m_nodes.push_back({});
        m_nodes.push_back({});
        cutNode.front = fi;
        cutNode.back = fi + 1;
        m_nodes[w.node] = cutNode;
        stack.push_back({fi, f});
        stack.push_back({fi + 1, b});
      }
    }
    if (!cut) {
      m_nodes[w.node].field = static_cast<int>(m_fields.size());
      m_fields.push_back({});
      m_fields.back().poly = std::move(w.poly);
    }
  }

  const float hm = Config::TERRAIN_HEIGHT_MAX;
  const float s = Config::TERRAIN_SCALE;
  const uint32_t farmSeed = mix(seed ^ 0x3c6ef372u);
  // Dry grassland and gentle slope at a world sample point.
  auto grass = [&](Point p) {
    int x = static_cast<int>(lroundf(p.x / s)), z = static_cast<int>(lroundf(p.z / s));
    float landH = (hmap.get(x, z) - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
    return landH >= Config::BIOME_SAND_MAX && landH < Config::BIOME_GRASS_MAX &&
           hmap.waterAt(x, z) == WaterType::None;
  };
  auto gentle = [&](Point p) {
    int x = static_cast<int>(lroundf(p.x / s)), z = static_cast<int>(lroundf(p.z / s));
    float h = hmap.get(x, z);
    float dx = (hmap.get(x + 1, z) - h) * hm / s;
    float dz = (hmap.get(x, z + 1) - h) * hm / s;
    return dx * dx + dz * dz <= Config::FIELD_MAX_SLOPE * Config::FIELD_MAX_SLOPE;
  };

  std::vector<Point> samples;
  for (int i = 0; i < fieldCount(); ++i) {
    Field &fd = m_fields[i];
    fd.hash = mix(static_cast<uint32_t>(i) * 0x9e3779b9u ^ seed);
    fd.tint = static_cast<uint8_t>((fd.hash >> 16) % kTintCount);
    const Point c = centroid(fd.poly);
    fd.homeRegion = regionAt(c.x, c.z);
    if (settlements && settlements->inTownOrVillage(c.x, c.z, 0.0f)) continue; // village land
    if (tiledNoise(c.x, c.z, Config::FARM_NOISE_PERIOD, farmSeed) <
        Config::FARM_NOISE_THRESHOLD)
      continue; // wild country
    // Corners and edge midpoints pulled a little inside, plus the centre:
    // mostly grass (a corner may clip a beach or a pond) and gentle.
    samples.assign(1, c);
    const size_t n = fd.poly.size();
    for (size_t k = 0; k < n; ++k) {
      const Point a = fd.poly[k], e = fd.poly[(k + 1) % n];
      samples.push_back({c.x + (a.x - c.x) * 0.8f, c.z + (a.z - c.z) * 0.8f});
      samples.push_back({c.x + ((a.x + e.x) * 0.5f - c.x) * 0.8f,
                         c.z + ((a.z + e.z) * 0.5f - c.z) * 0.8f});
    }
    int ok = 0, flat = 0;
    for (const Point &p : samples) {
      ok += grass(p);
      flat += gentle(p);
    }
    const int need = static_cast<int>(samples.size() * 4 / 5);
    if (!grass(c) || ok < need || flat < need) continue;
    if ((fd.hash & 0xffffu) / 65536.0f < Config::FIELD_WOOD_CHANCE)
      fd.wood = true;
    else
      fd.farmland = true;
  }
}
