#pragma once

#include "entity/Player.hpp"
#include "raylib.h"
#include "world/Planet.hpp"

// ====================================================================
// GameState — top-level state machine
// ====================================================================

enum class AppState { MainMenu, Playing, Paused, GameOver, Victory };
enum class CamMode { Follow, FreeRoam }; // F1 toggles in dev mode

class GameState {
public:
  void init();
  void update(float dt);
  void render(float alpha);
  void shutdown();

  AppState state() const { return m_state; }

private:
  // Update helpers
  void updateFreeCamera(float dt);
  void updateFollowCamera(float dt);
  void handleDevKeys();

  // Render helpers
  void drawHUD() const;
  void drawDebugPanel() const;

  // State
  AppState m_state = AppState::MainMenu;
  CamMode m_camMode = CamMode::Follow;

  // World
  Planet m_planet;
  Player m_player;

  // Camera
  Camera3D m_camera = {};
  float m_camYaw = 0.0f;
  float m_camPitch = -0.35f;
  float m_camSpeed = 40.0f;
};