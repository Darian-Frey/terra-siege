#include "SettlementLayout.hpp"

#include "Planet.hpp"
#include "Roads.hpp"
#include "Settlements.hpp"
#include "core/Config.hpp"

#include <algorithm>
#include <cmath>

namespace {

using BM = Decoration::BuildingModel;

uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

struct Rng {
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float f() { return (next() & 0xffffffu) / 16777216.0f; }
  float f(float lo, float hi) { return lo + f() * (hi - lo); }
  int i(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

void wrapDelta(float ax, float az, float bx, float bz, float &dx, float &dz) {
  const float ws = Config::WORLD_SIZE;
  dx = bx - ax;
  dz = bz - az;
  dx -= roundf(dx / ws) * ws;
  dz -= roundf(dz / ws) * ws;
}

// Footprint of each model in its own frame (x across, z front to back,
// front at +z): centre offset and half sizes, matching the meshgen
// geometry (scripts/meshgen/settlements.py) including eaves and porches.
struct Size {
  float ox, oz, hw, hd;
};
constexpr Size kSize[static_cast<int>(BM::Count)] = {
    {0.0f, 0.0f, 5.4f, 3.9f},   // FarmhouseA
    {2.0f, 0.0f, 6.9f, 3.9f},   // FarmhouseB (wing to the east)
    {0.0f, 0.0f, 8.4f, 4.9f},   // BarnA
    {0.0f, 0.0f, 7.9f, 4.4f},   // BarnB
    {0.0f, 0.0f, 7.3f, 4.3f},   // DutchBarn
    {0.0f, 0.0f, 2.6f, 2.6f},   // GrainSilo
    {0.0f, 0.0f, 3.3f, 2.3f},   // OutbuildingA
    {0.0f, 0.0f, 3.9f, 2.6f},   // OutbuildingB
    {0.0f, 0.0f, 4.1f, 2.1f},   // Greenhouse
    {0.0f, 0.0f, 7.6f, 3.1f},   // Windmill (sails sweep across)
    {0.0f, -0.4f, 2.4f, 1.8f},  // HayBalesA
    {0.0f, 0.0f, 1.8f, 1.0f},   // HayBalesB
    {0.0f, 0.5f, 1.4f, 1.8f},   // Tractor
    {0.0f, 0.0f, 4.5f, 3.5f},   // CottageA
    {0.0f, 0.0f, 4.4f, 3.4f},   // CottageB
    {0.0f, 0.5f, 4.1f, 3.7f},   // CottageC (porch)
    {0.0f, 0.0f, 4.9f, 3.9f},   // HouseA
    {0.0f, 0.0f, 4.9f, 4.1f},   // HouseB
    {0.0f, 0.8f, 5.1f, 4.7f},   // HouseC (porch)
    {0.0f, 0.0f, 7.9f, 3.9f},   // TerraceRowA
    {0.0f, 0.0f, 7.9f, 3.9f},   // TerraceRowB
    {0.0f, 2.4f, 4.4f, 11.8f},  // ChurchA (tower at the front)
    {0.0f, 2.4f, 4.4f, 11.8f},  // ChurchB
    {0.0f, 0.0f, 3.4f, 5.4f},   // Chapel
    {0.0f, 0.5f, 7.4f, 4.9f},   // Pub (sign)
    {0.0f, 0.6f, 4.4f, 4.2f},   // VillageShop (awning)
    {0.0f, 1.0f, 7.4f, 5.4f},   // VillageHall (porch)
    {0.0f, 0.0f, 1.6f, 1.6f},   // WarMemorial
    {0.0f, 0.0f, 0.2f, 2.0f},   // GardenWallA (segment along z)
    {0.0f, 0.0f, 0.2f, 2.0f},   // GardenWallB
    {0.0f, 0.0f, 0.15f, 2.0f},  // PicketFence
    {0.0f, 0.0f, 3.0f, 5.1f},   // TownhouseA (row segment: walls meet)
    {0.0f, 0.0f, 3.0f, 5.1f},   // TownhouseB
    {0.0f, 0.0f, 3.0f, 5.1f},   // TownhouseC
    {0.0f, 0.7f, 9.0f, 5.7f},   // ShopRowA (awnings)
    {0.0f, 0.7f, 9.0f, 5.7f},   // ShopRowB
    {0.0f, 1.35f, 12.3f, 8.7f}, // TownHall (portico)
    {0.0f, 0.0f, 9.4f, 6.4f},   // MarketHall
    {0.0f, 0.0f, 10.0f, 6.1f},  // ApartmentBlockA
    {0.0f, 0.0f, 10.0f, 6.1f},  // ApartmentBlockB
    {0.0f, 0.0f, 8.2f, 8.2f},   // OfficeBlockA
    {0.0f, 0.0f, 8.2f, 8.2f},   // OfficeBlockB
    {0.0f, 0.0f, 15.3f, 9.3f},  // WarehouseA
    {0.0f, 0.0f, 15.3f, 9.3f},  // WarehouseB
    {0.0f, 0.0f, 18.0f, 11.1f}, // Factory
    {0.0f, 0.0f, 2.2f, 2.2f},   // Chimney
    {0.0f, 0.0f, 4.0f, 4.0f},   // WaterTower
    {0.0f, -0.2f, 6.6f, 6.8f},  // PetrolStation
    {0.0f, 0.0f, 0.95f, 2.1f},  // ParkedCarA
    {0.0f, 0.0f, 0.95f, 2.1f},  // ParkedCarB
    {0.0f, 0.0f, 0.95f, 2.1f},  // ParkedCarC
    {0.0f, 0.7f, 0.2f, 0.8f},   // StreetLamp (arm over the road, +z)
    {0.0f, 0.0f, 0.9f, 0.15f},  // TelegraphPole
    {0.0f, 0.0f, 0.8f, 0.1f},   // RoadSign
    {0.0f, 0.0f, 0.9f, 0.9f},   // Signpost
    {0.0f, 0.0f, 1.75f, 0.9f},  // BusShelter
    {0.0f, 0.0f, 0.55f, 0.55f}, // PhoneBox
};
static_assert(sizeof(kSize) / sizeof(kSize[0]) == static_cast<size_t>(BM::Count),
              "a footprint for every building model");

bool isSegment(BM m) { return m == BM::GardenWallA || m == BM::GardenWallB || m == BM::PicketFence; }
// Street props: small, sit on the ground at their centre, may stand
// close to the road (parked cars on it).
bool isFurniture(BM m) { return m >= BM::ParkedCarA && m <= BM::PhoneBox; }

} // namespace

SettlementLayout::Footprint SettlementLayout::footprintOf(BM model, float x, float z, float yaw,
                                                          float stretch) {
  const Size &s = kSize[static_cast<int>(model)];
  const float c = cosf(yaw), sn = sinf(yaw);
  return {x + c * s.ox + sn * s.oz, z - sn * s.ox + c * s.oz, yaw, s.hw, s.hd * stretch};
}

void SettlementLayout::index(const Footprint &f) {
  const int id = static_cast<int>(m_prints.size());
  m_prints.push_back(f);
  const float cell = Config::BUILDING_INDEX_CELL;
  const float r = sqrtf(f.hw * f.hw + f.hd * f.hd);
  for (int cz = static_cast<int>(floorf((f.z - r) / cell)); cz <= static_cast<int>(floorf((f.z + r) / cell)); ++cz)
    for (int cx = static_cast<int>(floorf((f.x - r) / cell)); cx <= static_cast<int>(floorf((f.x + r) / cell)); ++cx)
      m_grid[static_cast<size_t>(((cz % m_gridN) + m_gridN) % m_gridN) * m_gridN +
             ((cx % m_gridN) + m_gridN) % m_gridN]
          .push_back(id);
}

bool SettlementLayout::blocked(float x, float z, float margin) const {
  if (m_gridN == 0) return false;
  const float cell = Config::BUILDING_INDEX_CELL;
  for (int cz = static_cast<int>(floorf((z - margin) / cell)); cz <= static_cast<int>(floorf((z + margin) / cell)); ++cz)
    for (int cx = static_cast<int>(floorf((x - margin) / cell)); cx <= static_cast<int>(floorf((x + margin) / cell)); ++cx)
      for (int id : m_grid[static_cast<size_t>(((cz % m_gridN) + m_gridN) % m_gridN) * m_gridN +
                           ((cx % m_gridN) + m_gridN) % m_gridN]) {
        const Footprint &f = m_prints[id];
        float dx, dz;
        wrapDelta(f.x, f.z, x, z, dx, dz);
        const float c = cosf(f.yaw), s = sinf(f.yaw);
        const float lx = c * dx - s * dz, lz = s * dx + c * dz;
        if (fabsf(lx) <= f.hw + margin && fabsf(lz) <= f.hd + margin) return true;
      }
  return false;
}

bool SettlementLayout::overlaps(const Footprint &f, float margin) const {
  // Separating-axis test against nearby footprints.
  const float r = sqrtf(f.hw * f.hw + f.hd * f.hd) + margin + 30.0f;
  const float cell = Config::BUILDING_INDEX_CELL;
  auto axesOf = [](const Footprint &g, float ax[2][2]) {
    const float c = cosf(g.yaw), s = sinf(g.yaw);
    ax[0][0] = c; ax[0][1] = -s; // local x in world
    ax[1][0] = s; ax[1][1] = c;  // local z in world
  };
  float fa[2][2];
  axesOf(f, fa);
  for (int cz = static_cast<int>(floorf((f.z - r) / cell)); cz <= static_cast<int>(floorf((f.z + r) / cell)); ++cz)
    for (int cx = static_cast<int>(floorf((f.x - r) / cell)); cx <= static_cast<int>(floorf((f.x + r) / cell)); ++cx)
      for (int id : m_grid[static_cast<size_t>(((cz % m_gridN) + m_gridN) % m_gridN) * m_gridN +
                           ((cx % m_gridN) + m_gridN) % m_gridN]) {
        const Footprint &g = m_prints[id];
        float dx, dz;
        wrapDelta(f.x, f.z, g.x, g.z, dx, dz);
        float ga[2][2];
        axesOf(g, ga);
        bool separated = false;
        for (const float *ax : {fa[0], fa[1], ga[0], ga[1]}) {
          const float d = fabsf(dx * ax[0] + dz * ax[1]);
          const float rf = f.hw * fabsf(fa[0][0] * ax[0] + fa[0][1] * ax[1]) +
                           f.hd * fabsf(fa[1][0] * ax[0] + fa[1][1] * ax[1]);
          const float rg = g.hw * fabsf(ga[0][0] * ax[0] + ga[0][1] * ax[1]) +
                           g.hd * fabsf(ga[1][0] * ax[0] + ga[1][1] * ax[1]);
          if (d > rf + rg + margin) {
            separated = true;
            break;
          }
        }
        if (!separated) return true;
      }
  return false;
}

void SettlementLayout::build(const Planet &planet, uint32_t seed, std::vector<Decoration> &out) {
  m_prints.clear();
  m_gridN = static_cast<int>(Config::WORLD_SIZE / Config::BUILDING_INDEX_CELL);
  m_grid.assign(static_cast<size_t>(m_gridN) * m_gridN, {});
  const RoadNetwork &roads = planet.roads();
  Rng rng{mix(seed ^ 0x51ed270bu) | 1u};

  // Try to put `model` at (x, z) facing yaw; on success append it and
  // record its footprint. gap < 0 uses the kind's default spacing (row
  // houses pass a small gap so terraces join). onRoad lets parked cars
  // stand on the carriageway.
  auto place = [&](BM model, float x, float z, float yaw, float stretch = 1.0f, float gap = -1.0f,
                   bool onRoad = false) {
    const Footprint f = footprintOf(model, x, z, yaw, stretch);
    const bool small = isSegment(model) || isFurniture(model);
    const float c = cosf(f.yaw), s = sinf(f.yaw);
    float lo = 1e30f, hi = -1e30f;
    const float roadClear = isFurniture(model) ? Config::FURNITURE_ROAD_CLEARANCE
                            : small            ? Config::SEGMENT_ROAD_CLEARANCE
                                               : Config::BUILDING_ROAD_CLEARANCE;
    // Sample the outline every few metres, plus the centre.
    const int nx = std::max(1, static_cast<int>(ceilf(2.0f * f.hw / 3.0f)));
    const int nz = std::max(1, static_cast<int>(ceilf(2.0f * f.hd / 3.0f)));
    for (int iz = 0; iz <= nz; ++iz)
      for (int ix = 0; ix <= nx; ++ix) {
        if (iz != 0 && iz != nz && ix != 0 && ix != nx && !(ix == nx / 2 && iz == nz / 2)) continue;
        const float lx = -f.hw + 2.0f * f.hw * ix / nx, lz = -f.hd + 2.0f * f.hd * iz / nz;
        const float px = f.x + c * lx + s * lz, pz = f.z - s * lx + c * lz;
        if (planet.isWater(px, pz)) return false;
        if (!onRoad && roads.edgeDistance(px, pz, roadClear + 1.0f) < roadClear) return false;
        const float h = planet.heightAt(px, pz);
        lo = fminf(lo, h);
        hi = fmaxf(hi, h);
      }
    if (hi - lo > (small ? Config::BUILDING_MAX_RISE * 2.0f : Config::BUILDING_MAX_RISE)) return false;
    const float g = gap >= 0.0f ? gap : small ? Config::SEGMENT_GAP : Config::BUILDING_GAP;
    if (overlaps(f, g)) return false;
    Decoration d;
    d.kind = Decoration::Kind::Building;
    d.building = model;
    d.pos = {x, small ? planet.heightAt(x, z) : hi - Config::BUILDING_SINK, z};
    d.yaw = yaw;
    d.scale = 1.0f;
    d.stretch = stretch;
    d.seed = rng.next();
    out.push_back(d);
    index(f);
    return true;
  };
  // Yaw that turns a model's front towards the nearest road (random if
  // none is within `range`).
  auto faceRoad = [&](float x, float z, float range) {
    RoadNetwork::Point p, dir;
    float hw;
    if (!roads.nearestPoint(x, z, range, p, dir, hw)) return rng.f(0.0f, 6.2831853f);
    return atan2f(p.x - x, p.z - z);
  };
  // Try a model on rings round (cx, cz) until it fits, facing the road.
  auto placeNear = [&](BM model, float cx, float cz, float r0, float r1, float roadRange) {
    for (float r = r0; r <= r1; r += 4.0f) {
      const int n = std::max(6, static_cast<int>(r / 4.0f));
      const float a0 = rng.f(0.0f, 6.2831853f);
      for (int k = 0; k < n; ++k) {
        const float a = a0 + 6.2831853f * k / n;
        const float x = cx + cosf(a) * r, z = cz + sinf(a) * r;
        if (place(model, x, z, faceRoad(x, z, roadRange))) return true;
      }
    }
    return false;
  };

  // ---- Frontage walker, shared by villages and towns ----
  // Walks every road through a site (both sides, metre by metre) and asks
  // `choose` what to build at each free spot: a model, its setback from
  // the road edge, the gap to the next building, and whether it gets a
  // front-garden boundary. Returns how many it placed.
  struct Pick {
    BM model;
    float setback, gapAlong, gapMin;
    bool boundary;
  };
  auto frontage = [&](const SettlementSite &site, bool streets, int maxCount, BM boundary,
                      const auto &choose, const auto &placed) {
    int count = 0;
    for (const RoadNetwork::Road &rd : roads.roads()) {
      using C = RoadNetwork::Class;
      if (rd.cls == C::Track || rd.cls == C::Square || rd.pts.size() < 2) continue;
      if (rd.cls == C::Street && !streets) continue;
      std::vector<RoadNetwork::Point> pts;
      float dx, dz;
      wrapDelta(site.x, site.z, rd.pts[0].x, rd.pts[0].z, dx, dz);
      const float sx = site.x + dx - rd.pts[0].x, sz = site.z + dz - rd.pts[0].z;
      bool near = false;
      for (const auto &q : rd.pts) {
        pts.push_back({q.x + sx, q.z + sz});
        const float ex = pts.back().x - site.x, ez = pts.back().z - site.z;
        near = near || ex * ex + ez * ez < (site.radius + 30.0f) * (site.radius + 30.0f);
      }
      if (!near) continue;
      const float rhw = RoadNetwork::halfWidthOf(rd.cls);
      for (int side = -1; side <= 1; side += 2) {
        float nextFree = 0.0f, arc = 0.0f;
        for (size_t i = 0; i + 1 < pts.size() && count < maxCount; ++i) {
          const auto a = pts[i], b = pts[i + 1];
          const float ex = b.x - a.x, ez = b.z - a.z, len = sqrtf(ex * ex + ez * ez);
          if (len < 0.01f) continue;
          const float tx = ex / len, tz = ez / len, nx = -tz * side, nz = tx * side;
          for (float t = 0.0f; t < len && count < maxCount; t += 1.0f) {
            const float at = arc + t;
            if (at < nextFree) continue;
            const float px = a.x + tx * t, pz = a.z + tz * t;
            const float dcx = px - site.x, dcz = pz - site.z, dc = sqrtf(dcx * dcx + dcz * dcz);
            if (dc > site.radius) continue;
            Pick pk;
            if (!choose(dc, atan2f(dcz, dcx), rd.cls, pk)) {
              nextFree = at + 2.0f;
              continue;
            }
            const Size &z = kSize[static_cast<int>(pk.model)];
            const float off = rhw + pk.setback + z.hd + z.oz; // front edge at the setback
            const float cx = px + tx * z.hw + nx * off, cz = pz + tz * z.hw + nz * off;
            const float yaw = atan2f(-nx, -nz); // front towards the road
            if (place(pk.model, cx, cz, yaw, 1.0f, pk.gapMin)) {
              ++count;
              placed(pk.model, out.back());
              const float gl = 2.0f * z.hw - 1.5f;
              const float go = rhw + Config::FRONTAGE_BOUNDARY_OFFSET;
              if (pk.boundary && gl > 1.0f)
                place(boundary, px + tx * z.hw + nx * go, pz + tz * z.hw + nz * go, atan2f(tx, tz),
                      gl / 4.0f);
              nextFree = at + 2.0f * z.hw + pk.gapAlong;
            } else {
              nextFree = at + 2.0f;
            }
          }
          arc += len;
        }
      }
    }
    return count;
  };
  // Every point along a site's roads of the given classes, every `step`
  // metres, with the road direction and half-width (for street props).
  auto alongRoads = [&](float cx, float cz, float radius, bool streetsOnly, float step,
                        const auto &visit) {
    for (const RoadNetwork::Road &rd : roads.roads()) {
      using C = RoadNetwork::Class;
      if (rd.pts.size() < 2 || rd.cls == C::Track || rd.cls == C::Square) continue;
      if (streetsOnly && rd.cls != C::Street) continue;
      float dx, dz;
      wrapDelta(cx, cz, rd.pts[0].x, rd.pts[0].z, dx, dz);
      const float sx = cx + dx - rd.pts[0].x, sz = cz + dz - rd.pts[0].z;
      float carry = 0.0f;
      for (size_t i = 0; i + 1 < rd.pts.size(); ++i) {
        const float ax = rd.pts[i].x + sx, az = rd.pts[i].z + sz;
        const float ex = rd.pts[i + 1].x - rd.pts[i].x, ez = rd.pts[i + 1].z - rd.pts[i].z;
        const float len = sqrtf(ex * ex + ez * ez);
        if (len < 0.01f) continue;
        for (float t = carry; t < len; t += step) {
          const float px = ax + ex / len * t, pz = az + ez / len * t;
          if ((px - cx) * (px - cx) + (pz - cz) * (pz - cz) <= radius * radius)
            visit(px, pz, ex / len, ez / len, RoadNetwork::halfWidthOf(rd.cls), rd.cls);
        }
        carry = fmodf(carry - len, step);
        if (carry < 0.0f) carry += step;
      }
    }
  };
  auto pickOf = [&](std::initializer_list<BM> l) { return *(l.begin() + rng.i(static_cast<int>(l.size()))); };

  using K = SettlementSite::Kind;
  using C = RoadNetwork::Class;
  for (const SettlementSite &site : planet.settlements().sites()) {
    if (site.kind == K::Village) {
      const bool bigChurch = rng.f() < 0.7f;
      const BM church = bigChurch ? (rng.f() < 0.5f ? BM::ChurchA : BM::ChurchB) : BM::Chapel;
      const BM boundary = rng.f() < 0.35f ? BM::GardenWallA : rng.f() < 0.5f ? BM::GardenWallB : BM::PicketFence;
      // ---- Core: church (or chapel) near the centre, memorial in front ----
      if (placeNear(church, site.x, site.z, 6.0f, 36.0f, 60.0f)) {
        const Decoration &ch = out.back();
        const float fx = sinf(ch.yaw), fz = cosf(ch.yaw);
        const float reach = kSize[static_cast<int>(church)].hd + kSize[static_cast<int>(church)].oz + 5.0f;
        for (float side : {0.0f, 7.0f, -7.0f})
          if (place(BM::WarMemorial, ch.pos.x + fx * reach + fz * side, ch.pos.z + fz * reach - fx * side,
                    ch.yaw + 3.14159265f))
            break;
      }
      // ---- Street frontage along the main roads and lanes through it ----
      bool pub = false, shop = false, hall = false;
      frontage(site, false, Config::VILLAGE_MAX_BUILDINGS, boundary,
               [&](float dc, float, C, Pick &pk) {
                 const bool core = dc < 0.55f * site.radius;
                 if (core && !pub && rng.f() < 0.5f) pk.model = BM::Pub;
                 else if (core && !shop && rng.f() < 0.5f) pk.model = BM::VillageShop;
                 else if (core && !hall && rng.f() < 0.3f) pk.model = BM::VillageHall;
                 else if (core && rng.f() < 0.35f) pk.model = pickOf({BM::TerraceRowA, BM::TerraceRowB});
                 else pk.model = pickOf({BM::CottageA, BM::CottageB, BM::CottageC, BM::HouseA, BM::HouseB, BM::HouseC});
                 pk.setback = rng.f(Config::FRONTAGE_SETBACK_MIN, Config::FRONTAGE_SETBACK_MAX);
                 pk.gapAlong = rng.f(Config::FRONTAGE_GAP_MIN, Config::FRONTAGE_GAP_MAX);
                 pk.gapMin = -1.0f;
                 pk.boundary = true;
                 return true;
               },
               [&](BM m, const Decoration &) {
                 pub = pub || m == BM::Pub;
                 shop = shop || m == BM::VillageShop;
                 hall = hall || m == BM::VillageHall;
               });
      // ---- Fringe: a few farm buildings round the edge ----
      const int fringe = 1 + rng.i(3);
      for (int k = 0; k < fringe; ++k)
        placeNear(pickOf({BM::BarnA, BM::BarnB, BM::OutbuildingA, BM::OutbuildingB, BM::HayBalesA, BM::DutchBarn}),
                  site.x, site.z, 0.8f * site.radius, 1.25f * site.radius, 80.0f);
    } else if (site.kind == K::Town) {
      // ---- Square: town hall, market hall and church facing it ----
      const float sqR = Config::TOWN_SQUARE_RADIUS;
      const float axis = rng.f(0.0f, 6.2831853f);
      for (BM m : {BM::TownHall, BM::MarketHall, rng.f() < 0.5f ? BM::ChurchA : BM::ChurchB}) {
        const Size &z = kSize[static_cast<int>(m)];
        for (int k = 0; k < 16; ++k) {
          const float a = axis + k * 0.3927f;
          const float r = sqR + 3.0f + z.hd + z.oz;
          const float x = site.x + cosf(a) * r, zz = site.z + sinf(a) * r;
          if (place(m, x, zz, atan2f(site.x - x, site.z - zz))) break;
        }
      }
      // ---- Frontage by zone: shops and terraces at the centre,
      // townhouses and flats further out, houses and an industrial
      // quarter at the edge ----
      const BM boundary = rng.f() < 0.5f ? BM::GardenWallA : BM::GardenWallB;
      const float industry = rng.f(0.0f, 6.2831853f);
      bool petrol = false;
      const float R = site.radius;
      frontage(site, true, Config::TOWN_MAX_BUILDINGS, boundary,
               [&](float dc, float ang, C cls, Pick &pk) {
                 float da = fabsf(ang - industry);
                 da = fminf(da, 6.2831853f - da);
                 pk.boundary = false;
                 pk.gapMin = -1.0f;
                 if (!petrol && cls == C::Main && dc > 0.6f * R && rng.f() < 0.3f) {
                   pk.model = BM::PetrolStation;
                   pk.setback = 2.0f;
                   pk.gapAlong = 6.0f;
                 } else if (dc < Config::TOWN_CORE_FRAC * R) {
                   const float r = rng.f();
                   pk.model = r < 0.4f ? pickOf({BM::ShopRowA, BM::ShopRowB})
                              : r < 0.75f ? pickOf({BM::TownhouseA, BM::TownhouseB, BM::TownhouseC})
                              : r < 0.88f ? pickOf({BM::OfficeBlockA, BM::OfficeBlockB})
                                          : pickOf({BM::ApartmentBlockA, BM::ApartmentBlockB});
                   pk.setback = rng.f(0.5f, 1.5f);
                   pk.gapAlong = rng.f(0.0f, 0.4f);
                   pk.gapMin = 0.1f; // terraces join
                 } else if (dc < Config::TOWN_MID_FRAC * R) {
                   const float r = rng.f();
                   pk.model = r < 0.45f ? pickOf({BM::TownhouseA, BM::TownhouseB, BM::TownhouseC})
                              : r < 0.6f ? pickOf({BM::ApartmentBlockA, BM::ApartmentBlockB})
                              : r < 0.75f ? pickOf({BM::TerraceRowA, BM::TerraceRowB})
                                          : pickOf({BM::HouseA, BM::HouseB, BM::HouseC});
                   pk.setback = rng.f(1.5f, 3.5f);
                   pk.gapAlong = rng.f(0.3f, 2.5f);
                   pk.gapMin = 0.3f;
                   pk.boundary = pk.model == BM::HouseA || pk.model == BM::HouseB || pk.model == BM::HouseC;
                 } else if (da < Config::TOWN_INDUSTRY_HALF_ANGLE) {
                   const float r = rng.f();
                   pk.model = r < 0.6f ? pickOf({BM::WarehouseA, BM::WarehouseB}) : r < 0.85f ? BM::Factory : BM::HouseA;
                   pk.setback = rng.f(4.0f, 8.0f);
                   pk.gapAlong = rng.f(3.0f, 8.0f);
                 } else {
                   pk.model = pickOf({BM::HouseA, BM::HouseB, BM::HouseC, BM::CottageB, BM::CottageC,
                                      BM::TerraceRowA, BM::TerraceRowB});
                   pk.setback = rng.f(Config::FRONTAGE_SETBACK_MIN, Config::FRONTAGE_SETBACK_MAX);
                   pk.gapAlong = rng.f(Config::FRONTAGE_GAP_MIN, Config::FRONTAGE_GAP_MAX);
                   pk.boundary = true;
                 }
                 return true;
               },
               [&](BM m, const Decoration &d) {
                 petrol = petrol || m == BM::PetrolStation;
                 if (m == BM::Factory) { // its chimney beside it
                   const float fx = sinf(d.yaw), fz = cosf(d.yaw);
                   for (float side : {1.0f, -1.0f})
                     if (place(BM::Chimney, d.pos.x + fz * side * 21.0f - fx * 6.0f,
                               d.pos.z - fx * side * 21.0f - fz * 6.0f, d.yaw, 1.0f, 0.5f))
                       break;
                 }
               });
      placeNear(BM::WaterTower, site.x + cosf(industry) * 0.8f * R, site.z + sinf(industry) * 0.8f * R,
                0.0f, 0.4f * R, 80.0f);
      // ---- Street props: lamps, parked cars, a phone box and a bus
      // shelter on the square, and town-limit signs on the main roads ----
      int lampSide = 1;
      alongRoads(site.x, site.z, R, false, Config::TOWN_LAMP_SPACING,
                 [&](float px, float pz, float tx, float tz, float rhw, C) {
                   const float nx = -tz * lampSide, nz = tx * lampSide;
                   const float o = rhw + 0.6f;
                   place(BM::StreetLamp, px + nx * o, pz + nz * o, atan2f(-nx, -nz));
                   lampSide = -lampSide;
                 });
      alongRoads(site.x, site.z, R, true, Config::TOWN_CAR_SPACING,
                 [&](float px, float pz, float tx, float tz, float rhw, C) {
                   if (rng.f() > Config::TOWN_CAR_CHANCE) return;
                   const float side = rng.f() < 0.5f ? 1.0f : -1.0f;
                   const float nx = -tz * side, nz = tx * side, o = rhw - 1.1f;
                   const BM car = pickOf({BM::ParkedCarA, BM::ParkedCarB, BM::ParkedCarC});
                   place(car, px + nx * o, pz + nz * o, atan2f(tx * side, tz * side), 1.0f, 0.3f, true);
                 });
      placeNear(BM::PhoneBox, site.x, site.z, sqR + 2.0f, sqR + 12.0f, 30.0f);
      placeNear(BM::BusShelter, site.x, site.z, sqR + 2.0f, sqR + 14.0f, 30.0f);
      for (const RoadNetwork::Road &rd : roads.roads()) {
        if (rd.cls != C::Main || rd.pts.size() < 2) continue;
        float dx, dz;
        wrapDelta(site.x, site.z, rd.pts[0].x, rd.pts[0].z, dx, dz);
        const float sx = site.x + dx - rd.pts[0].x, sz = site.z + dz - rd.pts[0].z;
        for (size_t i = 0; i + 1 < rd.pts.size(); ++i) {
          const float ax = rd.pts[i].x + sx, az = rd.pts[i].z + sz;
          const float bx = rd.pts[i + 1].x + sx, bz = rd.pts[i + 1].z + sz;
          const float da = hypotf(ax - site.x, az - site.z), db = hypotf(bx - site.x, bz - site.z);
          if ((da - R) * (db - R) > 0.0f) continue; // the edge of town lies between them
          const float t = (R - da) / (db - da);
          const float px = ax + (bx - ax) * t, pz = az + (bz - az) * t;
          const float ux = (bx - ax) / hypotf(bx - ax, bz - az), uz = (bz - az) / hypotf(bx - ax, bz - az);
          const float out = db > da ? 1.0f : -1.0f; // along the road, away from town
          const float o = RoadNetwork::halfWidthOf(C::Main) + 1.5f;
          place(BM::RoadSign, px - uz * o, pz + ux * o, atan2f(ux * out, uz * out));
        }
      }
    } else if (site.kind == K::Farm) {
      // ---- Farmstead: farmhouse, barn, outbuilding, then extras ----
      std::vector<BM> list{rng.f() < 0.5f ? BM::FarmhouseA : BM::FarmhouseB,
                           rng.f() < 0.5f ? BM::BarnA : BM::BarnB,
                           rng.f() < 0.5f ? BM::OutbuildingA : BM::OutbuildingB};
      if (rng.f() < 0.4f) list.push_back(BM::DutchBarn);
      if (rng.f() < 0.3f) list.push_back(BM::GrainSilo);
      if (rng.f() < 0.5f) list.push_back(rng.f() < 0.5f ? BM::HayBalesA : BM::HayBalesB);
      if (rng.f() < 0.3f) list.push_back(BM::Tractor);
      if (rng.f() < 0.15f) list.push_back(BM::Greenhouse);
      if (rng.f() < Config::FARM_WINDMILL_CHANCE) list.push_back(BM::Windmill);
      for (BM m : list) placeNear(m, site.x, site.z, 6.0f, site.radius + 14.0f, 50.0f);
    }
  }

  // ---- Telegraph poles along country lanes, clear of settlements ----
  const Settlements &settlements = planet.settlements();
  for (const RoadNetwork::Road &rd : roads.roads()) {
    if (rd.cls != C::Lane || rd.pts.size() < 2) continue;
    float carry = 0.0f;
    const float o = RoadNetwork::halfWidthOf(C::Lane) + Config::TELEGRAPH_POLE_OFFSET;
    for (size_t i = 0; i + 1 < rd.pts.size(); ++i) {
      const auto a = rd.pts[i], b = rd.pts[i + 1];
      const float ex = b.x - a.x, ez = b.z - a.z, len = sqrtf(ex * ex + ez * ez);
      if (len < 0.01f) continue;
      const float tx = ex / len, tz = ez / len;
      for (float t = carry; t < len; t += Config::TELEGRAPH_POLE_SPACING) {
        const float px = a.x + tx * t - tz * o, pz = a.z + tz * t + tx * o;
        if (!settlements.occupied(px, pz, 10.0f)) place(BM::TelegraphPole, px, pz, atan2f(tx, tz));
      }
      carry = fmodf(carry - len, Config::TELEGRAPH_POLE_SPACING);
      if (carry < 0.0f) carry += Config::TELEGRAPH_POLE_SPACING;
    }
  }
}
