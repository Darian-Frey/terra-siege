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
  default:                       return BlipType::Fighter;
  }
}

Color baseColourForBlip(BlipType t) {
  switch (t) {
  case BlipType::Friendly:  return COL_FRIENDLY;
  case BlipType::Pickup:    return COL_PICKUP;
  case BlipType::Base:
  case BlipType::LaunchPad: return COL_BASE;
  case BlipType::Ghost:     return {180, 60, 60, 120};
  default:                  return COL_ENEMY_CRAFT;
  }
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
  (void)dt;
  m_playerPos = playerPos;
  m_playerYaw = playerYaw;
  m_gameTime = gameTime;
  // 5g: a live RadarBooster friendly bumps the disc range to
  // RADAR_BOOST_RANGE. Border tint also flips green via boosted().
  m_boosted = entities.anyRadarBoosterAlive();
  m_activeRange = m_boosted ? Config::RADAR_BOOST_RANGE
                            : Config::RADAR_BASE_RANGE;

  m_contacts.clear();
  for (const Entity &e : entities.entities()) {
    if (!e.alive) continue;
    if (e.type == EntityType::Projectile) continue; // Tier 2 missile-only

    Vector3 d = Vector3Subtract(e.pos, playerPos);
    float horiz = sqrtf(d.x * d.x + d.z * d.z);

    BlipType bt = blipTypeFromEntity(e.type);
    Color col = baseColourForBlip(bt);

    RadarContact c;
    c.position = e.pos;
    c.velocity = e.vel;
    c.blipType = bt;
    c.distance = horiz;
    c.altDelta = e.pos.y - playerPos.y;
    c.colour = col;
    c.id = e.id;
    m_contacts.push_back(c);
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
  // clockwise (right turn). The inverse-rotation by +yaw gives a
  // local frame where +Z is the ship's nose, +X is its starboard.
  //   local.x = world.x * cos(yaw) - world.z * sin(yaw)
  //   local.z = world.x * sin(yaw) + world.z * cos(yaw)
  // Using -yaw here is wrong: it applies the forward rotation
  // instead of the inverse, so a target directly in front of an
  // east-facing player would end up at the bottom of the disc.
  float c = cosf(m_playerYaw);
  float s = sinf(m_playerYaw);
  float rx = dx * c - dz * s;
  float rz = dx * s + dz * c;
  float scale = discRadius / m_activeRange;
  return {discCentre.x + rx * scale,
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
    DrawCircleV({dotX, dotY}, 2.5f, tinted);
  }
}

void Radar::drawAltitudeStripOnly(Vector2 stripTopLeft, float height,
                                  float width) const {
  drawAltitudeStrip(stripTopLeft, height, width);
}

// ====================================================================
// Per-blip dispatch — shape selected by enemy type, colour from IFF +
// altitude tint, visibility gated by proximity-pulse blink.
// ====================================================================
void Radar::drawBlip(Vector2 radarPos, float distance, BlipType type,
                     Color col, Vector3 velocity, float playerYaw) const {
  if (!blipVisibleNow(distance, m_gameTime)) return;

  switch (type) {
  case BlipType::Drone:    DrawCircleV(radarPos, 2.0f, col); break;
  case BlipType::Seeder:   DrawCircleV(radarPos, 3.0f, col); break;
  case BlipType::Fighter: {
    // Triangle pointing in horizontal travel direction (rotated to
    // radar space the same way contact position is — see
    // worldToRadar; using +yaw inverts world→local).
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

  // Blips — clamp out-of-range contacts to the disc rim so the player
  // still sees a directional indicator even for distant threats.
  for (const auto &c : m_contacts) {
    Vector2 rp = worldToRadar(c.position, discCentre, discRadius);
    Vector2 toBlip = {rp.x - discCentre.x, rp.y - discCentre.y};
    float bDist = sqrtf(toBlip.x * toBlip.x + toBlip.y * toBlip.y);
    if (bDist > discRadius) {
      // Clip to rim
      float k = discRadius / bDist;
      rp = {discCentre.x + toBlip.x * k, discCentre.y + toBlip.y * k};
    }

    Color tinted = altitudeTint(c.colour, c.altDelta);
    drawBlip(rp, c.distance, c.blipType, tinted, c.velocity, m_playerYaw);
  }

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
// Pulse-blink — period interpolated by distance. Closer blips blink
// faster (alarming); distant blips pulse slowly. Visible for 60% of
// each cycle.
// ====================================================================
bool Radar::blipVisibleNow(float distance, float gameTime) {
  float t = (distance - Config::RADAR_BLINK_NEAR) /
            (Config::RADAR_BLINK_FAR - Config::RADAR_BLINK_NEAR);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float period = Config::RADAR_BLINK_FAST +
                 t * (Config::RADAR_BLINK_SLOW - Config::RADAR_BLINK_FAST);
  return fmodf(gameTime, period) < period * 0.6f;
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
