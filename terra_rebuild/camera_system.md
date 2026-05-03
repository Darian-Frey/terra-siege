# terra-siege — Camera System Design

## Overview

terra-siege provides five distinct camera views, each serving a different tactical
purpose. Views are switched instantly with number keys 1–5. The camera position
lerps to its new target over 0.3s to avoid jarring cuts, but orientation updates
on the keypress frame.

A 2-second fade-out label displays the active view name on switch so the player
always knows which mode they are in.

---

## The Five Views

| Key | Name | Camera Behaviour | Primary Purpose |
|-----|------|-----------------|----------------|
| 1 | Chase | Follows ship nose, behind and above | Combat — weapons always forward in view |
| 2 | Velocity | Follows velocity vector direction | High-speed flight, terrain avoidance |
| 3 | Tactical | Fixed overhead, ship centred, no rotation | Full battlefield situational awareness |
| 4 | Threat-lock | Rotates to keep nearest enemy in frame | Never lose the biggest threat |
| 5 | Classic | Fixed diagonal-down, world-north always up | Original Virus feel, master-level play |

---

## View 1 — Chase Camera

The default gameplay camera. Sits behind and above the ship, always following the
ship's nose direction (yaw axis only — does not follow pitch). The horizon stays
level regardless of ship attitude.

```
Ship nose direction (yaw only)
         ↑
    [Camera]
       \
        \  CAM_DISTANCE behind
         \
        [Ship] → nose direction
```

**Implementation:**
```cpp
void GameState::updateChaseCamera(float dt)
{
    Vector3 playerPos = m_player.position();
    float   playerYaw = m_player.yaw();

    // Horizontal forward only — camera never pitches with the ship
    Vector3 horizFwd = { sinf(playerYaw), 0.0f, cosf(playerYaw) };

    Vector3 desiredPos = Vector3Add(
        playerPos,
        Vector3Add(
            Vector3Scale(horizFwd, -Config::CAM_DISTANCE),
            { 0.0f, Config::CAM_HEIGHT, 0.0f }
        )
    );

    // Terrain clamp — camera never goes underground
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    desiredPos.y = std::max(desiredPos.y, camGroundH + 3.5f);
    desiredPos.y = std::max(desiredPos.y, playerPos.y + 2.0f);

    float lerpSpeed = Config::CAM_LERP * dt;
    m_camera.position = Vector3Lerp(m_camera.position, desiredPos,
                                    std::min(lerpSpeed, 1.0f));

    // Look target slightly ahead of ship
    m_camera.target = Vector3Add(playerPos,
        Vector3Add(Vector3Scale(horizFwd, 8.0f), { 0.0f, 1.5f, 0.0f }));

    m_camera.up = { 0.0f, 1.0f, 0.0f };
    m_camera.fovy = Config::CAM_FOV;
}
```

**Config values:**
```cpp
constexpr float CAM_DISTANCE  = 18.0f;
constexpr float CAM_HEIGHT    = 8.0f;
constexpr float CAM_FOV       = 75.0f;
constexpr float CAM_LERP      = 8.0f;
```

**Tactical notes:**
- Best view for active combat — weapons always fire forward in screen space
- Player can predict where shots will go by reading the screen centre
- Loses situational awareness to the sides and rear — pair with radar

---

## View 2 — Velocity Camera

Camera follows the velocity vector direction rather than the nose direction. When
the ship is flying sideways or backwards relative to its nose, this view shows the
terrain the ship is actually approaching rather than where it is thrusting.

Particularly useful during high-speed manoeuvres where built-up momentum carries
the ship in a different direction to the nose. Shows the player what they are about
to fly into, not what they are thrusting toward.

**Degenerate case:** When speed drops below threshold, velocity direction is
undefined. Camera blends smoothly back to chase cam orientation.

```cpp
void GameState::updateVelocityCamera(float dt)
{
    Vector3 playerPos = m_player.position();
    Vector3 vel       = m_player.velocity();
    float   spd       = Vector3Length(vel);

    // Fallback direction when nearly stationary
    float   playerYaw = m_player.yaw();
    Vector3 noseFwd   = { sinf(playerYaw), 0.0f, cosf(playerYaw) };

    Vector3 velDir;
    if (spd > 0.01f)
    {
        // Flatten velocity to horizontal plane for camera yaw
        Vector3 flatVel = { vel.x, 0.0f, vel.z };
        float flatSpd   = Vector3Length(flatVel);

        if (flatSpd > 0.01f)
            velDir = Vector3Scale(flatVel, 1.0f / flatSpd);
        else
            velDir = noseFwd;
    }
    else
    {
        velDir = noseFwd;
    }

    // Blend toward nose direction when nearly stationary
    // Threshold: below VELOCITY_CAM_MIN_SPD, linearly blend to nose direction
    float blendToNose = 1.0f - std::min(spd / Config::VELOCITY_CAM_MIN_SPD, 1.0f);
    velDir = Vector3Normalize(
        Vector3Add(
            Vector3Scale(velDir,   1.0f - blendToNose),
            Vector3Scale(noseFwd,  blendToNose)
        )
    );

    Vector3 desiredPos = Vector3Add(
        playerPos,
        Vector3Add(
            Vector3Scale(velDir, -Config::CAM_DISTANCE),
            { 0.0f, Config::CAM_HEIGHT, 0.0f }
        )
    );

    // Terrain clamp
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    desiredPos.y = std::max(desiredPos.y, camGroundH + 3.5f);
    desiredPos.y = std::max(desiredPos.y, playerPos.y + 2.0f);

    float lerpSpeed = Config::CAM_LERP * dt;
    m_camera.position = Vector3Lerp(m_camera.position, desiredPos,
                                    std::min(lerpSpeed, 1.0f));

    m_camera.target = Vector3Add(playerPos,
        Vector3Add(Vector3Scale(velDir, 8.0f), { 0.0f, 1.5f, 0.0f }));

    m_camera.up   = { 0.0f, 1.0f, 0.0f };
    m_camera.fovy = Config::CAM_FOV;
}
```

**Config values:**
```cpp
constexpr float VELOCITY_CAM_MIN_SPD = 5.0f;  // below this, blend to chase cam
```

**Tactical notes:**
- Essential for terrain avoidance during fast Newtonian manoeuvres
- Disorienting when the ship is nearly stationary — velocity direction flickers
- Experienced players use this when bleeding off speed over mountainous terrain
- The nose and velocity views feel identical during straight-line flight —
  difference is only apparent when building or shedding sideways momentum

---

## View 3 — Tactical Overhead

Fixed altitude overhead view. Camera looks straight down. Ship is always centred.
No rotation — world-north is always up on screen. Terrain scrolls beneath as the
ship moves.

This is the strategic awareness view — the player can see the entire local battlefield,
track all radar contacts relative to terrain features, and plan approach routes. The
trade-off is that combat is difficult from this angle since the ship's nose direction
is hard to read and the cannon fires horizontally (mostly off-screen from above).

```cpp
void GameState::updateTacticalCamera(float dt)
{
    Vector3 playerPos = m_player.position();

    Vector3 desiredPos = {
        playerPos.x,
        playerPos.y + Config::TACTICAL_CAM_ALTITUDE,
        playerPos.z
    };

    float lerpSpeed = Config::CAM_LERP * dt;
    m_camera.position = Vector3Lerp(m_camera.position, desiredPos,
                                    std::min(lerpSpeed, 1.0f));

    // Look straight down at player position
    m_camera.target = playerPos;

    // North is always up — world-Z axis points toward top of screen
    m_camera.up   = { 0.0f, 0.0f, 1.0f };  // +Z = north = up on screen
    m_camera.fovy = Config::TACTICAL_CAM_FOV;
}
```

**Config values:**
```cpp
constexpr float TACTICAL_CAM_ALTITUDE = 180.0f;  // world units above player
constexpr float TACTICAL_CAM_FOV      = 55.0f;   // narrower FOV for less distortion
```

**Tactical notes:**
- Best view for reading the radar and planning wave engagements
- Ship nose direction shown as a small arrow overlay on the ship sprite
  (the ship mesh reads as nearly flat from directly above — an indicator arrow
  is needed so the player knows which way they are pointing)
- Switching from Tactical to Chase mid-combat requires 0.3s of camera lerp to
  pull back to the correct follow position — acceptable latency
- At TACTICAL_CAM_ALTITUDE = 180 the terrain HEIGHT_MAX of 120 means mountains
  are visible at roughly 60 units above sea level — terrain features still read
  clearly from this altitude

---

## View 4 — Threat-Lock Camera

The camera sits behind the ship (like Chase) but its yaw slowly rotates to keep
the highest-priority enemy visible alongside the ship in frame. The player can see
both where they are going and where the biggest threat is simultaneously.

Priority is determined by a simple threat score:
```
threat_score = (1 / distance) * damage_multiplier
damage_multiplier: Carrier=4, Bomber=2, Fighter=1, Drone=0.3
```

**Target hysteresis:** A new enemy only becomes the locked target if its threat
score exceeds the current target's score by 20%. This prevents rapid camera
switching when two enemies have similar scores.

**Maximum rotation rate:** 90°/sec cap prevents snapping when a new target is
selected on the opposite side.

```cpp
void GameState::updateThreatLockCamera(float dt)
{
    Vector3 playerPos  = m_player.position();
    float   playerYaw  = m_player.yaw();
    Vector3 horizFwd   = { sinf(playerYaw), 0.0f, cosf(playerYaw) };

    // --- Find highest-priority threat ---
    Entity* target = findHighestThreat(playerPos);

    float desiredCamYaw = playerYaw; // default: same as chase cam

    if (target)
    {
        // Direction from player to threat in world space
        Vector3 toThreat = Vector3Subtract(target->position, playerPos);
        float   threatYaw = atan2f(toThreat.x, toThreat.z);

        // We want the camera to sit at an angle that shows BOTH the ship's
        // forward arc AND the threat. Offset camera yaw halfway between
        // player yaw and the direction toward the threat.
        float yawDiff = threatYaw - playerYaw;

        // Normalise yawDiff to [-π, π]
        while (yawDiff >  3.14159f) yawDiff -= 6.28318f;
        while (yawDiff < -3.14159f) yawDiff += 6.28318f;

        // Clamp offset — camera doesn't rotate more than 90° from nose
        yawDiff = std::clamp(yawDiff, -1.5708f, 1.5708f);
        desiredCamYaw = playerYaw + yawDiff * 0.5f;
    }

    // Apply rotation rate cap
    float maxRotation = Config::THREAT_CAM_MAX_ROT * dt;
    float yawError    = desiredCamYaw - m_threatCamYaw;
    while (yawError >  3.14159f) yawError -= 6.28318f;
    while (yawError < -3.14159f) yawError += 6.28318f;
    m_threatCamYaw += std::clamp(yawError, -maxRotation, maxRotation);

    // Build camera position from m_threatCamYaw
    Vector3 camDir = { sinf(m_threatCamYaw), 0.0f, cosf(m_threatCamYaw) };

    Vector3 desiredPos = Vector3Add(
        playerPos,
        Vector3Add(
            Vector3Scale(camDir, -Config::CAM_DISTANCE),
            { 0.0f, Config::CAM_HEIGHT, 0.0f }
        )
    );

    // Terrain clamp
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    desiredPos.y = std::max(desiredPos.y, camGroundH + 3.5f);
    desiredPos.y = std::max(desiredPos.y, playerPos.y + 2.0f);

    float lerpSpeed = Config::CAM_LERP * dt;
    m_camera.position = Vector3Lerp(m_camera.position, desiredPos,
                                    std::min(lerpSpeed, 1.0f));

    m_camera.target = Vector3Add(playerPos, { 0.0f, 1.5f, 0.0f });
    m_camera.up     = { 0.0f, 1.0f, 0.0f };
    m_camera.fovy   = Config::CAM_FOV;
}
```

**Config values:**
```cpp
constexpr float THREAT_CAM_MAX_ROT      = 1.5708f;  // rad/s — 90°/sec max rotation
constexpr float THREAT_CAM_HYSTERESIS   = 0.20f;    // 20% score advantage to switch
```

**Additional state needed in GameState:**
```cpp
float   m_threatCamYaw    = 0.0f;   // current threat-lock camera yaw
Entity* m_threatLockTarget = nullptr; // current locked target (with hysteresis)
```

**Tactical notes:**
- Most useful during multi-enemy engagements where flanking is a risk
- Camera rotation can be disorienting until the player adapts — recommend
  trying Chase first and switching to Threat-lock once comfortable
- When no enemies are present camera behaves identically to Chase
- The 90° max offset clamp means threats directly behind the player only
  pull the camera 45° to the side — the player must use radar for full
  rear coverage even in this mode

---

## View 5 — Classic Camera

Recreates the original Virus/Zarch fixed diagonal-down perspective. The camera
is at a fixed world-space position relative to the ship (not nose-relative —
world-north is always at the top of the screen). The ship moves within the view
as it flies, rather than the terrain scrolling with the camera behind the ship.

This is the most demanding view — the player must mentally map their ship's
orientation onto the fixed world frame. Mastering it gives complete battlefield
awareness since the camera never rotates and all directions are equally visible.

```cpp
void GameState::updateClassicCamera(float dt)
{
    Vector3 playerPos = m_player.position();

    // Fixed world-space offset — camera is always north-northwest of player,
    // elevated, looking south-southeast at a ~30° downward angle.
    // This matches the original Virus camera angle.
    Vector3 desiredPos = {
        playerPos.x + Config::CLASSIC_CAM_OFFSET_X,
        playerPos.y + Config::CLASSIC_CAM_ALTITUDE,
        playerPos.z + Config::CLASSIC_CAM_OFFSET_Z
    };

    // Terrain clamp — even fixed cameras can clip terrain on steep hills
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    desiredPos.y = std::max(desiredPos.y, camGroundH + 5.0f);

    // Classic view lerps more slowly — the feel is more detached and stable
    float lerpSpeed = Config::CLASSIC_CAM_LERP * dt;
    m_camera.position = Vector3Lerp(m_camera.position, desiredPos,
                                    std::min(lerpSpeed, 1.0f));

    // Always look at player position — camera pans smoothly as ship moves
    m_camera.target = playerPos;
    m_camera.up     = { 0.0f, 1.0f, 0.0f };
    m_camera.fovy   = Config::CLASSIC_CAM_FOV;
}
```

**Config values:**
```cpp
// Classic camera offset — fixed world-space position relative to player
// Tuned to recreate the original Virus diagonal-down perspective
// OFFSET_X = 0: camera is directly north of player (adjust for diagonal)
// OFFSET_Z = negative: camera is north of player (remember +Z = south in our system)
constexpr float CLASSIC_CAM_OFFSET_X = -40.0f;  // slight east-west offset
constexpr float CLASSIC_CAM_OFFSET_Z = -80.0f;  // north of player
constexpr float CLASSIC_CAM_ALTITUDE =  120.0f; // above player
constexpr float CLASSIC_CAM_FOV      =  65.0f;  // slightly narrower than chase
constexpr float CLASSIC_CAM_LERP     =  5.0f;   // slower follow = more detached feel
```

**Tuning note:** The exact values for CLASSIC_CAM_OFFSET_X/Z and CLASSIC_CAM_ALTITUDE
need playtesting against the actual world scale. The original's terrain was much smaller.
The goal is that the player ship is visible at roughly 1/3 from the bottom of the screen
with terrain receding into the distance at the top. Start with the values above and
adjust until it matches the aesthetic memory of the original.

**Tactical notes:**
- Hardest to learn — requires building a mental model of world-north orientation
- Most rewarding once mastered — full 360° terrain awareness simultaneously
- The ship's nose direction is visible from the mesh shape but subtle —
  an optional compass rose HUD element helps in this view only
- Shadows on the terrain (Phase 5 feature) become most meaningful in this view
  since they show enemy positions even when enemies are off-screen

---

## Camera View Switching

### State

```cpp
enum class CameraView {
    Chase       = 0,
    Velocity    = 1,
    Tactical    = 2,
    ThreatLock  = 3,
    Classic     = 4
};

// In GameState private members
CameraView  m_cameraView      = CameraView::Chase;
float       m_viewLabelTimer  = 0.0f;   // counts down from 2.0s after switch
```

### Input Handler

```cpp
void GameState::handleCameraViewKeys()
{
    CameraView newView = m_cameraView;

    if (IsKeyPressed(KEY_ONE))   newView = CameraView::Chase;
    if (IsKeyPressed(KEY_TWO))   newView = CameraView::Velocity;
    if (IsKeyPressed(KEY_THREE)) newView = CameraView::Tactical;
    if (IsKeyPressed(KEY_FOUR))  newView = CameraView::ThreatLock;
    if (IsKeyPressed(KEY_FIVE))  newView = CameraView::Classic;

    if (newView != m_cameraView)
    {
        m_cameraView     = newView;
        m_viewLabelTimer = 2.0f;   // show label for 2 seconds
    }
}
```

### Update Dispatcher

```cpp
void GameState::updateCamera(float dt)
{
    switch (m_cameraView)
    {
        case CameraView::Chase:      updateChaseCamera(dt);      break;
        case CameraView::Velocity:   updateVelocityCamera(dt);   break;
        case CameraView::Tactical:   updateTacticalCamera(dt);   break;
        case CameraView::ThreatLock: updateThreatLockCamera(dt); break;
        case CameraView::Classic:    updateClassicCamera(dt);    break;
    }

    m_viewLabelTimer -= dt;
}
```

### View Label HUD Element

```cpp
void GameState::drawCameraViewLabel() const
{
    if (m_viewLabelTimer <= 0.0f) return;

    static const char* labels[] = {
        "CHASE VIEW",
        "VELOCITY VIEW",
        "TACTICAL VIEW",
        "THREAT-LOCK VIEW",
        "CLASSIC VIEW"
    };

    const char* label = labels[static_cast<int>(m_cameraView)];

    // Fade alpha over last 0.5 seconds
    float alpha = std::min(m_viewLabelTimer / 0.5f, 1.0f);
    unsigned char a = static_cast<unsigned char>(220.0f * alpha);

    int sw  = GetScreenWidth();
    int tw  = MeasureText(label, 22);
    int px  = sw / 2 - tw / 2;
    int py  = 48;

    DrawRectangle(px - 12, py - 6, tw + 24, 34, { 0, 0, 0, static_cast<unsigned char>(120 * alpha) });
    DrawText(label, px, py, 22, { 200, 220, 255, a });
}
```

Call `drawCameraViewLabel()` at the end of `drawHUD()`.

---

## Tactical View — Ship Direction Indicator

From directly above, the ship mesh reads as a nearly symmetrical shape. The player
needs to know which way the nose is pointing. A simple direction arrow is drawn in
2D over the ship's screen position:

```cpp
void GameState::drawTacticalShipArrow() const
{
    if (m_cameraView != CameraView::Tactical) return;

    Vector3 shipPos    = m_player.position();
    Vector2 screenPos  = GetWorldToScreen(shipPos, m_camera);
    float   yaw        = m_player.yaw();

    // Arrow pointing in ship's nose direction (screen space)
    // In tactical view, +Z (north) is up on screen, so yaw maps directly
    float   arrowLen   = 18.0f;
    Vector2 tip = {
        screenPos.x + sinf(yaw)  * arrowLen,
        screenPos.y - cosf(yaw)  * arrowLen   // screen Y is inverted
    };

    DrawLineEx(screenPos, tip, 2.0f, { 255, 255, 100, 200 });
    // Small triangle arrowhead at tip
    DrawCircle(static_cast<int>(tip.x), static_cast<int>(tip.y),
               3.0f, { 255, 255, 100, 200 });
}
```

Call after `EndMode3D()` in `render()`.

---

## Classic View — Optional Compass Rose

In Classic view, world orientation is fixed but the ship can face any direction.
An optional small compass rose in the corner confirms which screen edge is north:

```cpp
void GameState::drawClassicCompassRose() const
{
    if (m_cameraView != CameraView::Classic) return;

    // Small compass in bottom-right corner
    const int cx = GetScreenWidth() - 45;
    const int cy = GetScreenHeight() - 90;
    const int r  = 22;

    DrawCircleLines(cx, cy, r, { 150, 160, 180, 160 });

    // N always at top (world-north = top of screen in classic view)
    DrawText("N", cx - 4, cy - r - 14, 12, { 220, 230, 255, 200 });
    DrawText("S", cx - 4, cy + r + 2,  12, { 180, 190, 210, 160 });
    DrawText("E", cx + r + 3, cy - 5,  12, { 180, 190, 210, 160 });
    DrawText("W", cx - r - 14, cy - 5, 12, { 180, 190, 210, 160 });

    // Tick mark at north
    DrawLine(cx, cy - r + 4, cx, cy - r + 10, { 255, 255, 100, 220 });
}
```

---

## Config.hpp Summary — All Camera Constants

```cpp
namespace Config {

// ----------------------------------------------------------------
// Camera — shared
// ----------------------------------------------------------------
constexpr float CAM_FOV               = 75.0f;
constexpr float CAM_LERP              = 8.0f;    // position lerp speed per second

// ----------------------------------------------------------------
// Camera — Chase (View 1) and Velocity (View 2)
// ----------------------------------------------------------------
constexpr float CAM_DISTANCE          = 18.0f;   // units behind player
constexpr float CAM_HEIGHT            = 8.0f;    // units above player
constexpr float VELOCITY_CAM_MIN_SPD  = 5.0f;   // blend to chase below this speed

// ----------------------------------------------------------------
// Camera — Tactical overhead (View 3)
// ----------------------------------------------------------------
constexpr float TACTICAL_CAM_ALTITUDE = 180.0f;
constexpr float TACTICAL_CAM_FOV      = 55.0f;

// ----------------------------------------------------------------
// Camera — Threat-lock (View 4)
// ----------------------------------------------------------------
constexpr float THREAT_CAM_MAX_ROT    = 1.5708f; // rad/s (90°/sec)
constexpr float THREAT_CAM_HYSTERESIS = 0.20f;   // 20% score gap to switch target

// ----------------------------------------------------------------
// Camera — Classic / original Virus view (View 5)
// ----------------------------------------------------------------
constexpr float CLASSIC_CAM_OFFSET_X  = -40.0f;  // world units east of player
constexpr float CLASSIC_CAM_OFFSET_Z  = -80.0f;  // world units north of player
constexpr float CLASSIC_CAM_ALTITUDE  = 120.0f;  // world units above player
constexpr float CLASSIC_CAM_FOV       = 65.0f;
constexpr float CLASSIC_CAM_LERP      = 5.0f;    // slower = more detached feel

} // namespace Config
```

---

## Implementation Checklist

- [ ] Add `CameraView` enum to `GameState.hpp`
- [ ] Add `m_cameraView`, `m_viewLabelTimer`, `m_threatCamYaw`,
      `m_threatLockTarget` to `GameState` private members
- [ ] Add `handleCameraViewKeys()` to `GameState::update()`
- [ ] Implement `updateChaseCamera()` — refactor from existing follow camera
- [ ] Implement `updateVelocityCamera()` with stationary blend fallback
- [ ] Implement `updateTacticalCamera()` with correct `camera.up` = `{0,0,1}`
- [ ] Implement `updateThreatLockCamera()` with hysteresis and rotation cap
- [ ] Implement `updateClassicCamera()` with world-fixed offset
- [ ] Add `drawCameraViewLabel()` to `drawHUD()`
- [ ] Add `drawTacticalShipArrow()` after `EndMode3D()`
- [ ] Add `drawClassicCompassRose()` to `drawHUD()`
- [ ] Add all new constants to `Config.hpp`
- [ ] Remove old dual-mode camera speed constants (Arcade remnants)
- [ ] Tune `CLASSIC_CAM_OFFSET_X/Z` and `CLASSIC_CAM_ALTITUDE` by feel
      against actual world scale

---

## Notes for Phase 3 Integration

When enemies are implemented, `updateThreatLockCamera()` requires access to the
entity pool via `EntityManager`. The threat score query:

```cpp
Entity* GameState::findHighestThreat(Vector3 playerPos)
{
    Entity* best      = nullptr;
    float   bestScore = m_threatLockTarget
                        ? (threatScore(m_threatLockTarget, playerPos)
                           * (1.0f + Config::THREAT_CAM_HYSTERESIS))
                        : 0.0f;

    for (auto& e : m_entityManager.enemies())
    {
        if (!e.alive) continue;
        float score = threatScore(&e, playerPos);
        if (score > bestScore)
        {
            bestScore = score;
            best      = &e;
        }
    }
    return best ? best : m_threatLockTarget;
}

float GameState::threatScore(const Entity* e, Vector3 playerPos)
{
    float dist = Vector3Distance(playerPos, e->position);
    if (dist < 0.01f) dist = 0.01f;

    float dmgMult = 1.0f;
    switch (e->type)
    {
        case EntityType::Carrier: dmgMult = 4.0f; break;
        case EntityType::Bomber:  dmgMult = 2.0f; break;
        case EntityType::Fighter: dmgMult = 1.0f; break;
        default:                  dmgMult = 0.3f; break;
    }
    return dmgMult / dist;
}
```

Until Phase 3 is implemented, `findHighestThreat()` returns `nullptr` and the
Threat-lock camera falls back to Chase cam behaviour.

