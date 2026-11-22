#include "Settlements.hpp"

#include "Heightmap.hpp"
#include "core/Config.hpp"
#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace {

uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

float wrapDist2(float ax, float az, float bx, float bz) {
  const float ws = Config::WORLD_SIZE;
  float dx = bx - ax, dz = bz - az;
  dx -= roundf(dx / ws) * ws;
  dz -= roundf(dz / ws) * ws;
  return dx * dx + dz * dz;
}

struct Candidate {
  float x, z, y, score;
};

} // namespace

int Settlements::count(SettlementSite::Kind k) const {
  int n = 0;
  for (const SettlementSite &s : m_sites) n += s.kind == k;
  return n;
}

bool Settlements::occupied(float x, float z, float margin) const {
  for (const SettlementSite &s : m_sites) {
    const float r = s.radius + margin;
    if (wrapDist2(x, z, s.x, s.z) < r * r) return true;
  }
  return false;
}

void Settlements::generate(const Heightmap &hmap, uint32_t seed) {
  m_sites.clear();
  const float ws = Config::WORLD_SIZE;
  const float cell = Config::TERRAIN_SCALE;
  const float hm = Config::TERRAIN_HEIGHT_MAX;
  auto cellOf = [&](float w) { return static_cast<int>(lroundf(w / cell)); };
  auto heightAt = [&](float x, float z) { return hmap.get(cellOf(x), cellOf(z)) * hm; };
  auto isWater = [&](float x, float z) {
    const int cx = cellOf(x), cz = cellOf(z);
    return hmap.get(cx, cz) < Config::SEA_LEVEL || hmap.waterAt(cx, cz) != WaterType::None;
  };
  // Dry, gentle grassland at a point.
  auto buildable = [&](float x, float z) {
    const int cx = cellOf(x), cz = cellOf(z);
    const float h = hmap.get(cx, cz);
    const float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
    if (landH < Config::BIOME_SAND_MAX || landH >= Config::BIOME_GRASS_MAX) return false;
    if (hmap.waterAt(cx, cz) != WaterType::None) return false;
    const float dx = (hmap.get(cx + 1, cz) - h) * hm / cell;
    const float dz = (hmap.get(cx, cz + 1) - h) * hm / cell;
    return dx * dx + dz * dz <= Config::SETTLEMENT_MAX_SLOPE * Config::SETTLEMENT_MAX_SLOPE;
  };

  // Footprint test: the centre and rings at half and full radius. Fails
  // (returns < 0) unless the centre is buildable, at least `needFrac` of
  // the samples are, and the ground stays within `maxRise` across them.
  // Otherwise scores higher for more buildable, flatter ground.
  auto footprint = [&](float x, float z, float radius, float needFrac, float maxRise) {
    if (!buildable(x, z)) return -1.0f;
    int ok = 1, n = 1;
    float lo = heightAt(x, z), hi = lo;
    for (float rf : {0.5f, 1.0f}) {
      const int pts = rf < 1.0f ? 8 : 16;
      for (int k = 0; k < pts; ++k) {
        const float a = 6.2831853f * (k + 0.5f * rf) / pts;
        const float px = x + cosf(a) * radius * rf, pz = z + sinf(a) * radius * rf;
        ++n;
        ok += buildable(px, pz);
        const float h = heightAt(px, pz);
        lo = fminf(lo, h);
        hi = fmaxf(hi, h);
      }
    }
    const float frac = static_cast<float>(ok) / n;
    const float rise = hi - lo;
    if (frac < needFrac || rise > maxRise) return -1.0f;
    return frac + 0.3f * (1.0f - rise / maxRise);
  };
  // Water within reach of a site (coast or lake), for towns.
  auto waterNear = [&](float x, float z, float radius) {
    for (float rf : {1.4f, 2.5f})
      for (int k = 0; k < 12; ++k) {
        const float a = 6.2831853f * k / 12;
        if (isWater(x + cosf(a) * radius * rf, z + sinf(a) * radius * rf)) return true;
      }
    return false;
  };
  auto jitter = [&](float x, float z, uint32_t salt) {
    const uint32_t h = mix(static_cast<uint32_t>(x) * 73856093u ^
                           static_cast<uint32_t>(z) * 19349663u ^ seed ^ salt);
    return (h & 0xffffu) / 65536.0f;
  };

  // Score every grid candidate for one kind, best first.
  auto candidates = [&](float step, const auto &scoreAt) {
    std::vector<Candidate> out;
    const int n = static_cast<int>(ws / step);
    for (int iz = 0; iz < n; ++iz)
      for (int ix = 0; ix < n; ++ix) {
        const float x = (ix + 0.5f) * step, z = (iz + 0.5f) * step;
        const float s = scoreAt(x, z);
        if (s >= 0.0f) out.push_back({x, z, heightAt(x, z), s});
      }
    std::sort(out.begin(), out.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    return out;
  };
  // Take candidates best-first while they keep clear of placed sites.
  auto take = [&](const std::vector<Candidate> &cands, SettlementSite::Kind kind,
                  float radius, int maxCount, const auto &clearOf) {
    int placed = 0;
    for (const Candidate &c : cands) {
      if (placed >= maxCount) break;
      bool ok = true;
      for (const SettlementSite &s : m_sites) {
        const float need = clearOf(s);
        if (wrapDist2(c.x, c.z, s.x, s.z) < need * need) {
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      m_sites.push_back({kind, c.x, c.z, c.y, radius});
      ++placed;
    }
  };

  using K = SettlementSite::Kind;
  // ---- Towns: big flat grassland, ideally by water, far apart ----
  const float tr = Config::TOWN_RADIUS;
  take(candidates(Config::TOWN_CANDIDATE_STEP,
                  [&](float x, float z) {
                    float s = footprint(x, z, tr, Config::TOWN_BUILDABLE_FRAC, Config::TOWN_MAX_RISE);
                    if (s < 0.0f) return s;
                    if (waterNear(x, z, tr)) s += Config::TOWN_WATER_BONUS;
                    return s + 0.1f * jitter(x, z, 1u);
                  }),
       K::Town, tr, Config::TOWN_COUNT,
       [&](const SettlementSite &) { return Config::TOWN_MIN_SPACING; });

  // ---- Villages: smaller, spaced out, preferring the land between towns ----
  const float vr = Config::VILLAGE_RADIUS;
  take(candidates(Config::VILLAGE_CANDIDATE_STEP,
                  [&](float x, float z) {
                    float s = footprint(x, z, vr, Config::VILLAGE_BUILDABLE_FRAC,
                                        Config::VILLAGE_MAX_RISE);
                    if (s < 0.0f) return s;
                    float nearest = 1e30f;
                    for (const SettlementSite &t : m_sites)
                      nearest = fminf(nearest, sqrtf(wrapDist2(x, z, t.x, t.z)));
                    if (nearest >= Config::VILLAGE_TOWN_BAND_MIN &&
                        nearest <= Config::VILLAGE_TOWN_BAND_MAX)
                      s += Config::VILLAGE_TOWN_BAND_BONUS;
                    return s + 0.1f * jitter(x, z, 2u);
                  }),
       K::Village, vr, Config::VILLAGE_COUNT, [&](const SettlementSite &s) {
         return s.kind == K::Town ? s.radius + vr + Config::VILLAGE_TOWN_GAP
                                  : Config::VILLAGE_MIN_SPACING;
       });

  // ---- Farmsteads: small, many, clear of towns and villages ----
  const float fr = Config::FARM_RADIUS;
  take(candidates(Config::FARM_CANDIDATE_STEP,
                  [&](float x, float z) {
                    float s = footprint(x, z, fr, 1.0f, Config::FARM_MAX_RISE);
                    return s < 0.0f ? s : s + 0.3f * jitter(x, z, 3u);
                  }),
       K::Farm, fr, Config::FARM_COUNT, [&](const SettlementSite &s) {
         return s.kind == K::Farm ? Config::FARM_MIN_SPACING : s.radius + fr + Config::FARM_GAP;
       });
}

void Settlements::exportMap(const Heightmap &hmap, const std::string &path) const {
  const int n = hmap.size() - 1;
  Image img = GenImageColor(n, n, BLACK);
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      const float h = hmap.get(x, z);
      const float landH = (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL);
      Color c;
      if (h < Config::SEA_LEVEL || hmap.waterAt(x, z) != WaterType::None) c = {40, 90, 180, 255};
      else if (landH < Config::BIOME_SAND_MAX) c = {210, 190, 120, 255};
      else if (landH < Config::BIOME_GRASS_MAX) c = {100, 155, 65, 255};
      else if (landH < Config::BIOME_ROCK_MAX) c = {145, 125, 95, 255};
      else if (landH < Config::BIOME_HIGH_MAX) c = {120, 115, 115, 255};
      else c = {240, 245, 255, 255};
      ImageDrawPixel(&img, x, z, c);
    }
  const float px = 1.0f / Config::TERRAIN_SCALE; // pixels per metre
  for (const SettlementSite &s : m_sites) {
    const Color c = s.kind == SettlementSite::Kind::Town      ? Color{230, 40, 40, 255}
                    : s.kind == SettlementSite::Kind::Village ? Color{250, 220, 40, 255}
                                                              : Color{40, 230, 230, 255};
    const int cx = static_cast<int>(s.x * px), cz = static_cast<int>(s.z * px);
    const int r = std::max(2, static_cast<int>(s.radius * px));
    ImageDrawCircleLines(&img, cx, cz, r, c);
    if (s.kind != SettlementSite::Kind::Farm) ImageDrawCircleLines(&img, cx, cz, r + 1, c);
    ImageDrawCircle(&img, cx, cz, s.kind == SettlementSite::Kind::Farm ? 1 : 3, c);
  }
  ExportImage(img, path.c_str());
  UnloadImage(img);
}
