#include "core/Clock.hpp"
#include "core/Config.hpp"
#include "core/GameState.hpp"
#include "raylib.h"

int main() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT, "terra-siege");
  SetTargetFPS(Config::TARGET_FPS);
  InitAudioDevice();
  DisableCursor(); // capture mouse for camera look

#ifdef DEV_MODE
  TraceLog(LOG_WARNING, "DEV_MODE active — cheats and debug overlay enabled");
#endif

  GameState game;
  game.init();

  Clock clock;

  while (!WindowShouldClose()) {
    const float frameTime = GetFrameTime();
    clock.accumulate(frameTime);

    while (clock.shouldTick()) {
      game.update(Config::FIXED_DT);
      clock.consume();
    }

    const float alpha = clock.alpha();

    BeginDrawing();
    game.render(alpha); // GameState sets its own ClearBackground
    EndDrawing();
  }

  game.shutdown();
  CloseAudioDevice();
  CloseWindow();
  return 0;
}