#include "Radar.hpp"

#include "entity/EntityManager.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>

// ====================================================================
// IFF colour palette — drawn from radar_system.md.
// Altitude-tinted at draw time; raw values held here.
// ====================================================================
namespace {

constexpr Color COL_ENEMY_CRAFT = {220, 50, 50, 255};
constexpr Color COL_ENEMY_PROJ = {255, 140, 0, 255};
constexpr Color COL_FRIENDLY = {60, 200, 80, 255};
constexpr Color COL_PLAYER = {240, 240, 240, 255};
constexpr Color COL_PICKUP = {220, 200, 50, 255};
constexpr Color COL_BASE = {160, 170, 185, 255};

// Map an EntityType to a BlipType + IFF colour. Defaults to enemy red
// for unknown types. Pickups / friendly units / terrain objects are
// added when those entity classes ship.
BlipType blipTypeFromEntity(EntityType t) {
  switch (t) {
  case EntityType::Drone:        return BlipType::Drone;
  case EntityType::Seeder:       return BlipType::Seeder;
  case EntityType::Fighter:      return BlipType::Fighter;
  case EntityType::Bomber:       return BlipType::Bomber;
  case EntityType::Carrier:      return BlipType::Carrier;
  case EntityType::GroundTurret: return BlipType::Turret;
  case EntityType::Collector:
  case EntityType::RepairStation:
  case EntityType::RadarBooster: return BlipType::Friendly;
  case EntityType::RadarStation: return BlipType::RadarStation;
  case EntityType::Base:         return BlipType::Base;
  default:                       return BlipType::Fighter;
  }
}

Color baseColourForBlip(BlipType t) {
  switch (t) {
  case BlipType::Friendly:
  case BlipType::RadarStation: return COL_FRIENDLY;
  case BlipType::Pickup:    return COL_PICKUP;
  case BlipType::Base:
  case BlipType::LaunchPad: return COL_BASE;
  case BlipType::Ghost:     return {180, 60, 60, 120};
  default:                  return COL_ENEMY_CRAFT;
  }
}

// Toroidal world: the copy of p nearest ref, so contacts across a world
// edge are measured and drawn where the player actually sees them.
inline Vector3 nearestCopy(Vector3 p, Vector3 ref) {
  const float ws = Config::WORLD_SIZE;
  p.x -= roundf((p.x - ref.x) / ws) * ws;
  p.z -= roundf((p.z - ref.z) / ws) * ws;
  return p;
}

inline unsigned char clampU(float v) {
  if (v < 0.0f) return 0;
  if (v > 255.0f) return 255;
  return static_cast<unsigned char>(v);
}

} // namespace

// ====================================================================
// update — rebuild the contacts list from the EntityManager's pool.
// O(N) over the entity pool; pool is bounded so this stays cheap.
// Range filter happens at draw time so altitude-strip dots can show
// contacts that are out-of-range horizontally but matching vertically.
// ====================================================================
void Radar::update(float dt, const EntityManager &entities, Vector3 playerPos,
                   float playerYaw, float gameTime) {
  m_playerPos = playerPos;
  m_playerYaw = playerYaw;
  // Pulse window for this tick. First tick has an empty window so the
  // screen starts dark and fills in as the first pulse sweeps out.
  const float t0 = m_prevGameTime < 0.0f ? gameTime : m_prevGameTime;
  m_prevGameTime = gameTime;
  m_gameTime = gameTime;
  m_activeRange = Config::RADAR_BASE_RANGE;
  m_contacts.clear();

  // C.4 — online detection nodes (towers / boosters whose cluster holds a
  // living base). They extend coverage and paint contacts with their
  // own pulse; the disc border brightens while the network feeds you.
  m_stationCount = 0;
  const auto &pool = entities.entities();
  for (const Entity &e : pool) {
    if (!e.alive || !e.networkOnline || !isDetectionNode(e.type)) continue;
    if (m_stationCount >= Config::RADAR_NODE_MAX) break;
    m_stations[static_cast<size_t>(m_stationCount++)] = nearestCopy(e.pos, playerPos);
  }
  m_boosted = m_stationCount > 0;
  // Tower rings travel at the player's ring speed, starting
  // RADAR_STATION_PULSE_OFFSET into each period.
  const float ringSpeed = Config::RADAR_BASE_RANGE / Config::RADAR_PULSE_SWEEP_TIME;
  const float nodeR2 = Config::RADAR_STATION_RANGE * Config::RADAR_STATION_RANGE;
  // Tier 2 jam factor — scan for the nearest live Carrier. Inside
  // RADAR_JAM_RANGE, jamFactor lerps 0 → 1 as the player closes.
  // Friendlies and other enemy types don't jam.
  float carrierDist = 1e9f;
  for (size_t i = 0; i < pool.size(); ++i) {
    const Entity &e = pool[i];
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue;

    const Vector3 epos = nearestCopy(e.pos, playerPos);
    if (e.type == EntityType::Carrier) {
      float d = Vector3Distance(epos, playerPos);
      if (d < carrierDist) carrierDist = d;
    }
    Vector3 d = Vector3Subtract(epos, playerPos);
    float horiz = sqrtf(d.x * d.x + d.z * d.z);

    // Coverage (C.4): the player's own range, or within range of any
    // online network node. Uncovered contacts aren't shown at all.
    // Paint when a covering pulse front sweeps past the contact — the
    // player's ring, or a node's ring half a period later. Between
    // pulses the echo stays where it was painted.
    PaintState &ps = m_paint[i];
    if (ps.id != e.id) ps = PaintState{e.id};
    bool covered = horiz <= m_activeRange;
    bool paintNow = covered && pulseCrossed(horiz / m_activeRange, t0, gameTime);
    for (int s = 0; s < m_stationCount; ++s) {
      const Vector3 &n = m_stations[static_cast<size_t>(s)];
      float nx = epos.x - n.x, nz = epos.z - n.z;
      float dn2 = nx * nx + nz * nz;
      if (dn2 > nodeR2) continue;
      covered = true;
      if (!paintNow &&
          pulseCrossedAt(Config::RADAR_STATION_PULSE_OFFSET + sqrtf(dn2) / ringSpeed,
                         t0, gameTime))
        paintNow = true;
    }
    if (!covered) {
      ps.painted = false;
      continue;
    }
    if (paintNow) {
      ps.painted = true;
      ps.pos = epos;
      ps.vel = e.vel;
      ps.time = gameTime;
    }
    if (!ps.painted) continue;

    BlipType bt = blipTypeFromEntity(e.type);
    Color col = baseColourForBlip(bt);
    // Allied ships (infected, or base defenders) keep their shape but
    // read as friendly.
    if (e.aiState == AIState::Infected || e.aiState == AIState::Infecting)
      col = COL_FRIENDLY;

    RadarContact c;
    c.position = nearestCopy(ps.pos, playerPos);
    c.velocity = ps.vel;
    c.blipType = bt;
    c.distance = horiz;
    c.altDelta = c.position.y - playerPos.y;
    c.paintAge = gameTime - ps.time;
    c.colour = col;
    c.id = e.id;
    m_contacts.push_back(c);
  }
  // Jam strength — 1.0 at zero distance, linearly fades to 0 at
  // RADAR_JAM_RANGE. Clamped.
  if (carrierDist < Config::RADAR_JAM_RANGE) {
    m_jamFactor = 1.0f - (carrierDist / Config::RADAR_JAM_RANGE);
    if (m_jamFactor < 0.0f) m_jamFactor = 0.0f;
    if (m_jamFactor > 1.0f) m_jamFactor = 1.0f;
  } else {
    m_jamFactor = 0.0f;
  }

  // Tier 3 ghost-blip persistence — any contact id that was present
  // last tick but is missing this tick (entity died / slot recycled)
  // gets a ghost at its last known position. Decay handled inside
  // updateGhosts().
  for (const PrevContact &p : m_prevContacts) {
    bool stillThere = false;
    for (const RadarContact &c : m_contacts) {
      if (c.id == p.id) {
        stillThere = true;
        break;
      }
    }
    if (stillThere) continue;
    // Find a free slot in the ghost pool, or recycle the oldest.
    int slot = -1;
    float oldest = 1e9f;
    for (size_t i = 0; i < m_ghosts.size(); ++i) {
      if (!m_ghosts[i].active) {
        slot = static_cast<int>(i);
        break;
      }
      if (m_ghosts[i].lifetime < oldest) {
        oldest = m_ghosts[i].lifetime;
        slot = static_cast<int>(i);
      }
    }
    if (slot < 0) continue;
    m_ghosts[slot].active = true;
    m_ghosts[slot].lastPos = p.pos;
    m_ghosts[slot].type = p.type;
    m_ghosts[slot].lifetime = Config::RADAR_GHOST_LIFETIME;
  }
  // Tick existing ghosts; expire when lifetime hits 0.
  updateGhosts(dt);

  // Snapshot current contacts for next tick's ghost-detection pass.
  m_prevContacts.clear();
  m_prevContacts.reserve(m_contacts.size());
  for (const RadarContact &c : m_contacts) {
    m_prevContacts.push_back({c.id, c.position, c.blipType});
  }

  // Tier 2 missile-warning ring — scan the projectile pool for
  // enemy-owned shots that are closing on the player. For each one,
  // compute closest-approach time (TTI) using the relative velocity
  // and gate by RADAR_MISSILE_WARN_MIN + RADAR_MISSILE_WARN_TTI.
  m_threats.clear();
  for (const Entity &p : entities.projectiles()) {
    if (!p.alive) continue;
    if (p.owner != ProjectileOwner::Enemy) continue;
    const Vector3 ppos = nearestCopy(p.pos, playerPos);
    Vector3 rel = Vector3Subtract(playerPos, ppos);
    float dist = Vector3Length(rel);
    if (dist > Config::RADAR_MISSILE_WARN_MIN) continue;
    // Closing rate = -d/dt(distance) = dot(rel, p.vel) / |rel|.
    // Positive means the projectile is heading toward the player.
    float relSpeed = Vector3DotProduct(rel, p.vel);
    if (relSpeed <= 0.0f) continue;
    relSpeed /= (dist > 0.001f ? dist : 1.0f);
    float tti = dist / (relSpeed > 0.1f ? relSpeed : 0.1f);
    if (tti > Config::RADAR_MISSILE_WARN_TTI) continue;

    // Bearing in ship-local space. Same world→local rotation as
    // worldToRadar uses.
    float dx = ppos.x - playerPos.x;
    float dz = ppos.z - playerPos.z;
    float c = cosf(playerYaw);
    float s = sinf(playerYaw);
    float lx = dx * c - dz * s;
    float lz = dx * s + dz * c;
    // atan2(x, z): 0 = nose (+Z), π/2 = starboard (+X).
    IncomingThreat t;
    t.bearing = atan2f(lx, lz);
    t.tti = tti;
    m_threats.push_back(t);
    if (m_threats.size() >= 8) break; // cap rim clutter
  }
}

void Radar::updateGhosts(float dt) {
  for (GhostBlip &g : m_ghosts) {
    if (!g.active) continue;
    g.lifetime -= dt;
    if (g.lifetime <= 0.0f) {
      g.active = false;
      g.lifetime = 0.0f;
    }
  }
}

// Per-blip jam jitter. Seed combines the entity id with the current
// game time so the noise is stable per-blip but animated globally.
// Magnitude scales with m_jamFactor (0..1).
Vector2 Radar::applyJam(Vector2 pos, uint32_t seed) const {
  if (m_jamFactor <= 0.001f) return pos;
  // xorshift32 per-blip. Mix in gameTime so the offset animates.
  uint32_t s = seed * 2654435761u +
               static_cast<uint32_t>(m_gameTime * 17.0f);
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  float fx = (static_cast<float>(s & 0xFFFF) / 65535.0f) - 0.5f;
  s = s * 1103515245u + 12345u;
  float fy = (static_cast<float>(s & 0xFFFF) / 65535.0f) - 0.5f;
  float mag = Config::RADAR_JAM_MAX_OFFSET * m_jamFactor;
  return {pos.x + fx * 2.0f * mag, pos.y + fy * 2.0f * mag};
}

void Radar::drawVelocityArrow(Vector2 radarPos, Vector3 velocity,
                              float playerYaw, Color col) const {
  // Rotate velocity into ship-local space — same convention as
  // worldToRadar so the arrow lines up with the blip's motion on
  // the disc. screenX is negated to match the left/right convention
  // used by worldToRadar (see comment there).
  float c = cosf(playerYaw);
  float s = sinf(playerYaw);
  float vx = velocity.x * c - velocity.z * s;
  float vz = velocity.x * s + velocity.z * c;
  float spd = sqrtf(vx * vx + vz * vz);
  if (spd < 1.0f) return; // stationary contacts get no arrow

  // Length proportional to speed, capped at RADAR_VECTOR_MAX_LEN.
  // Reference speed: NEWTON_MAX_SPEED (player top speed). Anything
  // faster reads as "max length".
  float t = spd / Config::NEWTON_MAX_SPEED;
  if (t > 1.0f) t = 1.0f;
  float len = Config::RADAR_VECTOR_MAX_LEN * t;
  // Local +Z (forward) maps to screen-up (-Y); +X to screen-left
  // (matching worldToRadar's screenX negation).
  Vector2 tip = {radarPos.x - (vx / spd) * len,
                 radarPos.y - (vz / spd) * len};
  Color faded = {col.r, col.g, col.b,
                 static_cast<unsigned char>(col.a * 0.7f)};
  DrawLineEx(radarPos, tip, 1.0f, faded);
}

void Radar::drawMissileWarnings(Vector2 centre, float radius) const {
  if (m_threats.empty()) return;
  // Fast blink (0.1s period) — alarm cadence regardless of distance.
  bool on = fmodf(m_gameTime, 0.20f) < 0.12f;
  if (!on) return;
  for (const IncomingThreat &t : m_threats) {
    // Bearing 0 = nose (top of disc, screen-up). Convert to screen
    // angle. screen x is negated to match the left/right convention
    // worldToRadar uses (see comment there).
    float bx = sinf(t.bearing);
    float bz = cosf(t.bearing);
    Vector2 dirS = {-bx, -bz};
    // Place an arrow on the rim pointing inward (toward the player)
    // — that's the "incoming" reading; the arrow LOOKS in toward
    // centre, pointing where the threat is coming from.
    float rimR = radius * 1.05f;
    Vector2 base = {centre.x + dirS.x * rimR,
                    centre.y + dirS.y * rimR};
    // Build a small triangle pointing inward (toward centre).
    Vector2 inwardDir = {-dirS.x, -dirS.y};
    Vector2 perp = {-inwardDir.y, inwardDir.x};
    float L = 8.0f, W = 5.0f;
    Vector2 tip = {base.x + inwardDir.x * L,
                   base.y + inwardDir.y * L};
    Vector2 a = {base.x + perp.x * W, base.y + perp.y * W};
    Vector2 b = {base.x - perp.x * W, base.y - perp.y * W};
    DrawTriangle(tip, a, b, {255, 60, 60, 240});
  }
}

// ====================================================================
// World → radar coordinates. Rotates by -playerYaw so the player's
// nose direction is "up" on the disc. Caller is expected to range-
// filter before drawing — the transform doesn't clamp.
// ====================================================================
Vector2 Radar::worldToRadar(Vector3 worldPos, Vector2 discCentre,
                            float discRadius) const {
  float dx = worldPos.x - m_playerPos.x;
  float dz = worldPos.z - m_playerPos.z;
  // World → ship-local. Player yaw=0 faces +Z; yaw increases
  // clockwise (right turn). Using +yaw rotates the inverse correctly
  // for front/back (a target ahead lands at the top of the disc).
  // The screenX offset is then NEGATED to put the player's starboard
  // (local +X) on the right of the disc — empirical fix; the
  // "clean" inverse-rotation produced a left/right mirror on screen
  // that read wrong against the live camera view.
  float c = cosf(m_playerYaw);
  float s = sinf(m_playerYaw);
  float rx = dx * c - dz * s;
  float rz = dx * s + dz * c;
  float scale = discRadius / m_activeRange;
  return {discCentre.x - rx * scale,
          // +Z (forward in local frame) maps to "up" = -screenY.
          discCentre.y - rz * scale};
}

// ====================================================================
// Disc — background + range rings + border.
// ====================================================================
void Radar::drawDisc(Vector2 centre, float radius) const {
  // Translucent background fill for contrast.
  DrawCircleV(centre, radius, {0, 15, 5, 180});

  // Inner range ring — cannon range marker. Cannon range is 200 world
  // units (Config::CANNON_RANGE), but the spec calls for visual
  // consistency at 35% of disc — close enough for the player's
  // mental model regardless of exact range value.
  DrawCircleLinesV(centre, radius * Config::RADAR_INNER_RING_FRAC,
                   {60, 120, 60, 100});
  DrawCircleLinesV(centre, radius * Config::RADAR_OUTER_RING_FRAC,
                   {60, 120, 60, 70});

  drawPulse(centre, radius);

  Color borderCol = m_boosted ? Color{80, 220, 100, 255}
                              : Color{40, 120, 55, 200};
  DrawCircleLinesV(centre, radius, borderCol);

  // Player marker — small white cross at centre.
  DrawLine(static_cast<int>(centre.x) - 4, static_cast<int>(centre.y),
           static_cast<int>(centre.x) + 4, static_cast<int>(centre.y),
           COL_PLAYER);
  DrawLine(static_cast<int>(centre.x), static_cast<int>(centre.y) - 4,
           static_cast<int>(centre.x), static_cast<int>(centre.y) + 4,
           COL_PLAYER);
}

// ====================================================================
// Pulse ring — expands centre → rim over RADAR_PULSE_SWEEP_TIME, then
// the screen rests until the next pulse. A soft trailing band behind
// the bright front gives the classic phosphor-wave look.
// ====================================================================
void Radar::drawPulse(Vector2 centre, float radius) const {
  float phase = fmodf(m_gameTime, Config::RADAR_PULSE_PERIOD);
  if (phase >= Config::RADAR_PULSE_SWEEP_TIME) return;
  float t = phase / Config::RADAR_PULSE_SWEEP_TIME;
  float r = t * radius;
  unsigned char a = clampU(230.0f * (1.0f - 0.6f * t));
  DrawRing(centre, std::max(r - 10.0f, 0.0f), r, 0.0f, 360.0f, 64,
           {80, 220, 100, static_cast<unsigned char>(a / 5)});
  DrawRing(centre, std::max(r - 2.0f, 0.0f), r, 0.0f, 360.0f, 64,
           {140, 255, 160, a});
}

// ====================================================================
// Radar-tower pulses — each tower's ring expands from its position on the
// disc at the same speed as the player's ring, starting
// RADAR_STATION_PULSE_OFFSET after it. Drawn at true scale and clipped to
// the disc, so a tower's pulse first shows once the player is within
// RADAR_STATION_RANGE + the player's range of it. The ring fades out over
// its last RADAR_STATION_PULSE_FADE metres.
// ====================================================================
void Radar::drawStationPulses(Vector2 centre, float radius) const {
  if (m_stationCount == 0) return;
  const float period = Config::RADAR_PULSE_PERIOD;
  const float range = Config::RADAR_STATION_RANGE;
  // Same ground speed as the player's ring (range per sweep time).
  const float speed = Config::RADAR_BASE_RANGE / Config::RADAR_PULSE_SWEEP_TIME;
  float phase = fmodf(m_gameTime - Config::RADAR_STATION_PULSE_OFFSET, period);
  if (phase < 0.0f) phase += period;
  const float rWorld = phase * speed;
  if (rWorld <= 0.0f || rWorld >= range) return;
  float fade = (range - rWorld) / Config::RADAR_STATION_PULSE_FADE;
  if (fade > 1.0f) fade = 1.0f;
  const float scale = radius / m_activeRange;
  const float rPx = rWorld * scale;
  const unsigned char a = clampU(200.0f * fade);
  const Color front = {110, 235, 205, a};
  const Color trail = {70, 200, 170, static_cast<unsigned char>(a / 4)};
  const float r2 = radius * radius;
  constexpr int kSegs = 72;
  for (int s = 0; s < m_stationCount; ++s) {
    Vector2 c = worldToRadar(m_stations[static_cast<size_t>(s)], centre, radius);
    // Skip towers whose ring can't reach the disc yet.
    float dx = c.x - centre.x, dy = c.y - centre.y;
    float dc = sqrtf(dx * dx + dy * dy);
    if (dc - rPx > radius) continue;
    // Polyline ring; segments with an end outside the disc are dropped,
    // which clips the ring to the disc.
    for (int pass = 0; pass < 2; ++pass) {
      float rr = pass == 0 ? rPx : fmaxf(rPx - 5.0f, 0.0f);
      float thick = pass == 0 ? 2.0f : 4.0f;
      Color col = pass == 0 ? front : trail;
      Vector2 prev{};
      bool prevIn = false;
      for (int k = 0; k <= kSegs; ++k) {
        float ang = 6.28318f * static_cast<float>(k) / kSegs;
        Vector2 p = {c.x + cosf(ang) * rr, c.y + sinf(ang) * rr};
        float px = p.x - centre.x, py = p.y - centre.y;
        bool in = px * px + py * py <= r2;
        if (k > 0 && in && prevIn) DrawLineEx(prev, p, thick, col);
        prev = p;
        prevIn = in;
      }
    }
  }
}

// ====================================================================
// Altitude strip — vertical bar showing contacts' Y delta from player.
// Player is at the midline; strip half-height maps to RADAR_ALT_STRIP_RANGE.
// ====================================================================
void Radar::drawAltitudeStrip(Vector2 stripTopLeft, float height,
                              float width) const {
  // Background + border
  DrawRectangle(static_cast<int>(stripTopLeft.x),
                static_cast<int>(stripTopLeft.y), static_cast<int>(width),
                static_cast<int>(height), {0, 15, 5, 160});
  DrawRectangleLines(static_cast<int>(stripTopLeft.x),
                     static_cast<int>(stripTopLeft.y),
                     static_cast<int>(width), static_cast<int>(height),
                     {40, 120, 55, 200});

  // Player midline
  float midY = stripTopLeft.y + height * 0.5f;
  DrawLine(static_cast<int>(stripTopLeft.x), static_cast<int>(midY),
           static_cast<int>(stripTopLeft.x + width), static_cast<int>(midY),
           {200, 200, 200, 200});

  // Contact dots — only those within RADAR_ALT_STRIP_RANGE are placed
  // proportionally; further-out contacts clamp to top/bottom edges so
  // the player still knows "something high above me" without losing
  // the dot off-strip.
  for (const auto &c : m_contacts) {
    float altT = c.altDelta / Config::RADAR_ALT_STRIP_RANGE;
    if (altT > 1.0f) altT = 1.0f;
    if (altT < -1.0f) altT = -1.0f;
    float dotY = midY - altT * (height * 0.45f);
    float dotX = stripTopLeft.x + width * 0.5f;
    Color tinted = altitudeTint(c.colour, c.altDelta);
    tinted.a = clampU(tinted.a * paintFade(c.paintAge));
    DrawCircleV({dotX, dotY}, 2.5f, tinted);
  }
}

void Radar::drawAltitudeStripOnly(Vector2 stripTopLeft, float height,
                                  float width) const {
  drawAltitudeStrip(stripTopLeft, height, width);
}

// ====================================================================
// Per-blip dispatch — shape selected by enemy type, colour from IFF +
// altitude tint + pulse fade.
// ====================================================================
void Radar::drawBlip(Vector2 radarPos, BlipType type, Color col,
                     Vector3 velocity, float playerYaw) const {
  switch (type) {
  case BlipType::Drone:    DrawCircleV(radarPos, 2.0f, col); break;
  case BlipType::Seeder:   DrawCircleV(radarPos, 3.0f, col); break;
  case BlipType::Fighter: {
    // Triangle pointing in horizontal travel direction. (Tier 3
    // velocity arrow already drawn separately in draw() — this is
    // just the shape pointing along its own velocity vector.)
    float cy = cosf(playerYaw);
    float sy = sinf(playerYaw);
    float vx = velocity.x * cy - velocity.z * sy;
    float vz = velocity.x * sy + velocity.z * cy;
    float spd = sqrtf(vx * vx + vz * vz);
    float ang = (spd > 0.5f) ? atan2f(vx, vz) : 0.0f;
    drawTriangleBlip(radarPos, col, 5.0f, ang);
    break;
  }
  case BlipType::Bomber:
    DrawRectangle(static_cast<int>(radarPos.x) - 2,
                  static_cast<int>(radarPos.y) - 2, 5, 5, col);
    break;
  case BlipType::Carrier:  drawDiamondBlip(radarPos, col, 7.0f); break;
  case BlipType::Turret:
    DrawLine(static_cast<int>(radarPos.x) - 3, static_cast<int>(radarPos.y),
             static_cast<int>(radarPos.x) + 3, static_cast<int>(radarPos.y),
             col);
    DrawLine(static_cast<int>(radarPos.x), static_cast<int>(radarPos.y) - 3,
             static_cast<int>(radarPos.x), static_cast<int>(radarPos.y) + 3,
             col);
    break;
  case BlipType::Friendly: DrawCircleV(radarPos, 4.0f, col); break;
  case BlipType::RadarStation:
    // Radome on a mast: hollow dome above a short stem, so radar
    // towers read differently from the solid friendly dots.
    DrawRing({radarPos.x, radarPos.y - 2.5f}, 2.6f, 4.2f, 0.0f, 360.0f, 16, col);
    DrawLineEx({radarPos.x, radarPos.y + 1.5f}, {radarPos.x, radarPos.y + 6.0f},
               1.6f, col);
    break;
  case BlipType::Pickup:   drawStarBlip(radarPos, col, 4.0f); break;
  case BlipType::Base:
  case BlipType::LaunchPad:
    DrawRectangle(static_cast<int>(radarPos.x) - 3,
                  static_cast<int>(radarPos.y) - 3, 6, 6, col);
    break;
  default: break;
  }
}

// ====================================================================
// Top-level draw: disc + range-filtered blips + altitude strip.
// ====================================================================
void Radar::draw(Vector2 discCentre, float discRadius) const {
  drawDisc(discCentre, discRadius);
  drawStationPulses(discCentre, discRadius);

  // Ghost blips first — drawn underneath active blips so a fresh
  // contact at the same position overdraws cleanly.
  for (const GhostBlip &g : m_ghosts) {
    if (!g.active) continue;
    Vector2 rp = worldToRadar(nearestCopy(g.lastPos, m_playerPos), discCentre,
                              discRadius);
    Vector2 toGhost = {rp.x - discCentre.x, rp.y - discCentre.y};
    float bDist = sqrtf(toGhost.x * toGhost.x + toGhost.y * toGhost.y);
    if (bDist > discRadius) continue; // out of range — drop ghost from view
    float alphaF = g.lifetime / Config::RADAR_GHOST_LIFETIME;
    if (alphaF < 0.0f) alphaF = 0.0f;
    if (alphaF > 1.0f) alphaF = 1.0f;
    unsigned char a = static_cast<unsigned char>(120 * alphaF);
    Color ghost = {180, 60, 60, a};
    // Subtle X — distinct from a live blip shape.
    DrawLineEx({rp.x - 3.0f, rp.y - 3.0f}, {rp.x + 3.0f, rp.y + 3.0f},
               1.0f, ghost);
    DrawLineEx({rp.x - 3.0f, rp.y + 3.0f}, {rp.x + 3.0f, rp.y - 3.0f},
               1.0f, ghost);
  }

  // Active blips — clamp out-of-range contacts to the disc rim so the
  // player still sees a directional indicator even for distant threats.
  // Jam jitter applied per-contact for Tier 2 Carrier-proximity effect.
  for (const auto &c : m_contacts) {
    Vector2 rp = worldToRadar(c.position, discCentre, discRadius);
    Vector2 toBlip = {rp.x - discCentre.x, rp.y - discCentre.y};
    float bDist = sqrtf(toBlip.x * toBlip.x + toBlip.y * toBlip.y);
    if (bDist > discRadius) {
      float k = discRadius / bDist;
      rp = {discCentre.x + toBlip.x * k, discCentre.y + toBlip.y * k};
    }
    // Apply jam jitter — no-op when no live Carrier is near.
    rp = applyJam(rp, c.id);

    Color tinted = altitudeTint(c.colour, c.altDelta);
    tinted.a = clampU(tinted.a * paintFade(c.paintAge));
    // Velocity arrow first so the blip shape sits on top of its tail.
    drawVelocityArrow(rp, c.velocity, m_playerYaw, tinted);
    drawBlip(rp, c.blipType, tinted, c.velocity, m_playerYaw);
  }

  // Incoming-missile warning arrows on the rim.
  drawMissileWarnings(discCentre, discRadius);

  // Altitude strip immediately right of the disc.
  const float stripW = 16.0f;
  const float stripH = discRadius * 2.0f;
  Vector2 stripTL = {discCentre.x + discRadius + 8.0f,
                     discCentre.y - discRadius};
  drawAltitudeStrip(stripTL, stripH, stripW);
}

// ====================================================================
// Altitude-relative tint — brighten above, dim below. Caps at ±40%.
// ====================================================================
Color Radar::altitudeTint(Color base, float altDelta) {
  float altT = altDelta / Config::RADAR_ALT_STRIP_RANGE;
  if (altT > 1.0f) altT = 1.0f;
  if (altT < -1.0f) altT = -1.0f;
  float bright = 1.0f + altT * 0.4f;
  return {clampU(base.r * bright), clampU(base.g * bright),
          clampU(base.b * bright), base.a};
}

// ====================================================================
// Pulse timing. The front reaches radius fraction f at times
// k * PERIOD + f * SWEEP_TIME; find the latest such time <= t1 and
// check it falls inside the window.
// ====================================================================
bool Radar::pulseCrossed(float f, float t0, float t1) {
  return pulseCrossedAt(f * Config::RADAR_PULSE_SWEEP_TIME, t0, t1);
}

bool Radar::pulseCrossedAt(float offset, float t0, float t1) {
  float k = floorf((t1 - offset) / Config::RADAR_PULSE_PERIOD);
  float hit = k * Config::RADAR_PULSE_PERIOD + offset;
  return hit > t0;
}

// Echo brightness — full when painted, fading linearly to
// RADAR_PAINT_MIN_ALPHA by the time the next pulse repaints it.
float Radar::paintFade(float age) {
  float t = age / Config::RADAR_PULSE_PERIOD;
  if (t > 1.0f) t = 1.0f;
  return 1.0f - t * (1.0f - Config::RADAR_PAINT_MIN_ALPHA);
}

// ====================================================================
// Primitive shape helpers — small filled triangles, diamonds, stars.
// ====================================================================
void Radar::drawTriangleBlip(Vector2 pos, Color col, float size,
                             float facingRad) {
  // Tip points along facingRad in radar space (where +Y is screen-up).
  float c = cosf(facingRad);
  float s = sinf(facingRad);
  // Local-space triangle: tip up, base bottom-left + bottom-right.
  Vector2 lt = {0.0f, -size};
  Vector2 lb = {-size * 0.7f, size * 0.6f};
  Vector2 rb = {size * 0.7f, size * 0.6f};
  // Rotate by facingRad (sin/cos around screen origin), then translate.
  auto rot = [&](Vector2 v) -> Vector2 {
    return {v.x * c - v.y * s + pos.x, v.x * s + v.y * c + pos.y};
  };
  DrawTriangle(rot(lt), rot(lb), rot(rb), col);
}

void Radar::drawDiamondBlip(Vector2 pos, Color col, float size) {
  Vector2 t = {pos.x, pos.y - size};
  Vector2 r = {pos.x + size, pos.y};
  Vector2 b = {pos.x, pos.y + size};
  Vector2 l = {pos.x - size, pos.y};
  // Two triangles forming the diamond
  DrawTriangle(t, r, b, col);
  DrawTriangle(t, b, l, col);
}

void Radar::drawStarBlip(Vector2 pos, Color col, float size) {
  // Simple 4-point star — two crossed lines.
  DrawLineEx({pos.x - size, pos.y}, {pos.x + size, pos.y}, 1.5f, col);
  DrawLineEx({pos.x, pos.y - size}, {pos.x, pos.y + size}, 1.5f, col);
  DrawLineEx({pos.x - size * 0.7f, pos.y - size * 0.7f},
             {pos.x + size * 0.7f, pos.y + size * 0.7f}, 1.0f, col);
  DrawLineEx({pos.x - size * 0.7f, pos.y + size * 0.7f},
             {pos.x + size * 0.7f, pos.y - size * 0.7f}, 1.0f, col);
}
