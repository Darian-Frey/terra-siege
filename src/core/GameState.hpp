#pragma once

#include "raylib.h"
#include "world/Planet.hpp"

// ====================================================================
// GameState — top-level state machine
// ====================================================================

enum class AppState {
  MainMenu,
  Playing,
  Paused,
  GameOver,
  Victory,
};

class GameState {
public:
  void init();
  void update(float dt);
  void render(float alpha);
  void shutdown();

  AppState state() const { return m_state; }

private:
  void updateFreeCamera(float dt);
  void drawHUD() const;

  AppState m_state = AppState::MainMenu;
  Planet m_planet;
  Camera3D m_camera = {};

  // Free-roam camera state
  float m_camYaw = 0.0f;
  float m_camPitch = -0.35f;
  float m_camSpeed = 40.0f;
};