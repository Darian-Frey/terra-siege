#include "GameState.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cmath>

void GameState::init() {
  m_planet.generate(12345u);

  // Position camera above terrain looking down at an angle
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);

  // Sit 350 units above ground, 400 units behind centre, angled down
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
    // Horizon blue sky
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
  DrawFPS(12, 12);
  DrawText("WASD: move  |  Mouse: look  |  Q/E: down/up  |  Shift: fast", 12,
           GetScreenHeight() - 24, 14, GRAY);
#ifdef DEV_MODE
  DrawText("[DEV MODE]", GetScreenWidth() - 120, GetScreenHeight() - 24, 14,
           YELLOW);
#endif
}

void GameState::shutdown() { m_planet.unload(); }