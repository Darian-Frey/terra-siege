# terra-siege — Radar System Design

## Overview

The radar is the player's primary situational awareness tool. It must communicate
threat positions, altitudes, movement vectors, types, and urgency simultaneously
without requiring the player to look away from the action for more than a glance.

The original Virus/Zarch radar was a flat 2D minimap showing enemy dots only —
no altitude, no IFF, no type distinction. terra-siege's radar is a full tactical
display built in three implementation tiers across Phases 4 and 5.

---

## Radar Disc — Physical Layout

```
╔══════════════════════════════════════════════╗
║                                   [ALT STRIP]║
║      ┌─────────────────────┐      │  ▲  ↑   ║
║      │   ·  ◆  (range ring)│      │  ■  │   ║
║      │    ──────           │      │     │   ║
║      │  ▲    ●    · ──     │      │  ●  ─   ║
║      │         ▲ ───       │      │         ║
║      │      [PLAYER]       │      │  ▼  ↓   ║
║      │   ·       ■ ──      │      │  ·      ║
║      │  (inner ring)       │      │         ║
║      └─────────────────────┘      └─────────║
║       RADAR  [BOOST INDICATOR]              ║
╚══════════════════════════════════════════════╝
```

**Disc:** Circular, 120px diameter (at 1280×720). Scales proportionally with
resolution. Positioned bottom-right corner, 12px from each edge.

**Altitude strip:** Vertical bar 16px wide, 120px tall, immediately to the right
of the disc. Shows relative altitude of every radar contact. Player is always at
the vertical midpoint. Contacts above player appear in the upper half; below in
the lower half. Strip height maps to ±Config::RADAR_ALT_STRIP_RANGE world units.

**Border:** Thin circle outline. Dim green normally. Bright green when Radar
Booster friendly is alive. Pulses briefly white on jamming hit.

**Player marker:** Always at disc centre. Small white cross (+).

---

## Config Values

```cpp
namespace Config {

// ----------------------------------------------------------------
// Radar
// ----------------------------------------------------------------
constexpr float RADAR_BASE_RANGE       = 300.0f;  // world units
constexpr float RADAR_BOOST_RANGE      = 500.0f;  // when Radar Booster alive
constexpr float RADAR_ALT_STRIP_RANGE  = 150.0f;  // ±units shown on alt strip
constexpr float RADAR_BLINK_NEAR       = 80.0f;   // distance for fast blink
constexpr float RADAR_BLINK_FAR        = 250.0f;  // distance for slow blink
constexpr float RADAR_BLINK_FAST       = 0.12f;   // seconds per cycle (near)
constexpr float RADAR_BLINK_SLOW       = 0.60f;   // seconds per cycle (far)
constexpr float RADAR_GHOST_LIFETIME   = 8.0f;    // seconds ghost blip persists
constexpr float RADAR_VECTOR_MAX_LEN   = 18.0f;   // pixels at max speed
constexpr float RADAR_JAM_MAX_OFFSET   = 12.0f;   // pixels of jitter near Carrier
constexpr float RADAR_MISSILE_WARN_MIN = 120.0f;  // distance to start warning ring
constexpr float RADAR_DISC_RADIUS_PX   = 60.0f;   // pixels (half of 120px disc)
constexpr float RADAR_INNER_RING_FRAC  = 0.35f;   // cannon range ring fraction
constexpr float RADAR_OUTER_RING_FRAC  = 0.75f;   // missile range ring fraction

} // namespace Config
```

---

## Blip Types — Shape and Colour

### Colours (IFF)

| Contact Type | Colour | Notes |
|-------------|--------|-------|
| Enemy craft | Red `{220, 50, 50, 255}` | All enemy ships |
| Enemy projectile | Orange `{255, 140, 0, 255}` | Only within RADAR_MISSILE_WARN_MIN |
| Friendly unit | Green `{60, 200, 80, 255}` | Collector, Repair, Radar Booster |
| Player | White `{240, 240, 240, 255}` | Centre cross |
| Pickup/upgrade | Yellow `{220, 200, 50, 255}` | Weapon pickups on ground |
| Base/launchpad | Light grey `{160, 170, 185, 255}` | Static terrain objects |
| Ghost (lost contact) | Hollow, faded `{180, 60, 60, 120}` | Last known position |

### Shapes by Enemy Type

Drawn as simple geometric primitives — no textures, stays legible at small size.

| Enemy | Shape | Size | Notes |
|-------|-------|------|-------|
| Swarm Drone | Filled circle | 2px radius | Smallest blip |
| Seeder | Filled circle | 3px radius | Slightly larger dot |
| Fighter | Filled triangle | 5px | Pointing in travel direction |
| Bomber | Filled square | 5×5px | Square is visually stable, easy to spot |
| Carrier | Filled diamond | 7px | Largest enemy blip |
| Ground Turret | Cross (+) | 5px | Cross = stationary, like crosshair |
| Friendly unit | Filled circle | 4px | Green, distinct from enemy dots |
| Pickup | Star (4-point) | 4px | Yellow, distinct shape |

### Altitude-relative Colour Tinting

Contacts above the player are brighter/lighter. Contacts below are darker/muted.
Applied as a multiplicative tint on top of the base IFF colour:

```cpp
// altDelta = contact.position.y - player.position.y
// range clamped to ±Config::RADAR_ALT_STRIP_RANGE
float altT = std::clamp(altDelta / Config::RADAR_ALT_STRIP_RANGE, -1.0f, 1.0f);

// altT > 0: above player → brighten (lerp toward white)
// altT < 0: below player → darken (lerp toward 0)
float brightFactor = 1.0f + altT * 0.4f;   // 0.6 to 1.4 range
Color tinted = {
    clampU(base.r * brightFactor),
    clampU(base.g * brightFactor),
    clampU(base.b * brightFactor),
    base.a
};
```

---

## Tier 1 — Phase 4 Baseline

The minimum viable radar for combat to feel fair. All of these are mandatory
before Phase 3 combat is considered complete.

### 1. Disc and Background

```cpp
void Radar::drawDisc(Vector2 centre, float radius)
{
    // Background fill — dark, semi-transparent
    DrawCircleV(centre, radius, { 0, 15, 5, 180 });

    // Inner range ring (cannon range)
    float innerR = radius * Config::RADAR_INNER_RING_FRAC;
    DrawCircleLinesV(centre, innerR, { 60, 120, 60, 100 });

    // Outer range ring (missile range)
    float outerR = radius * Config::RADAR_OUTER_RING_FRAC;
    DrawCircleLinesV(centre, outerR, { 60, 120, 60, 70 });

    // Border — colour based on boost state
    Color borderCol = m_boosted
        ? Color{ 80, 220, 100, 255 }   // bright green when boosted
        : Color{ 40, 120, 55, 200 };   // dim green normal
    DrawCircleLinesV(centre, radius, borderCol);
}
```

### 2. World-to-Radar Coordinate Mapping

The radar is ego-centric — player is always at centre, heading is always up.
World contacts are transformed into radar space using the player's yaw:

```cpp
Vector2 Radar::worldToRadar(Vector3 contactPos, Vector3 playerPos,
                             float playerYaw, float radarRange,
                             Vector2 discCentre, float discRadius)
{
    // World-space offset (ignore Y — handled by altitude strip)
    float dx = contactPos.x - playerPos.x;
    float dz = contactPos.z - playerPos.z;

    // Rotate by -playerYaw so player's forward = up on radar
    float cosY = cosf(-playerYaw);
    float sinY = sinf(-playerYaw);
    float rx   =  dx * cosY - dz * sinY;
    float rz   =  dx * sinY + dz * cosY;

    // Scale to disc pixels
    float scale = discRadius / radarRange;
    float px    = discCentre.x + rx * scale;
    // rz positive = north = up on screen = negative screen Y
    float py    = discCentre.y - rz * scale;

    return { px, py };
}
```

### 3. IFF Blips with Proximity Pulse

Blink rate is proportional to distance — closer = faster.

```cpp
void Radar::drawBlip(Vector2 radarPos, float distance, BlipType type,
                     Color col, float gameTime)
{
    // Clamp blip to disc edge if outside range
    // (contacts at edge of radar range appear at disc rim)
    // [clipping handled before calling this function]

    // Blink period interpolated by distance
    float t      = std::clamp((distance - Config::RADAR_BLINK_NEAR)
                              / (Config::RADAR_BLINK_FAR - Config::RADAR_BLINK_NEAR),
                              0.0f, 1.0f);
    float period = Config::RADAR_BLINK_FAST
                   + t * (Config::RADAR_BLINK_SLOW - Config::RADAR_BLINK_FAST);

    bool visible = fmodf(gameTime, period) < period * 0.6f;
    if (!visible) return;

    switch (type)
    {
        case BlipType::Drone:
            DrawCircleV(radarPos, 2.0f, col);
            break;
        case BlipType::Seeder:
            DrawCircleV(radarPos, 3.0f, col);
            break;
        case BlipType::Fighter:
            // Small triangle pointing in travel direction (set before call)
            drawTriangleBlip(radarPos, col, 5.0f);
            break;
        case BlipType::Bomber:
            DrawRectangle(static_cast<int>(radarPos.x) - 2,
                          static_cast<int>(radarPos.y) - 2,
                          5, 5, col);
            break;
        case BlipType::Carrier:
            drawDiamondBlip(radarPos, col, 7.0f);
            break;
        case BlipType::Turret:
            DrawLine(static_cast<int>(radarPos.x) - 3,
                     static_cast<int>(radarPos.y),
                     static_cast<int>(radarPos.x) + 3,
                     static_cast<int>(radarPos.y), col);
            DrawLine(static_cast<int>(radarPos.x),
                     static_cast<int>(radarPos.y) - 3,
                     static_cast<int>(radarPos.x),
                     static_cast<int>(radarPos.y) + 3, col);
            break;
        case BlipType::Friendly:
            DrawCircleV(radarPos, 4.0f, col);
            break;
        case BlipType::Pickup:
            drawStarBlip(radarPos, col, 4.0f);
            break;
        default: break;
    }
}
```

### 4. Altitude Strip

```cpp
void Radar::drawAltitudeStrip(Vector2 stripTopLeft, float height,
                               float width, float playerAlt,
                               const std::vector<RadarContact>& contacts)
{
    // Background
    DrawRectangle(static_cast<int>(stripTopLeft.x),
                  static_cast<int>(stripTopLeft.y),
                  static_cast<int>(width),
                  static_cast<int>(height),
                  { 0, 15, 5, 160 });

    // Border
    DrawRectangleLines(static_cast<int>(stripTopLeft.x),
                       static_cast<int>(stripTopLeft.y),
                       static_cast<int>(width),
                       static_cast<int>(height),
                       { 40, 120, 55, 200 });

    // Player marker — horizontal line at midpoint
    float midY = stripTopLeft.y + height * 0.5f;
    DrawLine(static_cast<int>(stripTopLeft.x),
             static_cast<int>(midY),
             static_cast<int>(stripTopLeft.x + width),
             static_cast<int>(midY),
             { 200, 200, 200, 200 });

    // Contact dots
    for (const auto& c : contacts)
    {
        if (c.altDelta == 0.0f && c.distance < 1.0f) continue; // skip player

        float altT = std::clamp(c.altDelta / Config::RADAR_ALT_STRIP_RANGE,
                                -1.0f, 1.0f);
        // altT = +1 → top of strip, -1 → bottom
        float dotY = midY - altT * (height * 0.45f);
        float dotX = stripTopLeft.x + width * 0.5f;

        DrawCircleV({ dotX, dotY }, 2.5f, c.colour);
    }
}
```

---

## Tier 2 — Phase 4 Complete

Additional tactical information layered on top of the baseline disc.

### 5. Threat Vector Arrows

A line extends from each blip in the direction of travel, length proportional
to speed. Maximum line length = Config::RADAR_VECTOR_MAX_LEN pixels.

```cpp
void Radar::drawThreatVector(Vector2 blipPos, Vector3 contactVel,
                              float playerYaw, float maxWorldSpeed,
                              Color col)
{
    // Transform velocity to radar space (same rotation as position)
    float cosY = cosf(-playerYaw);
    float sinY = sinf(-playerYaw);
    float vx   =  contactVel.x * cosY - contactVel.z * sinY;
    float vz   =  contactVel.x * sinY + contactVel.z * cosY;

    float spd = sqrtf(vx*vx + vz*vz);
    if (spd < 0.5f) return;  // stationary — no vector shown

    float scale = (spd / maxWorldSpeed) * Config::RADAR_VECTOR_MAX_LEN;
    float nx    = vx / spd;
    float nz    = vz / spd;

    Vector2 tip = {
        blipPos.x + nx * scale,
        blipPos.y - nz * scale   // screen Y inverted
    };

    // Draw as a faded line (alpha 60% of blip colour)
    Color vecCol = { col.r, col.g, col.b,
                     static_cast<unsigned char>(col.a * 0.6f) };
    DrawLineEx(blipPos, tip, 1.5f, vecCol);
}
```

### 6. Inbound Missile Warning Ring

When an enemy missile is locked on the player and within warning range, a ring
pulses outward from the player's centre blip. Ring size pulses between disc
inner ring radius and full disc radius. Rate increases as missile closes.

```cpp
void Radar::drawMissileWarning(Vector2 discCentre, float discRadius,
                                float missileDistance, float gameTime)
{
    if (missileDistance > Config::RADAR_MISSILE_WARN_MIN) return;

    // Pulse rate: 2 Hz at far range → 8 Hz as missile closes
    float distT   = 1.0f - missileDistance / Config::RADAR_MISSILE_WARN_MIN;
    float pulseFq = 2.0f + distT * 6.0f;
    float phase   = fmodf(gameTime * pulseFq, 1.0f);

    // Ring expands outward from inner ring to disc edge
    float innerR  = discRadius * Config::RADAR_INNER_RING_FRAC;
    float ringR   = innerR + phase * (discRadius - innerR);

    // Alpha fades at the outer edge
    unsigned char alpha = static_cast<unsigned char>(
        (1.0f - phase) * 200.0f);

    DrawCircleLinesV(discCentre, ringR, { 255, 80, 80, alpha });

    // Small warning text below disc
    DrawText("MISSILE", static_cast<int>(discCentre.x) - 22,
             static_cast<int>(discCentre.y + discRadius + 6),
             10, { 255, 80, 80, 200 });
}
```

### 7. Zone Overlay — Friendly Asset Positions

A subtle indication of where the player's key assets are relative to their
position. Drawn before blips so blips sit on top.

Friendly units (Collectors, Repair Stations, Radar Boosters) already appear as
green blips. The zone overlay adds:
- Static white squares for bases and launch pads
- A faint green tint region around the home base

```cpp
void Radar::drawZoneOverlay(Vector2 discCentre, float discRadius,
                             const std::vector<TerrainObject>& objects,
                             Vector3 playerPos, float playerYaw,
                             float radarRange)
{
    for (const auto& obj : objects)
    {
        if (obj.type != TerrainObjectType::Base &&
            obj.type != TerrainObjectType::LaunchPad) continue;

        float dx  = obj.position.x - playerPos.x;
        float dz  = obj.position.z - playerPos.z;
        float dist = sqrtf(dx*dx + dz*dz);
        if (dist > radarRange) continue;

        Vector2 rp = worldToRadar(obj.position, playerPos,
                                   playerYaw, radarRange,
                                   discCentre, discRadius);

        // Clamp to disc
        Vector2 toBlip = { rp.x - discCentre.x, rp.y - discCentre.y };
        float   bDist  = sqrtf(toBlip.x*toBlip.x + toBlip.y*toBlip.y);
        if (bDist > discRadius) continue;

        // Small white square
        DrawRectangle(static_cast<int>(rp.x) - 3,
                      static_cast<int>(rp.y) - 3,
                      6, 6,
                      { 160, 170, 185, 180 });
    }
}
```

### 8. Carrier Drone Count

When a Carrier appears on the radar its diamond blip has a small number beside
it showing how many drones it has currently spawned. Helps the player decide
whether to prioritise the Carrier or the existing drone threat.

```cpp
// After drawing the Carrier blip:
if (contact.type == BlipType::Carrier && contact.spawnedCount > 0)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", contact.spawnedCount);
    DrawText(buf,
             static_cast<int>(blipPos.x) + 6,
             static_cast<int>(blipPos.y) - 4,
             9, { 220, 50, 50, 200 });
}
```

---

## Tier 3 — Phase 5 Polish

### 9. Contact Memory — Ghost Blips

When a contact leaves radar range, its last known position is stored as a ghost.
Ghost blips are hollow (outline only), faded, and fade completely over
Config::RADAR_GHOST_LIFETIME seconds.

```cpp
struct GhostBlip {
    Vector3 lastPos;
    BlipType type;
    float   lifetime;   // counts down from RADAR_GHOST_LIFETIME
    bool    active;
};

// In Radar::update(dt):
for (auto& ghost : m_ghosts)
{
    if (!ghost.active) continue;
    ghost.lifetime -= dt;
    if (ghost.lifetime <= 0.0f) ghost.active = false;
}

// When a contact drops off radar:
void Radar::onContactLost(const RadarContact& c)
{
    // Find inactive slot or oldest ghost
    GhostBlip* slot = findGhostSlot();
    if (slot)
    {
        slot->lastPos  = c.position;
        slot->type     = c.blipType;
        slot->lifetime = Config::RADAR_GHOST_LIFETIME;
        slot->active   = true;
    }
}

// Drawing ghosts:
void Radar::drawGhosts(Vector2 discCentre, float discRadius,
                        Vector3 playerPos, float playerYaw,
                        float radarRange, float gameTime)
{
    for (const auto& ghost : m_ghosts)
    {
        if (!ghost.active) continue;

        Vector2 rp = worldToRadar(ghost.lastPos, playerPos,
                                   playerYaw, radarRange,
                                   discCentre, discRadius);

        // Clamp to disc
        Vector2 toBlip = { rp.x - discCentre.x, rp.y - discCentre.y };
        float   bDist  = sqrtf(toBlip.x*toBlip.x + toBlip.y*toBlip.y);
        if (bDist > discRadius) continue;

        float fadeT = ghost.lifetime / Config::RADAR_GHOST_LIFETIME;
        unsigned char alpha = static_cast<unsigned char>(120.0f * fadeT);
        Color ghostCol = { 180, 60, 60, alpha };

        // Hollow blip — circle outline only
        DrawCircleLinesV(rp, 4.0f, ghostCol);
    }
}
```

### 10. Radar Jamming

Carriers in later waves emit a jamming field. Within the jamming radius
(proportional to Carrier health — weakened Carrier jams less), enemy blips
jitter from their true radar position.

```cpp
// Per contact, when inside jamming range:
float jamT = 1.0f - (distToCarrier / Config::CARRIER_JAM_RANGE);
jamT       = std::max(0.0f, jamT);

// Pseudo-random jitter using contact ID + time as seed
float jitterX = sinf(contact.id * 7.3f + gameTime * 3.1f)
                * jamT * Config::RADAR_JAM_MAX_OFFSET;
float jitterZ = cosf(contact.id * 4.1f + gameTime * 2.7f)
                * jamT * Config::RADAR_JAM_MAX_OFFSET;

radarPos.x += jitterX;
radarPos.y += jitterZ;
```

```cpp
namespace Config {
    constexpr float CARRIER_JAM_RANGE   = 200.0f;  // world units
    constexpr float RADAR_JAM_MAX_OFFSET = 12.0f;  // pixels at full jam
}
```

### 11. Radar Boost Visual

When the Radar Booster friendly unit is alive, the radar range increases from
RADAR_BASE_RANGE to RADAR_BOOST_RANGE. Make this change clearly visible:

- Disc border changes from dim green to bright green
- Disc briefly expands in screen size by 15% (lerps back over 0.5s)
- Small "BOOST" label appears below the disc for 3 seconds on activation

```cpp
// In Radar::update(dt):
bool wasBooted = m_boosted;
m_boosted = m_entityManager.radarBoosterAlive();

if (m_boosted && !wasBooted)
{
    // Just activated
    m_boostLabelTimer = 3.0f;
    m_discRadiusMultiplier = 1.15f;  // start expanded
}

m_discRadiusMultiplier = std::max(1.0f,
    m_discRadiusMultiplier - dt * 0.3f);  // lerp back to 1.0 over 0.5s
m_boostLabelTimer -= dt;

float activeRange = m_boosted
    ? Config::RADAR_BOOST_RANGE
    : Config::RADAR_BASE_RANGE;
float activeRadius = Config::RADAR_DISC_RADIUS_PX * m_discRadiusMultiplier;
```

---

## Radar Class Declaration

```cpp
// src/hud/Radar.hpp
#pragma once

#include "raylib.h"
#include "core/Config.hpp"
#include "entity/Entity.hpp"
#include "world/Planet.hpp"
#include <vector>

enum class BlipType {
    Drone, Seeder, Fighter, Bomber, Carrier, Turret,
    Friendly, Pickup, Base, LaunchPad, Ghost
};

struct RadarContact {
    Vector3  position;
    Vector3  velocity;
    BlipType blipType;
    float    distance;
    float    altDelta;    // contact.y - player.y
    Color    colour;
    int      id;          // entity ID for jitter seed
    int      spawnedCount; // for Carrier drone count label
};

struct GhostBlip {
    Vector3  lastPos;
    BlipType type;
    float    lifetime;
    bool     active = false;
};

class Radar {
public:
    void init();
    void update(float dt, const EntityManager& entities,
                const std::vector<TerrainObject>& objects,
                Vector3 playerPos, float playerYaw,
                float gameTime);
    void draw(Vector2 discCentre, float discRadius) const;
    void onContactLost(const RadarContact& c);

private:
    // Draw helpers
    void drawDisc(Vector2 centre, float radius) const;
    void drawAltitudeStrip(Vector2 stripTopLeft) const;
    void drawBlip(Vector2 radarPos, float distance,
                  BlipType type, Color col, float gameTime) const;
    void drawThreatVector(Vector2 blipPos, Vector3 vel,
                          float playerYaw, Color col) const;
    void drawMissileWarning(Vector2 discCentre, float discRadius) const;
    void drawZoneOverlay(Vector2 discCentre, float discRadius) const;
    void drawGhosts(Vector2 discCentre, float discRadius) const;

    // Coordinate mapping
    Vector2 worldToRadar(Vector3 worldPos, Vector2 discCentre,
                         float discRadius) const;

    // Primitive helpers
    void drawTriangleBlip(Vector2 pos, Color col, float size) const;
    void drawDiamondBlip(Vector2 pos, Color col, float size) const;
    void drawStarBlip(Vector2 pos, Color col, float size) const;

    // Ghost slot management
    GhostBlip* findGhostSlot();

    // State
    std::vector<RadarContact> m_contacts;
    std::vector<TerrainObject*> m_zoneObjects;
    GhostBlip m_ghosts[32] = {};    // pre-allocated ghost pool

    Vector3 m_playerPos   = {};
    float   m_playerYaw   = 0.0f;
    float   m_activeRange = Config::RADAR_BASE_RANGE;
    float   m_gameTime    = 0.0f;

    bool    m_boosted              = false;
    float   m_discRadiusMultiplier = 1.0f;
    float   m_boostLabelTimer      = 0.0f;
    float   m_missileDistance      = 9999.0f; // nearest inbound missile
    bool    m_missileInbound       = false;
};
```

---

## Integration with Camera Views

### Tactical View (Camera 3)

When Tactical view is active, the whole screen is effectively a radar.
The corner radar disc should reduce to altitude strip only:

```cpp
void HUD::draw(CameraView camView) const
{
    if (camView == CameraView::Tactical)
    {
        // Full screen is overhead view — disc redundant
        // Draw altitude strip only, repositioned to top-right corner
        m_radar.drawAltitudeStripOnly({ GetScreenWidth() - 28.0f, 12.0f },
                                       120.0f, 16.0f);
    }
    else
    {
        // Normal radar disc + altitude strip
        Vector2 discCentre = {
            static_cast<float>(GetScreenWidth()) - Config::RADAR_DISC_RADIUS_PX - 28.0f,
            static_cast<float>(GetScreenHeight()) - Config::RADAR_DISC_RADIUS_PX - 28.0f
        };
        m_radar.draw(discCentre, Config::RADAR_DISC_RADIUS_PX);
    }
}
```

### Classic View (Camera 5)

Classic view benefits most from a full-featured radar since the fixed camera
leaves the largest threat blindspot. In Classic view, optionally increase the
radar disc size by 20% — the player needs more information from this view.

```cpp
float radarScale = (camView == CameraView::Classic) ? 1.20f : 1.0f;
float discRadius = Config::RADAR_DISC_RADIUS_PX * radarScale;
```

---

## Screen-Edge Missile Direction Indicator

Companion to the radar missile warning ring. When a missile is inbound, a
small arrow appears at the screen edge pointing toward the missile's position.
Tells the player which way to look (and which way to evade) without requiring
them to read the radar disc.

```cpp
void HUD::drawMissileDirectionIndicator(Vector3 missilePos,
                                         Vector3 playerPos,
                                         Camera3D camera) const
{
    if (!m_radar.missileInbound()) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // Project missile to screen
    Vector2 screenPos = GetWorldToScreen(missilePos, camera);

    // If on screen — no edge indicator needed
    if (screenPos.x >= 0 && screenPos.x <= sw &&
        screenPos.y >= 0 && screenPos.y <= sh) return;

    // Direction from screen centre to projected position
    Vector2 centre  = { sw * 0.5f, sh * 0.5f };
    Vector2 dir     = { screenPos.x - centre.x, screenPos.y - centre.y };
    float   len     = sqrtf(dir.x*dir.x + dir.y*dir.y);
    if (len < 0.01f) return;

    dir.x /= len;
    dir.y /= len;

    // Clamp to screen edge with margin
    const float margin = 30.0f;
    float tx = (dir.x > 0) ? sw - margin : margin;
    float ty = (dir.y > 0) ? sh - margin : margin;
    float t  = std::min((tx - centre.x) / dir.x,
                        (ty - centre.y) / dir.y);

    Vector2 arrowPos = { centre.x + dir.x * t, centre.y + dir.y * t };

    // Pulsing red arrow triangle
    float pulse = 0.6f + 0.4f * sinf(m_gameTime * 8.0f);
    unsigned char alpha = static_cast<unsigned char>(200 * pulse);
    drawArrowAt(arrowPos, atan2f(dir.y, dir.x), { 255, 80, 80, alpha });
}
```

---

## Implementation Checklist

### Phase 4 Baseline
- [ ] `Radar` class in `src/hud/Radar.hpp/cpp` — replacing empty stubs
- [ ] `RadarContact` and `GhostBlip` structs defined
- [ ] `worldToRadar()` coordinate transform with player yaw rotation
- [ ] `drawDisc()` with background, inner/outer range rings, border
- [ ] `drawAltitudeStrip()` with player midline and contact dots
- [ ] `drawBlip()` with IFF colours, shape by type, proximity pulse
- [ ] `drawTriangleBlip()`, `drawDiamondBlip()`, `drawStarBlip()` helpers
- [ ] Altitude-relative colour tinting
- [ ] Radar disc positioned bottom-right, altitude strip beside it
- [ ] `HUD::draw()` calls `Radar::draw()` with correct camera-dependent sizing
- [ ] All Config::RADAR_* constants added

### Phase 4 Complete
- [ ] `drawThreatVector()` — velocity arrows per contact
- [ ] `drawMissileWarning()` — pulsing ring when inbound missile detected
- [ ] `drawZoneOverlay()` — base/launchpad static icons
- [ ] Carrier drone count label beside diamond blip
- [ ] Screen-edge missile direction indicator in HUD
- [ ] `m_missileInbound` and `m_missileDistance` updated from EntityManager

### Phase 5 Polish
- [ ] `GhostBlip` pool and `onContactLost()` — ghost blips for lost contacts
- [ ] Jamming effect — jitter on blips near Carrier
- [ ] Radar boost visual — bright border, disc expand animation, BOOST label
- [ ] Tactical view — disc hidden, altitude strip only shown
- [ ] Classic view — disc scaled up 20%
- [ ] `Config::CARRIER_JAM_RANGE` constant added

