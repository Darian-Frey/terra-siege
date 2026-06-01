#pragma once

#include "raylib.h"
#include <cstdint>
#include <string_view>

// ====================================================================
// 32-colour palette for OBJ mesh authoring (3d_assets.md §6, extended
// from the original 16-colour spec to capture the ship-side palette
// already in use across the codebase).
//
// Blender material names encode the palette index:
//   c00..c31         → indices 0..31
//   palette_00..31   → indices 0..31 (alias)
//   anything else    → kPaletteFallback (debug magenta)
//
// Indices 0..15 reserved for original Virus-style primaries + utility
// colours. Indices 16..31 capture existing hand-coded ship/structure
// colours from EntityManager.cpp render paths so converting a ship's
// procedural geometry to an OBJ preserves the look at parity.
// ====================================================================
namespace tsmesh {

constexpr Color kPalette[32] = {
    // ---- 0-15: Virus-era primaries + utility ----
    {0, 0, 0, 255},        // 0:  black
    {255, 255, 255, 255},  // 1:  white
    {255, 0, 0, 255},      // 2:  red
    {0, 255, 0, 255},      // 3:  green
    {0, 0, 255, 255},      // 4:  blue
    {255, 255, 0, 255},    // 5:  yellow
    {255, 0, 255, 255},    // 6:  magenta
    {0, 255, 255, 255},    // 7:  cyan
    {128, 128, 128, 255},  // 8:  mid grey
    {192, 128, 64, 255},   // 9:  terrain brown
    {64, 128, 192, 255},   // 10: sky blue
    {255, 128, 0, 255},    // 11: orange
    {128, 0, 255, 255},    // 12: purple
    {0, 128, 0, 255},      // 13: dark green
    {128, 64, 0, 255},     // 14: dark brown
    {200, 200, 200, 255},  // 15: light grey

    // ---- 16-31: Ship-side / structure / damage colours ----
    // Snapped from existing EntityManager render paths. Comments
    // name the original site so re-mapping is traceable.
    {200, 50, 50, 255},    // 16: fighter body (red)
    {240, 200, 80, 255},   // 17: fighter nose tip / cabin pip
    {200, 80, 220, 255},   // 18: drone body (magenta-purple)
    {120, 60, 160, 255},   // 19: seeder hull (dark purple)
    {200, 140, 240, 255},  // 20: seeder hatch (lavender)
    {70, 80, 110, 255},    // 21: carrier hull (navy)
    {40, 50, 80, 255},     // 22: carrier belly (deep navy)
    {180, 200, 240, 255},  // 23: carrier spire / shield-blue
    {120, 130, 80, 255},   // 24: bomber fuselage (olive)
    {90, 100, 60, 255},    // 25: bomber pod (dark olive)
    {110, 100, 90, 255},   // 26: tank chassis (military tan-grey)
    {160, 70, 60, 255},    // 27: tank turret cap / base turret cap
    {80, 110, 70, 255},    // 28: collector hull (olive-green)
    {120, 220, 100, 255},  // 29: collector cargo-loaded green
    {90, 100, 110, 255},   // 30: base pad / structure grey
    {120, 220, 140, 255},  // 31: heal-pad green / friendly accent
};

constexpr Color kPaletteFallback = {255, 0, 255, 255}; // debug magenta

// Parse a material name into a palette index. Returns -1 (= use
// kPaletteFallback) on unrecognised names. Accepts:
//   "c07"           → 7
//   "c31"           → 31
//   "palette_07"    → 7
//   "palette_31"    → 31
//   anything else   → -1
//
// Strict numeric format: exactly the digits after the prefix matter,
// no leading whitespace, no trailing characters. Two digits required
// (or one, for compatibility with single-digit indices).
int parsePaletteIndex(std::string_view materialName);

// Convenience — resolve a material name straight to a Color.
inline Color colourForMaterial(std::string_view materialName) {
  int idx = parsePaletteIndex(materialName);
  if (idx < 0 || idx >= 32) return kPaletteFallback;
  return kPalette[idx];
}

} // namespace Mesh
