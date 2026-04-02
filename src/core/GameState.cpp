#include "GameState.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>

void GameState::init() {
  m_planet.generate(12345u);

  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);

  m_camera.position = {mid, ground + 350.0f, mid - 400.0f};
  m_camera.target = {mid, ground + 30.0f, mid + 200.0f};
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = Config::CAM_FOV;
  m_camera.projection = CAMERA_PERSPECTIVE;

  Vector3 dir =
      Vector3Normalize(Vector3Subtract(m_camera.target, m_camera.position));
  m_camYaw = atan2f(dir.x, dir.z);
  m_camPitch = asinf(dir.y);

  m_state = AppState::Playing;
}

void GameState::updateFreeCamera(float dt) {
  Vector2 delta = GetMouseDelta();
  m_camYaw += delta.x * 0.003f;
  m_camPitch -= delta.y * 0.003f;
  if (m_camPitch > 1.4f)
    m_camPitch = 1.4f;
  if (m_camPitch < -1.4f)
    m_camPitch = -1.4f;

  Vector3 forward = {cosf(m_camPitch) * sinf(m_camYaw), sinf(m_camPitch),
                     cosf(m_camPitch) * cosf(m_camYaw)};
  Vector3 right = {sinf(m_camYaw - 1.5708f), 0.0f, cosf(m_camYaw - 1.5708f)};

  float speed = m_camSpeed;
  if (IsKeyDown(KEY_LEFT_SHIFT))
    speed *= 4.0f;

  if (IsKeyDown(KEY_W))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(forward, speed * dt));
  if (IsKeyDown(KEY_S))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(forward, -speed * dt));
  if (IsKeyDown(KEY_D))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(right, speed * dt));
  if (IsKeyDown(KEY_A))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(right, -speed * dt));
  if (IsKeyDown(KEY_E))
    m_camera.position.y += speed * dt;
  if (IsKeyDown(KEY_Q))
    m_camera.position.y -= speed * dt;

  m_camera.target = Vector3Add(m_camera.position, forward);
}

void GameState::update(float dt) {
  switch (m_state) {
  case AppState::Playing:
    updateFreeCamera(dt);
    break;
  default:
    break;
  }
}

void GameState::render(float alpha) {
  (void)alpha;

  switch (m_state) {
  case AppState::Playing: {
    ClearBackground({55, 80, 140, 255});
    BeginMode3D(m_camera);
    m_planet.draw(m_camera.position);
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

void GameState::drawHUD() const {
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();

  // ---- FPS (top left) ----
  DrawFPS(12, 12);

  // ---- Debug info panel (top right) ----
  const Vector3 &pos = m_camera.position;
  float terrainH = m_planet.heightAt(pos.x, pos.z);
  float agl = pos.y - terrainH; // altitude above ground
  float yawDeg = m_camYaw * (180.0f / 3.14159f);
  float pitchDeg = m_camPitch * (180.0f / 3.14159f);

  // Normalise yaw to [0, 360)
  while (yawDeg < 0.0f)
    yawDeg += 360.0f;
  while (yawDeg >= 360.0f)
    yawDeg -= 360.0f;

  // Compass heading label
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

  char buf[64];
  const int px = sw - 220;
  int py = 12;
  const int lineH = 18;
  const Color col = {200, 220, 255, 220};

  // Semi-transparent backing panel
  DrawRectangle(px - 8, py - 4, 216, lineH * 6 + 8, {0, 0, 0, 120});

  snprintf(buf, sizeof(buf), "X: %7.1f", pos.x);
  DrawText(buf, px, py, 14, col);
  py += lineH;

  snprintf(buf, sizeof(buf), "Z: %7.1f", pos.z);
  DrawText(buf, px, py, 14, col);
  py += lineH;

  snprintf(buf, sizeof(buf), "Alt:  %6.1f m", pos.y);
  DrawText(buf, px, py, 14, col);
  py += lineH;

  snprintf(buf, sizeof(buf), "AGL:  %6.1f m", agl);
  DrawText(buf, px, py, 14, col);
  py += lineH;

  snprintf(buf, sizeof(buf), "Hdg: %5.1f  %s", yawDeg, compass);
  DrawText(buf, px, py, 14, col);
  py += lineH;

  snprintf(buf, sizeof(buf), "Pitch:%5.1f", pitchDeg);
  DrawText(buf, px, py, 14, col);

  // ---- Controls bar (bottom) ----
  DrawRectangle(0, sh - 28, sw, 28, {0, 0, 0, 140});
  DrawText("WASD: move  |  Mouse: look  |  Q/E: down/up  |  Shift: fast", 12,
           sh - 20, 14, {180, 180, 180, 220});

#ifdef DEV_MODE
  DrawText("[DEV MODE]", sw - 110, sh - 20, 14, YELLOW);
#endif
}

void GameState::shutdown() { m_planet.unload(); }