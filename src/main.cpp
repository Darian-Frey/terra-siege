#include "core/Clock.hpp"
#include "core/Config.hpp"
#include "core/GameState.hpp"
#include "raylib.h"

int main() {
  // Quiet raylib's per-VAO mesh-upload / unload / shader-link chatter
  // at LOG_INFO. Our own diagnostic messages use LOG_WARNING + LOG_INFO
  // explicitly; LOG_INFO from our code is dev-mode only, so suppress
  // it in release builds and keep it in DEV_MODE for hotkey traces.
  // Must run BEFORE InitWindow so the early raylib startup chatter
  // ("Display size...", "OpenGL initialised", etc.) also stays quiet.
#ifdef DEV_MODE
  SetTraceLogLevel(LOG_INFO);
#else
  SetTraceLogLevel(LOG_WARNING);
#endif

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