#include "GameState.hpp"
#include "Config.hpp"
#include "raylib.h"

void GameState::init() { m_state = AppState::Playing; }

void GameState::update(float dt) {
  (void)dt;

  switch (m_state) {
  case AppState::Playing:
    break;
  case AppState::Paused:
    break;
  case AppState::MainMenu:
  case AppState::GameOver:
  case AppState::Victory:
    break;
  }
}

void GameState::render(float alpha) {
  (void)alpha;

  switch (m_state) {
  case AppState::Playing:
    DrawText("terra-siege — Phase 1 scaffold", 40, 40, 28, RAYWHITE);
    DrawText("Subsystems initialising...", 40, 80, 18, GRAY);
#ifdef DEV_MODE
    DrawText("[DEV MODE]", 40, GetScreenHeight() - 30, 16, YELLOW);
#endif
    break;
  case AppState::MainMenu:
    DrawText("MAIN MENU (todo)", 40, 40, 28, RAYWHITE);
    break;
  case AppState::Paused:
    DrawText("PAUSED", 40, 40, 28, RAYWHITE);
    break;
  case AppState::GameOver:
    DrawText("GAME OVER", 40, 40, 28, RED);
    break;
  case AppState::Victory:
    DrawText("VICTORY", 40, 40, 28, GREEN);
    break;
  }
}

void GameState::shutdown() {}