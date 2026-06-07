#include "InspectorFont.hpp"

namespace tsmesh {

namespace {

Font g_font{};
bool g_loaded = false;

// 36-px atlas — supports the inspector's 12-20 px sizes cleanly via
// bilinear filtering. Larger would help if the inspector ever needs
// bigger headers but the empty-workspace overlay (36 px) is the
// current ceiling and renders fine.
constexpr int kAtlasSize = 36;

const char *kCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
    "/Library/Fonts/Menlo.ttc",
};

} // anonymous namespace

void loadInspectorFont() {
  if (g_loaded) return;
  for (const char *path : kCandidates) {
    if (!FileExists(path)) continue;
    Font f = LoadFontEx(path, kAtlasSize, nullptr, 0);
    if (f.texture.id == 0) continue;
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    g_font = f;
    g_loaded = true;
    TraceLog(LOG_INFO, "Inspector font loaded: %s", path);
    return;
  }
  TraceLog(LOG_INFO,
           "Inspector font: no system TTF found, falling back to raylib default");
}

void unloadInspectorFont() {
  if (!g_loaded) return;
  UnloadFont(g_font);
  g_loaded = false;
}

void drawText(const char *text, int x, int y, int size, Color color) {
  if (g_loaded) {
    Vector2 p{static_cast<float>(x), static_cast<float>(y)};
    DrawTextEx(g_font, text, p, static_cast<float>(size), 1.0f, color);
  } else {
    DrawText(text, x, y, size, color);
  }
}

int measureText(const char *text, int size) {
  if (g_loaded) {
    return static_cast<int>(
        MeasureTextEx(g_font, text, static_cast<float>(size), 1.0f).x);
  }
  return MeasureText(text, size);
}

} // namespace tsmesh
