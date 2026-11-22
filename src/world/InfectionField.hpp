#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

// ====================================================================
// Ground infection (Settlements track R.8a, F-052).
//
// One byte of infection (0..255 = 0..1) per heightmap cell over the
// whole toroidal world, mirrored in a single-channel GPU texture that
// wraps. Seeders deposit it under themselves; the player cleanses it.
// The terrain and the instanced scenery shaders sample the texture by
// world position, so infected land, trees, hedges and buildings all
// turn alien together, with no per-object state.
//
// CPU writes mark it dirty; upload() pushes the texture, rate-limited
// by the caller. Needs a GL context for init() (after InitWindow).
// ====================================================================
class InfectionField {
public:
  void init();   // allocate the grid and texture, all clean
  void unload(); // free the texture
  void clear();

  // Add (or remove) infection in a disc of `radius` metres round a
  // world point, falling off to nothing at the edge. Wrap-aware.
  void deposit(float worldX, float worldZ, float radius, float amount);
  void cleanse(float worldX, float worldZ, float radius, float amount) {
    deposit(worldX, worldZ, radius, -amount);
  }

  float at(float worldX, float worldZ) const;
  // Mean infection over a disc, sampled every `step` metres.
  float average(float worldX, float worldZ, float radius, float step = 16.0f) const;

  // Push the grid to the GPU if it changed since the last upload.
  void upload();
  const Texture2D &texture() const { return m_tex; }
  bool ready() const { return m_tex.id != 0; }

private:
  int m_n = 0;
  float m_cell = 0.0f;
  std::vector<float> m_value;   // 0..1 per cell (float for slow rates)
  std::vector<uint8_t> m_bytes; // texture mirror
  Texture2D m_tex{};
  bool m_dirty = false;
};
