#include "core/Clock.hpp"
#include "core/Config.hpp"
#include "core/GameState.hpp"
#include "raylib.h"

int main() {
  // ----------------------------------------------------------------
  // Window initialisation
  // ----------------------------------------------------------------
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT, "terra-siege");
  SetTargetFPS(Config::TARGET_FPS);
  InitAudioDevice();

#ifdef DEV_MODE
  TraceLog(LOG_WARNING, "DEV_MODE active — cheats and debug overlay enabled");
#endif

  // ----------------------------------------------------------------
  // Game state
  // ----------------------------------------------------------------
  GameState game;
  game.init();

  // ----------------------------------------------------------------
  // Fixed-timestep accumulator loop
  // ----------------------------------------------------------------
  Clock clock;

  while (!WindowShouldClose()) {
    const float frameTime = GetFrameTime();
    clock.accumulate(frameTime);

    // Fixed update ticks
    while (clock.shouldTick()) {
      game.update(Config::FIXED_DT);
      clock.consume();
    }

    // Render — interpolation alpha for smooth motion between ticks
    const float alpha = clock.alpha();

    BeginDrawing();
    ClearBackground(BLACK);
    game.render(alpha);
    EndDrawing();
  }

  // ----------------------------------------------------------------
  // Shutdown
  // ----------------------------------------------------------------
  game.shutdown();
  CloseAudioDevice();
  CloseWindow();

  return 0;
}