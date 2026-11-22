#include "Roads.hpp"

#include "Heightmap.hpp"
#include "Settlements.hpp"
#include "core/Config.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {

using Point = RoadNetwork::Point;
using Poly = std::vector<Point>;

float cross(Point a, Point b, Point p) {
  return (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
}

// Twice the signed area. Measured from the first vertex: raw world
// coordinates (up to ~8 km) would cancel catastrophically in float for
// pieces a few metres across.
float area2(const Poly &p) {
  if (p.size() < 3) return 0.0f;
  const Point o = p[0];
  float a = 0.0f;
  for (size_t i = 1; i + 1 < p.size(); ++i) {
    const float ux = p[i].x - o.x, uz = p[i].z - o.z;
    const float vx = p[i + 1].x - o.x, vz = p[i + 1].z - o.z;
    a += ux * vz - vx * uz;
  }
  return a;
}

// Split a convex polygon by the directed line a→b into the part on its
// left (inside, cross >= 0) and the part on its right.
void splitByLine(const Poly &p, Point a, Point b, Poly &in, Poly &out) {
  in.clear();
  out.clear();
  const size_t n = p.size();
  for (size_t i = 0; i < n; ++i) {
    const Point u = p[i], v = p[(i + 1) % n];
    const float su = cross(a, b, u), sv = cross(a, b, v);
    if (su >= 0.0f) in.push_back(u);
    if (su <= 0.0f) out.push_back(u);
    if ((su > 0.0f && sv < 0.0f) || (su < 0.0f && sv > 0.0f)) {
      const float t = su / (su - sv);
      const Point c{u.x + (v.x - u.x) * t, u.z + (v.z - u.z) * t};
      in.push_back(c);
      out.push_back(c);
    }
  }
  if (in.size() < 3 || fabsf(area2(in)) < 1e-4f) in.clear();
  if (out.size() < 3 || fabsf(area2(out)) < 1e-4f) out.clear();
}


// Chaikin corner cutting, endpoints kept.
Poly chaikin(const Poly &p) {
  if (p.size() < 3) return p;
  Poly out{p.front()};
  for (size_t i = 0; i + 1 < p.size(); ++i) {
    const Point a = p[i], b = p[i + 1];
    out.push_back({0.75f * a.x + 0.25f * b.x, 0.75f * a.z + 0.25f * b.z});
    out.push_back({0.25f * a.x + 0.75f * b.x, 0.25f * a.z + 0.75f * b.z});
  }
  out.push_back(p.back());
  return out;
}

// Douglas-Peucker simplification.
void simplify(const Poly &p, size_t i0, size_t i1, float tol, std::vector<bool> &keep) {
  if (i1 <= i0 + 1) return;
  const Point a = p[i0], b = p[i1];
  const float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
  float worst = -1.0f;
  size_t wi = i0;
  for (size_t i = i0 + 1; i < i1; ++i) {
    const float d = len > 1e-4f ? fabsf(cross(a, b, p[i])) / len
                                : sqrtf((p[i].x - a.x) * (p[i].x - a.x) + (p[i].z - a.z) * (p[i].z - a.z));
    if (d > worst) {
      worst = d;
      wi = i;
    }
  }
  if (worst > tol) {
    keep[wi] = true;
    simplify(p, i0, wi, tol, keep);
    simplify(p, wi, i1, tol, keep);
  }
}

Poly simplified(const Poly &p, float tol) {
  if (p.size() < 3) return p;
  std::vector<bool> keep(p.size(), false);
  keep.front() = keep.back() = true;
  simplify(p, 0, p.size() - 1, tol, keep);
  Poly out;
  for (size_t i = 0; i < p.size(); ++i)
    if (keep[i]) out.push_back(p[i]);
  return out;
}

} // namespace

float RoadNetwork::halfWidthOf(Class c) {
  switch (c) {
  case Class::Main: return 0.5f * Config::ROAD_MAIN_WIDTH;
  case Class::Lane: return 0.5f * Config::ROAD_LANE_WIDTH;
  case Class::Track: return 0.5f * Config::ROAD_TRACK_WIDTH;
  case Class::Street: return 0.5f * Config::ROAD_STREET_WIDTH;
  case Class::Square: return Config::TOWN_SQUARE_RADIUS;
  }
  return 0.0f;
}

float RoadNetwork::length(Class c) const {
  float total = 0.0f;
  for (const Road &r : m_roads) {
    if (r.cls != c) continue;
    for (size_t i = 0; i + 1 < r.pts.size(); ++i)
      total += sqrtf((r.pts[i + 1].x - r.pts[i].x) * (r.pts[i + 1].x - r.pts[i].x) +
                     (r.pts[i + 1].z - r.pts[i].z) * (r.pts[i + 1].z - r.pts[i].z));
  }
  return total;
}

void RoadNetwork::generate(const Heightmap &hmap, const Settlements &settlements, uint32_t seed,
                           const ProgressCb &progress) {
  m_roads.clear();
  m_unconnected = 0;
  auto report = [&](const char *step, float p) {
    if (progress) progress(step, p);
  };

  const float ws = Config::WORLD_SIZE;
  const float G = Config::ROAD_GRID_STEP;
  const int N = static_cast<int>(ws / G);
  const float hm = Config::TERRAIN_HEIGHT_MAX;
  const float cellsPerNode = G / Config::TERRAIN_SCALE;
  auto idx = [N](int x, int z) { return ((z % N + N) % N) * N + ((x % N + N) % N); };
  auto wetCell = [&](int cx, int cz) {
    return hmap.get(cx, cz) < Config::SEA_LEVEL || hmap.waterAt(cx, cz) != WaterType::None;
  };
  auto wetAt = [&](float x, float z) {
    return wetCell(static_cast<int>(lroundf(x / Config::TERRAIN_SCALE)),
                   static_cast<int>(lroundf(z / Config::TERRAIN_SCALE)));
  };

  // ---- Grid: heights, passability (water plus clearance), land masses ----
  std::vector<float> height(static_cast<size_t>(N) * N), landH(height.size()), wobble(height.size());
  std::vector<uint8_t> passable(height.size(), 0);
  const int clr = std::max(1, static_cast<int>(ceilf(Config::ROAD_WATER_CLEARANCE / Config::TERRAIN_SCALE)));
  for (int z = 0; z < N; ++z)
    for (int x = 0; x < N; ++x) {
      const int cx = static_cast<int>(x * cellsPerNode), cz = static_cast<int>(z * cellsPerNode);
      const float h = hmap.get(cx, cz);
      const int i = z * N + x;
      height[i] = h * hm;
      landH[i] = std::max(0.0f, (h - Config::SEA_LEVEL) / (1.0f - Config::SEA_LEVEL));
      bool dry = !wetCell(cx, cz);
      for (int k = 0; k < 8 && dry; ++k) {
        const float a = 0.785398f * k;
        dry = !wetCell(cx + static_cast<int>(lroundf(cosf(a) * clr)),
                       cz + static_cast<int>(lroundf(sinf(a) * clr)));
      }
      passable[i] = dry;
    }
  // Smooth cost variation (tiling value noise, ROAD_WOBBLE_PERIOD lattice
  // cells per world edge), so routes over open ground wander instead of
  // running dead straight along the grid's eight directions.
  {
    const int P = Config::ROAD_WOBBLE_PERIOD;
    auto lattice = [&](int x, int z) {
      x = ((x % P) + P) % P;
      z = ((z % P) + P) % P;
      uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u +
                   seed * 2654435761u;
      h = (h ^ (h >> 13)) * 1274126177u;
      return ((h ^ (h >> 16)) & 0xffffu) / 65536.0f;
    };
    for (int z = 0; z < N; ++z)
      for (int x = 0; x < N; ++x) {
        const float fx = static_cast<float>(x) * P / N, fz = static_cast<float>(z) * P / N;
        const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
        float tx = fx - ix, tz = fz - iz;
        tx = tx * tx * (3.0f - 2.0f * tx);
        tz = tz * tz * (3.0f - 2.0f * tz);
        const float a = lattice(ix, iz), b = lattice(ix + 1, iz);
        const float c = lattice(ix, iz + 1), d = lattice(ix + 1, iz + 1);
        const float v = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
        wobble[z * N + x] = 1.0f + Config::ROAD_WOBBLE * v;
      }
  }
  // Bridgeable (R.6): water and shore nodes within ROAD_BRIDGE_REACH of
  // passable land, found by a breadth-first distance from the land. Open
  // sea stays out of reach; narrow water can be crossed, at a price.
  std::vector<uint8_t> bridgeable(height.size(), 0);
  {
    const int reach = static_cast<int>(Config::ROAD_BRIDGE_REACH / G);
    std::vector<int> dist(height.size(), -1), q;
    for (int i = 0; i < N * N; ++i)
      if (passable[i]) {
        dist[i] = 0;
        q.push_back(i);
      }
    for (size_t k = 0; k < q.size(); ++k) {
      const int x = q[k] % N, z = q[k] / N;
      if (dist[q[k]] >= reach) continue;
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          const int j = idx(x + dx, z + dz);
          if (dist[j] < 0) {
            dist[j] = dist[q[k]] + 1;
            q.push_back(j);
          }
        }
    }
    for (int i = 0; i < N * N; ++i) bridgeable[i] = !passable[i] && dist[i] > 0;
  }
  auto canGo = [&](int i) { return passable[i] || bridgeable[i]; };
  std::vector<int> comp(height.size(), -1);
  {
    std::vector<int> q;
    int nc = 0;
    for (int i = 0; i < N * N; ++i) {
      if (comp[i] >= 0 || !canGo(i)) continue;
      q.assign(1, i);
      comp[i] = nc;
      for (size_t k = 0; k < q.size(); ++k) {
        const int x = q[k] % N, z = q[k] / N;
        for (int d = 0; d < 4; ++d) {
          const int j = idx(x + (d == 0) - (d == 1), z + (d == 2) - (d == 3));
          if (comp[j] < 0 && canGo(j)) {
            comp[j] = nc;
            q.push_back(j);
          }
        }
      }
      ++nc;
    }
  }

  // ---- A* / Dijkstra over the wrapped grid ----
  std::vector<uint8_t> onRoad(height.size(), 0);
  std::vector<float> g(height.size());
  std::vector<int> parent(height.size());
  std::vector<uint32_t> seen(height.size(), 0), closed(height.size(), 0);
  uint32_t searchId = 0;
  auto stepCost = [&](int from, int to, float dist) {
    const float grade = fabsf(height[to] - height[from]) / dist;
    float c = dist * wobble[to] *
              (1.0f + Config::ROAD_GRADE_COST * grade * grade + Config::ROAD_HEIGHT_COST * landH[to]);
    if (grade > Config::ROAD_MAX_GRADE) c *= Config::ROAD_STEEP_PENALTY;
    if (onRoad[to]) c *= Config::ROAD_REUSE_FACTOR;
    if (!passable[to]) c *= Config::ROAD_BRIDGE_COST; // over water: a bridge
    return c;
  };
  auto heuristic = [&](int a, int b) {
    int dx = abs(a % N - b % N), dz = abs(a / N - b / N);
    dx = std::min(dx, N - dx);
    dz = std::min(dz, N - dz);
    return G * (std::max(dx, dz) + 0.41421356f * std::min(dx, dz));
  };
  // goal >= 0: A* to that node. goal < 0: Dijkstra to the nearest road
  // node, giving up past maxCost. Path is start..end.
  auto search = [&](int start, int goal, float maxCost, std::vector<int> &path) {
    ++searchId;
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    g[start] = 0.0f;
    parent[start] = -1;
    seen[start] = searchId;
    open.push({goal >= 0 ? heuristic(start, goal) : 0.0f, start});
    int end = -1;
    while (!open.empty()) {
      const int n = open.top().second;
      open.pop();
      if (closed[n] == searchId) continue;
      closed[n] = searchId;
      if (n == goal || (goal < 0 && onRoad[n] && n != start)) {
        end = n;
        break;
      }
      if (g[n] > maxCost) break;
      const int x = n % N, z = n / N;
      for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dz) continue;
          const int m = idx(x + dx, z + dz);
          if (!canGo(m) || closed[m] == searchId) continue;
          const float ng = g[n] + stepCost(n, m, (dx && dz) ? G * 1.41421356f : G);
          if (seen[m] != searchId || ng < g[m]) {
            seen[m] = searchId;
            g[m] = ng;
            parent[m] = n;
            open.push({ng + (goal >= 0 ? heuristic(m, goal) : 0.0f), m});
          }
        }
    }
    path.clear();
    if (end < 0) return false;
    for (int n = end; n >= 0; n = parent[n]) path.push_back(n);
    std::reverse(path.begin(), path.end());
    return true;
  };
  // Grid path → smoothed polyline in unwrapped coordinates.
  auto addRoad = [&](const std::vector<int> &path, Class cls) {
    if (path.size() < 2) return;
    for (int n : path) onRoad[n] = 1;
    Poly raw;
    float px = (path[0] % N + 0.5f) * G, pz = (path[0] / N + 0.5f) * G;
    raw.push_back({px, pz});
    for (size_t i = 1; i < path.size(); ++i) {
      int dx = path[i] % N - path[i - 1] % N, dz = path[i] / N - path[i - 1] / N;
      if (dx > N / 2) dx -= N;
      if (dx < -N / 2) dx += N;
      if (dz > N / 2) dz -= N;
      if (dz < -N / 2) dz += N;
      px += dx * G;
      pz += dz * G;
      raw.push_back({px, pz});
    }
    // Smooth, then check the smoothed road stays clear of water as drawn
    // (a terrain triangle is water when any corner is) across its full
    // width; fall back to the raw grid path, which kept its clearance, if
    // smoothing cut a corner. A path that bridges water keeps its raw
    // shape too.
    const float reach = halfWidthOf(cls) + 2.0f;
    auto wetDrawn = [&](float x, float z) {
      const float c = Config::TERRAIN_SCALE;
      const float x0 = floorf(x / c) * c, z0 = floorf(z / c) * c;
      return wetAt(x0, z0) || wetAt(x0 + c, z0) || wetAt(x0, z0 + c) || wetAt(x0 + c, z0 + c);
    };
    Poly smooth = simplified(chaikin(chaikin(raw)), Config::ROAD_SIMPLIFY_TOL);
    bool dry = true;
    for (size_t i = 0; i + 1 < smooth.size() && dry; ++i) {
      const Point a = smooth[i], b = smooth[i + 1];
      const float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
      if (len < 1e-3f) continue;
      const float nx = -(b.z - a.z) / len * reach, nz = (b.x - a.x) / len * reach;
      const int steps = std::max(1, static_cast<int>(len / 4.0f));
      for (int s = 0; s <= steps && dry; ++s) {
        const float x = a.x + (b.x - a.x) * s / steps, z = a.z + (b.z - a.z) * s / steps;
        dry = !wetDrawn(x, z) && !wetDrawn(x + nx, z + nz) && !wetDrawn(x - nx, z - nz);
      }
    }
    m_roads.push_back({cls, dry ? smooth : simplified(raw, 0.01f)});
  };
  // Nearest passable node to a site centre.
  auto nodeOf = [&](const SettlementSite &s) {
    const int x0 = static_cast<int>(s.x / G), z0 = static_cast<int>(s.z / G);
    for (int r = 0; r <= 6; ++r)
      for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx) {
          if (std::max(abs(dx), abs(dz)) != r) continue;
          const int i = idx(x0 + dx, z0 + dz);
          if (passable[i]) return i;
        }
    return -1;
  };
  auto wrapDist = [&](const SettlementSite &a, const SettlementSite &b) {
    float dx = b.x - a.x, dz = b.z - a.z;
    dx -= roundf(dx / ws) * ws;
    dz -= roundf(dz / ws) * ws;
    return sqrtf(dx * dx + dz * dz);
  };

  const auto &sites = settlements.sites();
  std::vector<int> node(sites.size());
  for (size_t i = 0; i < sites.size(); ++i) node[i] = nodeOf(sites[i]);
  auto compOf = [&](size_t i) { return node[i] >= 0 ? comp[node[i]] : -1; };
  using K = SettlementSite::Kind;
  std::vector<size_t> towns, villages, farms;
  for (size_t i = 0; i < sites.size(); ++i)
    (sites[i].kind == K::Town ? towns : sites[i].kind == K::Village ? villages : farms).push_back(i);
  std::vector<int> path;
  std::vector<bool> connected(sites.size(), false);

  // ---- Main roads: spanning tree over the towns of each land mass,
  // plus the shortest ROAD_EXTRA_TOWN_LINKS other pairs for loops ----
  report("Routing main roads", 0.0f);
  {
    std::vector<std::pair<size_t, size_t>> links;
    std::vector<bool> inTree(towns.size(), false);
    for (size_t root = 0; root < towns.size(); ++root) {
      if (inTree[root] || compOf(towns[root]) < 0) continue;
      inTree[root] = true; // Prim within this land mass
      for (;;) {
        float best = 1e30f;
        size_t bi = 0, bj = 0;
        for (size_t i = 0; i < towns.size(); ++i) {
          if (!inTree[i] || compOf(towns[i]) != compOf(towns[root])) continue;
          for (size_t j = 0; j < towns.size(); ++j) {
            if (inTree[j] || compOf(towns[j]) != compOf(towns[root])) continue;
            const float d = wrapDist(sites[towns[i]], sites[towns[j]]);
            if (d < best) {
              best = d;
              bi = i;
              bj = j;
            }
          }
        }
        if (best >= 1e30f) break;
        inTree[bj] = true;
        links.push_back({towns[bi], towns[bj]});
      }
    }
    std::vector<std::pair<float, std::pair<size_t, size_t>>> extra;
    for (size_t i = 0; i < towns.size(); ++i)
      for (size_t j = i + 1; j < towns.size(); ++j) {
        const auto pr = std::make_pair(towns[i], towns[j]);
        if (compOf(towns[i]) < 0 || compOf(towns[i]) != compOf(towns[j])) continue;
        if (std::find(links.begin(), links.end(), pr) != links.end() ||
            std::find(links.begin(), links.end(), std::make_pair(pr.second, pr.first)) != links.end())
          continue;
        extra.push_back({wrapDist(sites[towns[i]], sites[towns[j]]), pr});
      }
    std::sort(extra.begin(), extra.end());
    for (int k = 0; k < Config::ROAD_EXTRA_TOWN_LINKS && k < static_cast<int>(extra.size()); ++k)
      links.push_back(extra[k].second);
    for (size_t k = 0; k < links.size(); ++k) {
      report("Routing main roads", 0.4f * k / links.size());
      if (search(node[links[k].first], node[links[k].second], 1e30f, path)) {
        addRoad(path, Class::Main);
        connected[links[k].first] = connected[links[k].second] = true;
      }
    }
    for (size_t t : towns) connected[t] = connected[t] || compOf(t) >= 0;
  }

  // ---- Main roads come straight into town ----
  // Inside a town a routed main road staircases between grid nodes.
  // Every stretch of main road inside a town's street reach is replaced
  // by straight spokes through the square: edge to centre for a road that
  // ends there, edge to centre to edge for one passing through. The
  // street grid is then aligned with one of the spokes. Roads entering
  // within TOWN_SPOKE_MERGE of each other (they shared a route outside
  // town) share a spoke. A spoke that would cross water keeps the route.
  std::vector<std::vector<Point>> townEntries(sites.size());
  auto straightLeg = [&](Point from, Point to) {
    const float len = hypotf(to.x - from.x, to.z - from.z);
    for (float s = 0.0f; s <= len; s += 4.0f) {
      const float px = from.x + (to.x - from.x) * s / len, pz = from.z + (to.z - from.z) * s / len;
      if (!passable[idx(static_cast<int>(floorf(px / G)), static_cast<int>(floorf(pz / G)))]) return false;
    }
    return true;
  };
  auto markLeg = [&](Point from, Point to) {
    const float len = hypotf(to.x - from.x, to.z - from.z);
    for (float s = 0.0f; s <= len; s += G * 0.5f) {
      const float px = from.x + (to.x - from.x) * s / len, pz = from.z + (to.z - from.z) * s / len;
      onRoad[idx(static_cast<int>(floorf(px / G)), static_cast<int>(floorf(pz / G)))] = 1;
    }
  };
  for (Road &r : m_roads) {
    if (r.cls != Class::Main || r.pts.size() < 2) continue;
    for (size_t t : towns) {
      const SettlementSite &site = sites[t];
      const float reach = Config::TOWN_STREET_REACH * site.radius;
      Poly &pts = r.pts;
      // The town centre in the road's frame, and which points lie inside.
      float dx = site.x - pts[0].x, dz = site.z - pts[0].z;
      dx -= roundf(dx / ws) * ws;
      dz -= roundf(dz / ws) * ws;
      const Point c{pts[0].x + dx, pts[0].z + dz};
      auto inside = [&](const Point &q) {
        float ex = q.x - c.x, ez = q.z - c.z;
        ex -= roundf(ex / ws) * ws; // a long road may wrap away from c
        ez -= roundf(ez / ws) * ws;
        return ex * ex + ez * ez < reach * reach;
      };
      size_t i0 = 0;
      while (i0 < pts.size() && !inside(pts[i0])) ++i0;
      if (i0 >= pts.size()) continue;
      size_t i1 = i0;
      while (i1 + 1 < pts.size() && inside(pts[i1 + 1])) ++i1;
      if (i0 == 0 && i1 + 1 == pts.size()) continue; // wholly inside: leave it
      // The town centre and crossing points in this stretch's own frame.
      float cx = site.x - pts[i0].x, cz = site.z - pts[i0].z;
      cx -= roundf(cx / ws) * ws;
      cz -= roundf(cz / ws) * ws;
      const Point cc{pts[i0].x + cx, pts[i0].z + cz};
      auto crossing = [&](const Point &in, const Point &out) {
        const float da = hypotf(in.x - cc.x, in.z - cc.z), db = hypotf(out.x - cc.x, out.z - cc.z);
        const float f = db > da ? (reach - da) / (db - da) : 0.0f;
        Point x{in.x + (out.x - in.x) * f, in.z + (out.z - in.z) * f};
        for (const Point &q : townEntries[t]) {
          float qx = q.x - x.x, qz = q.z - x.z;
          qx -= roundf(qx / ws) * ws;
          qz -= roundf(qz / ws) * ws;
          if (qx * qx + qz * qz < Config::TOWN_SPOKE_MERGE * Config::TOWN_SPOKE_MERGE) return Point{x.x + qx, x.z + qz};
        }
        return x;
      };
      Poly mid;
      bool ok = true;
      if (i0 > 0) {
        const Point x1 = crossing(pts[i0], pts[i0 - 1]);
        ok = ok && straightLeg(x1, cc);
        mid.push_back(x1);
      }
      mid.push_back(cc);
      if (i1 + 1 < pts.size()) {
        const Point x2 = crossing(pts[i1], pts[i1 + 1]);
        ok = ok && straightLeg(cc, x2);
        mid.push_back(x2);
      }
      if (!ok) continue;
      for (size_t k = 0; k < mid.size(); ++k) {
        if (k + 1 < mid.size()) markLeg(mid[k], mid[k + 1]);
        if (k != 0 || i0 > 0) townEntries[t].push_back(mid[k]);
      }
      Poly out(pts.begin(), pts.begin() + i0);
      out.insert(out.end(), mid.begin(), mid.end());
      out.insert(out.end(), pts.begin() + i1 + 1, pts.end());
      pts.swap(out);
    }
  }

  // ---- Town streets: a rough grid round each town centre ----
  // Lines every TOWN_STREET_SPACING across and along the main road's
  // direction near the centre, offset half a block so the main road is
  // the high street; each line is cut where it meets water or steep
  // ground. A paved square sits on the centre.
  report("Laying out town streets", 0.4f);
  for (size_t t : towns) {
    const SettlementSite &site = sites[t];
    if (node[t] < 0) continue;
    // Axis: the nearest existing road's direction near the centre.
    float ux = 1.0f, uz = 0.0f, best = 1e30f;
    for (const Road &r : m_roads)
      for (size_t i = 0; i + 1 < r.pts.size(); ++i) {
        float dx = r.pts[i].x - site.x, dz = r.pts[i].z - site.z;
        dx -= roundf(dx / ws) * ws;
        dz -= roundf(dz / ws) * ws;
        const float d2 = dx * dx + dz * dz;
        const float ex = r.pts[i + 1].x - r.pts[i].x, ez = r.pts[i + 1].z - r.pts[i].z;
        const float l = sqrtf(ex * ex + ez * ez);
        if (d2 < best && l > 1.0f) {
          best = d2;
          ux = ex / l;
          uz = ez / l;
        }
      }
    const float vx = -uz, vz = ux;
    const float R = Config::TOWN_STREET_REACH * site.radius, S = Config::TOWN_STREET_SPACING;
    auto dryGentle = [&](float x, float z) {
      const int i = idx(static_cast<int>(floorf(x / G)), static_cast<int>(floorf(z / G)));
      return passable[i] != 0;
    };
    uint32_t h = static_cast<uint32_t>(t) * 2654435761u ^ seed;
    auto jit = [&]() {
      h ^= h << 13;
      h ^= h >> 17;
      h ^= h << 5;
      return ((h & 0xffffu) / 65536.0f - 0.5f) * 2.0f;
    };
    std::vector<Poly> townRuns;
    for (int dirn = 0; dirn < 2; ++dirn) {
      const float ax = dirn ? vx : ux, az = dirn ? vz : uz;   // along the line
      const float px = dirn ? ux : vx, pz = dirn ? uz : vz;   // across the lines
      const int n = static_cast<int>(R / S);
      for (int k = -n; k < n; ++k) {
        const float off = (k + 0.5f) * S;
        if (fabsf(off) >= R) continue;
        const float half = sqrtf(R * R - off * off);
        const float tilt = jit() * Config::TOWN_STREET_TILT;
        const float lx = ax * cosf(tilt) - az * sinf(tilt), lz = ax * sinf(tilt) + az * cosf(tilt);
        Poly run;
        float prevH = 0.0f;
        auto flush = [&]() {
          if (run.size() >= 3) townRuns.push_back(run);
          run.clear();
        };
        for (float s = -half; s <= half; s += 8.0f) {
          const float x = site.x + px * off + lx * s, z = site.z + pz * off + lz * s;
          const float hh = hmap.get(static_cast<int>(lroundf(x / Config::TERRAIN_SCALE)),
                                    static_cast<int>(lroundf(z / Config::TERRAIN_SCALE))) * hm;
          const bool ok = dryGentle(x, z) &&
                          (run.empty() || fabsf(hh - prevH) / 8.0f < Config::TOWN_STREET_MAX_GRADE);
          if (!ok) {
            flush();
            continue;
          }
          run.push_back({x, z});
          prevH = hh;
        }
        flush();
      }
    }
    // Trim each street to its first and last junction (with another
    // street or a road through town), so the grid has no dead-end stubs
    // running out into the fields; a line with fewer than two junctions
    // goes. Repeat until nothing changes: a junction only counts if the
    // street it meets survives its own trimming.
    std::vector<Poly> through; // roads through town, in this town's frame
    for (const Road &r : m_roads) {
      if (r.pts.size() < 2) continue;
      Poly near;
      for (const Point &q : r.pts) {
        float dx = q.x - site.x, dz = q.z - site.z;
        dx -= roundf(dx / ws) * ws;
        dz -= roundf(dz / ws) * ws;
        near.push_back({site.x + dx, site.z + dz});
      }
      through.push_back(near);
    }
    auto crossAt = [](Point a, Point b, Point c, Point d, float &t) {
      const float rx = b.x - a.x, rz = b.z - a.z, sx = d.x - c.x, sz = d.z - c.z;
      const float den = rx * sz - rz * sx;
      if (fabsf(den) < 1e-6f) return false;
      t = ((c.x - a.x) * sz - (c.z - a.z) * sx) / den;
      const float u = ((c.x - a.x) * rz - (c.z - a.z) * rx) / den;
      // A little slack (about a metre on 8 m segments) so a street that
      // ends exactly on another still counts as meeting it after trimming.
      constexpr float kSlack = 0.15f;
      if (t < -kSlack || t > 1.0f + kSlack || u < -kSlack || u > 1.0f + kSlack) return false;
      t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      return true;
    };
    for (int pass = 0; pass < 6; ++pass) {
      std::vector<Poly> next;
      bool changed = false;
      for (size_t ri = 0; ri < townRuns.size(); ++ri) {
        const Poly &run = townRuns[ri];
        float lo = 1e30f, hi = -1e30f; // junction positions, in segment units
        auto scan = [&](const Poly &o) {
          for (size_t i = 0; i + 1 < run.size(); ++i)
            for (size_t j = 0; j + 1 < o.size(); ++j) {
              float u;
              if (!crossAt(run[i], run[i + 1], o[j], o[j + 1], u)) continue;
              lo = fminf(lo, i + u);
              hi = fmaxf(hi, i + u);
            }
        };
        for (size_t oi = 0; oi < townRuns.size(); ++oi)
          if (oi != ri) scan(townRuns[oi]);
        for (const Poly &o : through) scan(o);
        if (hi - lo < 1.0f) {
          changed = true;
          continue;
        }
        auto at = [&](float u) {
          const size_t i = std::min(static_cast<size_t>(u), run.size() - 2);
          const float f = u - i;
          return Point{run[i].x + (run[i + 1].x - run[i].x) * f, run[i].z + (run[i + 1].z - run[i].z) * f};
        };
        Poly trimmed{at(lo)};
        for (size_t i = static_cast<size_t>(lo) + 1; i <= static_cast<size_t>(hi) && i < run.size(); ++i)
          trimmed.push_back(run[i]);
        trimmed.push_back(at(hi));
        const float before = hypotf(run.front().x - run.back().x, run.front().z - run.back().z);
        const float after = hypotf(trimmed.front().x - trimmed.back().x, trimmed.front().z - trimmed.back().z);
        changed = changed || after < before - 0.5f;
        next.push_back(trimmed);
      }
      townRuns.swap(next);
      if (!changed) break;
    }
    for (const Poly &trimmed : townRuns) {
      m_roads.push_back({Class::Street, trimmed});
      for (const Point &q : trimmed)
        onRoad[idx(static_cast<int>(floorf(q.x / G)), static_cast<int>(floorf(q.z / G)))] = 1;
    }
    if (passable[node[t]]) m_roads.push_back({Class::Square, {{site.x, site.z}}});
  }

  // ---- Lanes: each village to the nearest connected settlement on its
  // land mass, nearest-to-the-network first, cut where they meet road ----
  std::sort(villages.begin(), villages.end(), [&](size_t a, size_t b) {
    auto nearestTown = [&](size_t v) {
      float d = 1e30f;
      for (size_t t : towns)
        if (compOf(t) == compOf(v)) d = std::min(d, wrapDist(sites[v], sites[t]));
      return d;
    };
    return nearestTown(a) < nearestTown(b);
  });
  for (size_t k = 0; k < villages.size(); ++k) {
    report("Routing lanes", 0.4f + 0.3f * k / std::max<size_t>(1, villages.size()));
    const size_t v = villages[k];
    if (compOf(v) < 0) {
      ++m_unconnected;
      continue;
    }
    float best = 1e30f;
    long target = -1;
    for (size_t i = 0; i < sites.size(); ++i) {
      if (!connected[i] || i == v || compOf(i) != compOf(v) || sites[i].kind == K::Farm) continue;
      const float d = wrapDist(sites[v], sites[i]);
      if (d < best) {
        best = d;
        target = static_cast<long>(i);
      }
    }
    if (target < 0) { // first settlement on a land mass with no town
      connected[v] = true;
      continue;
    }
    if (search(node[v], node[target], 1e30f, path)) {
      for (size_t i = 1; i < path.size(); ++i)
        if (onRoad[path[i]]) {
          path.resize(i + 1);
          break;
        }
      addRoad(path, Class::Lane);
      connected[v] = true;
    } else {
      ++m_unconnected;
    }
  }

  // ---- Tracks: each farmstead to the nearest road ----
  for (size_t k = 0; k < farms.size(); ++k) {
    if (k % 16 == 0) report("Routing farm tracks", 0.7f + 0.3f * k / std::max<size_t>(1, farms.size()));
    const size_t f = farms[k];
    if (node[f] < 0 || onRoad[node[f]]) {
      m_unconnected += node[f] < 0;
      continue;
    }
    if (search(node[f], -1, Config::FARM_TRACK_MAX_COST, path))
      addRoad(path, Class::Track);
    else
      ++m_unconnected;
  }

  // ---- Join road ends to the road they stop at ----
  // A route that stops on reaching existing road ends at a grid node,
  // which can sit metres off that road's line; street trimming leaves
  // ends on a junction. Give every end within ROAD_SNAP_DIST of another
  // road one more point, on that road, so the two meet.
  for (size_t ri = 0; ri < m_roads.size(); ++ri) {
    if (m_roads[ri].pts.size() < 2) continue;
    for (int end = 0; end < 2; ++end) {
      Poly &pts = m_roads[ri].pts;
      const Point e = end ? pts.back() : pts.front();
      float best = Config::ROAD_SNAP_DIST * Config::ROAD_SNAP_DIST;
      Point hit{};
      bool found = false;
      for (size_t oi = 0; oi < m_roads.size(); ++oi) {
        if (oi == ri) continue;
        const Poly &o = m_roads[oi].pts;
        for (size_t j = 0; j + 1 < o.size(); ++j) {
          float ax = o[j].x - e.x, az = o[j].z - e.z;
          const float sx = roundf(ax / ws) * ws, sz = roundf(az / ws) * ws;
          ax -= sx;
          az -= sz;
          const float bx = o[j + 1].x - sx - e.x, bz = o[j + 1].z - sz - e.z;
          const float ex = bx - ax, ez = bz - az, l2 = ex * ex + ez * ez;
          float t = l2 > 0.0f ? -(ax * ex + az * ez) / l2 : 0.0f;
          t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
          const float px = ax + ex * t, pz = az + ez * t, d2 = px * px + pz * pz;
          if (d2 < best) {
            best = d2;
            hit = {e.x + px, e.z + pz};
            found = true;
          }
        }
      }
      if (!found || best < 0.25f) continue;
      if (end) pts.push_back(hit);
      else pts.insert(pts.begin(), hit);
    }
  }

  buildShapes();
  report("Roads complete", 1.0f);
}

void RoadNetwork::addShape(int cls, std::vector<Point> poly) {
  if (area2(poly) < 0.0f) std::reverse(poly.begin(), poly.end());
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (const Point &p : poly) {
    x0 = fminf(x0, p.x);
    z0 = fminf(z0, p.z);
    x1 = fmaxf(x1, p.x);
    z1 = fmaxf(z1, p.z);
  }
  const float ws = Config::WORLD_SIZE;
  const float cell = Config::ROAD_INDEX_CELL;
  // Fold the shape into the world square, adding a copy for each edge it
  // crosses so triangles on either side find it.
  const float bx = floorf(x0 / ws) * ws, bz = floorf(z0 / ws) * ws;
  for (float sx : {-bx, -bx - ws})
    for (float sz : {-bz, -bz - ws}) {
      if (x1 + sx < 0.0f || x0 + sx > ws || z1 + sz < 0.0f || z0 + sz > ws) continue;
      Shape s{cls, poly, x0 + sx, z0 + sz, x1 + sx, z1 + sz};
      for (Point &p : s.poly) {
        p.x += sx;
        p.z += sz;
      }
      const int id = static_cast<int>(m_shapes.size());
      m_shapes.push_back(std::move(s));
      const Shape &r = m_shapes.back();
      const int cx0 = std::max(0, static_cast<int>(r.x0 / cell));
      const int cz0 = std::max(0, static_cast<int>(r.z0 / cell));
      const int cx1 = std::min(m_indexN - 1, static_cast<int>(r.x1 / cell));
      const int cz1 = std::min(m_indexN - 1, static_cast<int>(r.z1 / cell));
      for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) m_index[static_cast<size_t>(cz) * m_indexN + cx].push_back(id);
    }
}

void RoadNetwork::addSegment(Segment s) {
  const float ws = Config::WORLD_SIZE;
  const float cell = Config::ROAD_INDEX_CELL;
  const float x0 = fminf(s.a.x, s.b.x) - s.halfWidth, x1 = fmaxf(s.a.x, s.b.x) + s.halfWidth;
  const float z0 = fminf(s.a.z, s.b.z) - s.halfWidth, z1 = fmaxf(s.a.z, s.b.z) + s.halfWidth;
  const float bx = floorf(x0 / ws) * ws, bz = floorf(z0 / ws) * ws;
  for (float sx : {-bx, -bx - ws})
    for (float sz : {-bz, -bz - ws}) {
      if (x1 + sx < 0.0f || x0 + sx > ws || z1 + sz < 0.0f || z0 + sz > ws) continue;
      const int id = static_cast<int>(m_segments.size());
      m_segments.push_back({{s.a.x + sx, s.a.z + sz}, {s.b.x + sx, s.b.z + sz}, s.halfWidth});
      const int cx0 = std::max(0, static_cast<int>((x0 + sx) / cell));
      const int cz0 = std::max(0, static_cast<int>((z0 + sz) / cell));
      const int cx1 = std::min(m_indexN - 1, static_cast<int>((x1 + sx) / cell));
      const int cz1 = std::min(m_indexN - 1, static_cast<int>((z1 + sz) / cell));
      for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx)
          m_segIndex[static_cast<size_t>(cz) * m_indexN + cx].push_back(id);
    }
}

float RoadNetwork::edgeDistance(float x, float z, float maxDist) const {
  if (m_indexN == 0) return maxDist;
  const float ws = Config::WORLD_SIZE;
  const float cell = Config::ROAD_INDEX_CELL;
  x = fmodf(x, ws);
  z = fmodf(z, ws);
  if (x < 0.0f) x += ws;
  if (z < 0.0f) z += ws;
  float best = maxDist;
  const int cx0 = static_cast<int>(floorf((x - maxDist) / cell));
  const int cx1 = static_cast<int>(floorf((x + maxDist) / cell));
  const int cz0 = static_cast<int>(floorf((z - maxDist) / cell));
  const int cz1 = static_cast<int>(floorf((z + maxDist) / cell));
  for (int cz = cz0; cz <= cz1; ++cz)
    for (int cx = cx0; cx <= cx1; ++cx) {
      // Cells past the world edge hold segments shifted by a world width.
      const int wx = ((cx % m_indexN) + m_indexN) % m_indexN;
      const int wz = ((cz % m_indexN) + m_indexN) % m_indexN;
      const float ox = (cx - wx) * cell, oz = (cz - wz) * cell;
      for (int id : m_segIndex[static_cast<size_t>(wz) * m_indexN + wx]) {
        const Segment &s = m_segments[id];
        const float ax = s.a.x + ox, az = s.a.z + oz, bx = s.b.x + ox, bz = s.b.z + oz;
        const float ex = bx - ax, ez = bz - az, l2 = ex * ex + ez * ez;
        float t = l2 > 0.0f ? ((x - ax) * ex + (z - az) * ez) / l2 : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float dx = x - (ax + ex * t), dz = z - (az + ez * t);
        best = fminf(best, sqrtf(dx * dx + dz * dz) - s.halfWidth);
      }
    }
  return best;
}

bool RoadNetwork::nearestPoint(float x, float z, float maxDist, Point &p, Point &dir,
                               float &halfWidth) const {
  if (m_indexN == 0) return false;
  const float ws = Config::WORLD_SIZE;
  const float cell = Config::ROAD_INDEX_CELL;
  // Search in the world square, report in the caller's frame.
  float qx = fmodf(x, ws), qz = fmodf(z, ws);
  if (qx < 0.0f) qx += ws;
  if (qz < 0.0f) qz += ws;
  const float shiftX = x - qx, shiftZ = z - qz;
  float best = maxDist * maxDist;
  bool found = false;
  for (int cz = static_cast<int>(floorf((qz - maxDist) / cell)); cz <= static_cast<int>(floorf((qz + maxDist) / cell)); ++cz)
    for (int cx = static_cast<int>(floorf((qx - maxDist) / cell)); cx <= static_cast<int>(floorf((qx + maxDist) / cell)); ++cx) {
      const int wx = ((cx % m_indexN) + m_indexN) % m_indexN;
      const int wz = ((cz % m_indexN) + m_indexN) % m_indexN;
      const float ox = (cx - wx) * cell, oz = (cz - wz) * cell;
      for (int id : m_segIndex[static_cast<size_t>(wz) * m_indexN + wx]) {
        const Segment &s = m_segments[id];
        const float ax = s.a.x + ox, az = s.a.z + oz, bx = s.b.x + ox, bz = s.b.z + oz;
        const float ex = bx - ax, ez = bz - az, l2 = ex * ex + ez * ez;
        float t = l2 > 0.0f ? ((qx - ax) * ex + (qz - az) * ez) / l2 : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float px = ax + ex * t, pz = az + ez * t;
        const float d2 = (qx - px) * (qx - px) + (qz - pz) * (qz - pz);
        if (d2 < best && l2 > 0.0f) {
          best = d2;
          found = true;
          p = {px + shiftX, pz + shiftZ};
          const float l = sqrtf(l2);
          dir = {ex / l, ez / l};
          halfWidth = s.halfWidth;
        }
      }
    }
  return found;
}

void RoadNetwork::buildShapes() {
  m_shapes.clear();
  m_segments.clear();
  m_indexN = static_cast<int>(Config::WORLD_SIZE / Config::ROAD_INDEX_CELL);
  m_index.assign(static_cast<size_t>(m_indexN) * m_indexN, {});
  m_segIndex.assign(static_cast<size_t>(m_indexN) * m_indexN, {});
  for (const Road &r : m_roads) {
    const float w = halfWidthOf(r.cls);
    const int cls = static_cast<int>(r.cls);
    for (size_t i = 0; i + 1 < r.pts.size(); ++i) {
      const Point a = r.pts[i], b = r.pts[i + 1];
      const float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
      if (len < 1e-3f) continue;
      const float ux = (b.x - a.x) / len, uz = (b.z - a.z) / len;
      const float nx = -uz * w, nz = ux * w;
      addShape(cls, {{a.x + nx, a.z + nz}, {b.x + nx, b.z + nz}, {b.x - nx, b.z - nz}, {a.x - nx, a.z - nz}});
      addSegment({a, b, w});
    }
    if (r.pts.size() == 1) addSegment({r.pts[0], r.pts[0], w}); // a square: a disc
    // Octagons at every vertex fill the wedge at bends and round the ends.
    for (const Point &p : r.pts) {
      Poly oct;
      for (int k = 0; k < 8; ++k)
        oct.push_back({p.x + w * cosf(0.785398f * k + 0.3927f), p.z + w * sinf(0.785398f * k + 0.3927f)});
      addShape(cls, oct);
    }
  }
  m_stamp.assign(m_shapes.size(), 0);
  m_stampGen = 0;
}

void RoadNetwork::classify(const std::vector<Point> &poly, std::vector<Piece> &out) const {
  float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
  for (const Point &p : poly) {
    x0 = fminf(x0, p.x);
    z0 = fminf(z0, p.z);
    x1 = fmaxf(x1, p.x);
    z1 = fmaxf(z1, p.z);
  }
  // Candidate shapes from the index, widest class first.
  std::vector<int> cand;
  if (m_indexN > 0) {
    const float cell = Config::ROAD_INDEX_CELL;
    ++m_stampGen;
    const int cx0 = std::max(0, static_cast<int>(x0 / cell));
    const int cz0 = std::max(0, static_cast<int>(z0 / cell));
    const int cx1 = std::min(m_indexN - 1, static_cast<int>(x1 / cell));
    const int cz1 = std::min(m_indexN - 1, static_cast<int>(z1 / cell));
    for (int cz = cz0; cz <= cz1; ++cz)
      for (int cx = cx0; cx <= cx1; ++cx)
        for (int id : m_index[static_cast<size_t>(cz) * m_indexN + cx]) {
          const Shape &s = m_shapes[id];
          if (m_stamp[id] == m_stampGen || s.x1 < x0 || s.x0 > x1 || s.z1 < z0 || s.z0 > z1) continue;
          m_stamp[id] = m_stampGen;
          cand.push_back(id);
        }
  }
  if (cand.empty()) {
    out.push_back({-1, poly});
    return;
  }
  std::stable_sort(cand.begin(), cand.end(),
                   [&](int a, int b) { return m_shapes[a].cls < m_shapes[b].cls; });
  std::vector<Piece> pieces{{-1, poly}}, next;
  Poly rest, in, outPart;
  for (int id : cand) {
    const Shape &s = m_shapes[id];
    next.clear();
    for (Piece &pc : pieces) {
      if (pc.cls >= 0) {
        next.push_back(std::move(pc));
        continue;
      }
      // Peel off the parts outside each edge; what remains is inside.
      rest = pc.poly;
      for (size_t e = 0; e < s.poly.size() && !rest.empty(); ++e) {
        splitByLine(rest, s.poly[e], s.poly[(e + 1) % s.poly.size()], in, outPart);
        if (!outPart.empty()) next.push_back({-1, outPart});
        rest.swap(in);
      }
      if (!rest.empty()) next.push_back({s.cls, rest});
    }
    pieces.swap(next);
  }
  for (Piece &pc : pieces) out.push_back(std::move(pc));
}
