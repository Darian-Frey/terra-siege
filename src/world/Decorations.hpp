#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

class Planet;

// ====================================================================
// Engine Phase 3.5 — atmospheric terrain objects.
//
// Non-interactive scenery distributed across the world to give it
// scale and texture. Strictly visual — no collision, no AI, no damage,
// no entity-pool slots burned. Instances are ~32 bytes each so a few
// hundred fit in cache with no fuss.
//
// Per CLAUDE.md §3.5 — tree clusters, rock formations, antennas,
// crash sites. All four placed by the same procedural pass at world-
// generation time, then static for the session.
//
// Render path does a radial distance check against the camera and
// skips anything past ~800 units — fog hides the rest anyway.
// ====================================================================

struct Decoration {
  enum class Kind : uint8_t {
    Tree,      // One Decoration = one tree. Forests are made of thousands
               // of Tree decorations placed by a noise-mask pass.
    Rock,
    Antenna,
    CrashSite,
  };

  Kind kind = Kind::Rock;
  Vector3 pos{0, 0, 0}; // world-space, y = terrain height at pos
  float yaw = 0.0f;     // visual rotation (purely cosmetic)
  float scale = 1.0f;   // per-instance size variation
  uint32_t seed = 0;    // per-instance RNG (cluster count, debris jitter, ...)
};

class Decorations {
public:
  Decorations() = default;

  // Procedural placement — samples random world points, gates each on
  // terrain rules (sea-level cutoff, slope, elevation), and pushes
  // accepted Decorations into the list. Deterministic given the seed
  // so reroll (F5 in DEV_MODE) reproduces consistently.
  void generate(const Planet &planet, uint32_t seed);

  // Draw every decoration within visible range of the camera. Planet
  // is needed because tree clusters / rocks / crash sites scatter
  // their sub-instances at draw time and query heightAt() for each.
  // Called inside BeginMode3D so geometry composites with the terrain.
  void render(const Planet &planet, Vector3 cameraPos) const;

  // Clear all instances — called by Planet regeneration paths.
  void clear() { m_items.clear(); }
  size_t count() const { return m_items.size(); }

private:
  std::vector<Decoration> m_items;
};
