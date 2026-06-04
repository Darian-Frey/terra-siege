#include "Inspector.hpp"
#include "core/Config.hpp"
#include "raylib.h"

#include <cstdio>

int main(int argc, char **argv) {
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(Config::SCREEN_WIDTH, Config::SCREEN_HEIGHT,
             "terra-siege inspector");
  SetTargetFPS(60);

  tsmesh::Inspector inspector;
  // CLI arg is a shortcut for "boot and immediately open this file".
  // No arg → empty workspace; user presses O / drag-and-drops.
  if (argc >= 2) {
    if (!inspector.load(argv[1])) {
      std::fprintf(stderr, "[main] failed to load %s — starting empty\n",
                   argv[1]);
    }
  }

  int rc = inspector.run();
  CloseWindow();
  return rc;
}
