#include "Inspector.hpp"
#include "core/Config.hpp"
#include "raylib.h"

#include <cstdio>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <path-to-obj>\n",
                 argc > 0 ? argv[0] : "terra-siege-inspect");
    return 2;
  }

  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT,
             "terra-siege inspector");
  SetTargetFPS(60);

  tsmesh::Inspector inspector;
  if (!inspector.load(argv[1])) {
    std::fprintf(stderr, "Failed to load: %s\n", argv[1]);
    CloseWindow();
    return 1;
  }

  int rc = inspector.run();
  CloseWindow();
  return rc;
}
