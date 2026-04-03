#include "GameState.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>

// ================================================================
// init
// ================================================================
void GameState::init() {
  m_planet.generate(12345u);

  // Start player above terrain centre facing north (+Z)
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);
  Vector3 startPos = {mid, ground + 12.0f, mid};

  m_player.init(startPos, Config::FLIGHT_ASSIST_DEFAULT);

  // Camera setup
  m_camera.fovy = Config::CAM_FOV;
  m_camera.projection = CAMERA_PERSPECTIVE;
  m_camera.up = {0.0f, 1.0f, 0.0f};

  // Initialise follow camera at sensible position
  Vector3 fwd = m_player.forward();
  m_camera.position =
      Vector3Add(startPos, Vector3Add(Vector3Scale(fwd, -Config::CAM_DISTANCE),
                                      {0, Config::CAM_HEIGHT, 0}));
  m_camera.target = Vector3Add(startPos, {0, 1.5f, 0});

  // Start in follow mode
  m_camMode = CamMode::Follow;

  m_state = AppState::Playing;
}

// ================================================================
// Dev keys (only compiled/active in DEV_MODE)
// ================================================================
void GameState::handleDevKeys() {
#ifdef DEV_MODE
  // F1 — toggle camera mode
  if (IsKeyPressed(KEY_F1)) {
    if (m_camMode == CamMode::Follow) {
      m_camMode = CamMode::FreeRoam;
      // Initialise free-roam from current follow camera position
      Vector3 dir =
          Vector3Normalize(Vector3Subtract(m_camera.target, m_camera.position));
      m_camYaw = atan2f(dir.x, dir.z);
      m_camPitch = asinf(dir.y);
    } else {
      m_camMode = CamMode::Follow;
    }
  }

  // F2 — next flight assist level
  if (IsKeyPressed(KEY_F2)) {
    int next = (m_player.flightAssist() + 1) % 4;
    m_player.setFlightAssist(next);
  }
#endif
}

// ================================================================
// Follow camera
// ================================================================
void GameState::updateFollowCamera(float dt) {
  Vector3 playerPos = m_player.position();
  Vector3 playerFwd = m_player.forward();
  float playerRoll = m_player.roll();

  // Desired camera position: behind and above the player
  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(playerFwd, -Config::CAM_DISTANCE),
                            {0.0f, Config::CAM_HEIGHT, 0.0f}));

  // Clamp desired position above terrain — prevents camera going underground
  // when turning toward hills or flying over ridges
  {
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    float camMinY = camGroundH + 3.5f; // stay 3.5 units above terrain
    if (desiredPos.y < camMinY)
      desiredPos.y = camMinY;
  }

  // Also enforce a minimum distance above the player so the camera
  // never drops below ship level on very steep slopes
  if (desiredPos.y < playerPos.y + 2.0f)
    desiredPos.y = playerPos.y + 2.0f;

  // Smooth follow — lerp toward desired position
  float lerpSpeed = Config::CAM_FOLLOW_SPEED * dt;
  if (lerpSpeed > 1.0f)
    lerpSpeed = 1.0f;

  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  // Look at player with slight forward offset so we see ahead
  Vector3 lookTarget = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(playerFwd, 8.0f), {0.0f, 1.5f, 0.0f}));
  m_camera.target = Vector3Lerp(m_camera.target, lookTarget, lerpSpeed * 1.5f);

  // Bank camera slightly with the ship
  float rollLerp = playerRoll * 0.3f;
  Vector3 baseUp = {0.0f, 1.0f, 0.0f};
  Vector3 rightV = m_player.right();
  m_camera.up =
      Vector3Normalize(Vector3Add(baseUp, Vector3Scale(rightV, -rollLerp)));
}

// ================================================================
// Free-roam camera (dev mode)
// ================================================================
void GameState::updateFreeCamera(float dt) {
  Vector2 delta = GetMouseDelta();
  m_camYaw += delta.x * 0.003f;
  m_camPitch -= delta.y * 0.003f;
  if (m_camPitch > 1.4f)
    m_camPitch = 1.4f;
  if (m_camPitch < -1.4f)
    m_camPitch = -1.4f;

  Vector3 fwd = {cosf(m_camPitch) * sinf(m_camYaw), sinf(m_camPitch),
                 cosf(m_camPitch) * cosf(m_camYaw)};
  Vector3 rgt = {sinf(m_camYaw - 1.5708f), 0.0f, cosf(m_camYaw - 1.5708f)};

  float speed = m_camSpeed;
  if (IsKeyDown(KEY_LEFT_SHIFT))
    speed *= 4.0f;

  if (IsKeyDown(KEY_W))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(fwd, speed * dt));
  if (IsKeyDown(KEY_S))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(fwd, -speed * dt));
  if (IsKeyDown(KEY_D))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(rgt, speed * dt));
  if (IsKeyDown(KEY_A))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(rgt, -speed * dt));
  if (IsKeyDown(KEY_E))
    m_camera.position.y += speed * dt;
  if (IsKeyDown(KEY_Q))
    m_camera.position.y -= speed * dt;

  m_camera.target = Vector3Add(m_camera.position, fwd);
  m_camera.up = {0.0f, 1.0f, 0.0f};
}

// ================================================================
// update
// ================================================================
void GameState::update(float dt) {
  switch (m_state) {
  case AppState::Playing: {
    handleDevKeys();

    if (m_camMode == CamMode::Follow) {
      m_player.update(dt, m_planet);
      updateFollowCamera(dt);
    } else {
      // Free-roam: player suspended
      updateFreeCamera(dt);
    }
    break;
  }
  default:
    break;
  }
}

// ================================================================
// render
// ================================================================
void GameState::render(float alpha) {
  (void)alpha;

  switch (m_state) {
  case AppState::Playing: {
    ClearBackground({55, 80, 140, 255});

    BeginMode3D(m_camera);
    m_planet.draw(m_camera.position);
    m_player.render();
    EndMode3D();

    drawHUD();
    break;
  }
  case AppState::MainMenu:
    ClearBackground(BLACK);
    DrawText("MAIN MENU (todo)", 40, 40, 28, RAYWHITE);
    break;
  case AppState::Paused:
    DrawText("PAUSED", 40, 40, 28, RAYWHITE);
    break;
  case AppState::GameOver:
    ClearBackground(BLACK);
    DrawText("GAME OVER", 40, 40, 28, RED);
    break;
  case AppState::Victory:
    ClearBackground(BLACK);
    DrawText("VICTORY", 40, 40, 28, GREEN);
    break;
  }
}

// ================================================================
// HUD
// ================================================================
void GameState::drawHUD() const {
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();

  DrawFPS(12, 12);

  // ---- Debug info panel (top right) ----
  const Vector3 &pos = m_player.position();
  float terrainH = m_planet.heightAt(pos.x, pos.z);
  float agl = pos.y - terrainH;
  float spd = m_player.speed();

  float yawDeg = m_player.yaw() * (180.0f / 3.14159f);
  while (yawDeg < 0.0f)
    yawDeg += 360.0f;
  while (yawDeg >= 360.0f)
    yawDeg -= 360.0f;

  const char *compass = "N";
  if (yawDeg >= 22.5f && yawDeg < 67.5f)
    compass = "NE";
  else if (yawDeg >= 67.5f && yawDeg < 112.5f)
    compass = "E";
  else if (yawDeg >= 112.5f && yawDeg < 157.5f)
    compass = "SE";
  else if (yawDeg >= 157.5f && yawDeg < 202.5f)
    compass = "S";
  else if (yawDeg >= 202.5f && yawDeg < 247.5f)
    compass = "SW";
  else if (yawDeg >= 247.5f && yawDeg < 292.5f)
    compass = "W";
  else if (yawDeg >= 292.5f && yawDeg < 337.5f)
    compass = "NW";

  // Assist level label
  const char *assistLabels[] = {"RAW", "MINIMAL", "STANDARD", "FULL"};
  const char *assistStr = assistLabels[m_player.flightAssist()];

  char buf[64];
  const int px = sw - 220;
  int py = 12;
  const int lh = 18;
  const Color col = {200, 220, 255, 220};

  int panelLines = m_camMode == CamMode::FreeRoam ? 8 : 7;
  DrawRectangle(px - 8, py - 4, 216, lh * panelLines + 8, {0, 0, 0, 120});

  snprintf(buf, sizeof(buf), "X: %7.1f", pos.x);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Z: %7.1f", pos.z);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Alt:  %6.1f m", pos.y);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "AGL:  %6.1f m", agl);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Spd:  %6.1f", spd);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Hdg:  %5.1f  %s", yawDeg, compass);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Asst: %s", assistStr);
  DrawText(buf, px, py, 14, col);
  py += lh;

#ifdef DEV_MODE
  if (m_camMode == CamMode::FreeRoam) {
    DrawText("[ FREE CAM ]", px, py, 14, YELLOW);
  }
#endif

  // ---- Health bar ----
  const int bx = 12, by = sh - 60, bw = 160, bh = 14;
  DrawRectangle(bx - 1, by - 1, bw + 2, bh + 2, {0, 0, 0, 160});
  DrawRectangle(bx, by, bw, bh, {50, 50, 50, 255});
  int healthW = static_cast<int>(bw * m_player.health() / 100.0f);
  Color hpCol = healthW > bw / 2   ? Color{40, 200, 60, 255}
                : healthW > bw / 4 ? Color{220, 180, 0, 255}
                                   : Color{210, 40, 40, 255};
  DrawRectangle(bx, by, healthW, bh, hpCol);
  DrawText("HULL", bx, by - 16, 12, col);

  // ---- Controls bar ----
  DrawRectangle(0, sh - 28, sw, 28, {0, 0, 0, 140});

  const char *controls =
      m_camMode == CamMode::Follow
          ? "W/S: thrust/brake  |  A/D: turn  |  Q/E: altitude  |  Shift: boost"
          : "WASD: fly  |  Q/E: up/down  |  Shift: fast  (F1: back to ship)";
  DrawText(controls, 12, sh - 20, 14, {180, 180, 180, 220});

#ifdef DEV_MODE
  DrawText("[DEV]  F1:cam  F2:assist", sw - 200, sh - 20, 14, YELLOW);
#endif
}

// ================================================================
// shutdown
// ================================================================
void GameState::shutdown() {
  m_player.unload();
  m_planet.unload();
}