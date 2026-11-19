#pragma once

#include "raylib.h"

#include <array>
#include <cstdint>
#include <filesystem>
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
// skips anything past DECOR_RENDER_RADIUS — fog hides the rest anyway.
//
// Trees and rocks are OBJ models (tree_*.obj, rock_*.obj from
// scripts/meshgen/scenery.py) drawn with GPU instancing: there are
// ~135k trees, far too many for a draw call each. Their transforms are
// built once per placement into a grid of DECOR_CELL_SIZE cells, and
// each frame the cells near the camera are gathered into one
// DrawMeshInstanced call per model. If the models or the instancing
// shader fail to load, both kinds fall back to the old box geometry.
// Antennas and crash sites are still procedural.
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

  // Load the tree / boulder models and the instancing shader. Needs a GL
  // context (after InitWindow). unloadModels() before CloseWindow.
  void loadModels(const std::filesystem::path &meshesDir);
  void unloadModels();

  // Clear all instances — called by Planet regeneration paths.
  void clear() {
    m_items.clear();
    for (auto &v : m_xf) v.clear();
    for (auto &v : m_cellStart) v.clear();
  }
  // Remove decorations inside a square (toroidal), e.g. a base apron, and
  // re-seat those inside the larger `reseatHalf` square onto the current
  // terrain (after Planet::flattenPad has reshaped the ground there).
  void clearSquare(const Planet &planet, float x, float z, float half,
                   float reseatHalf);
  size_t count() const { return m_items.size(); }

private:
  // Instanced scenery models, in tree_*.obj / rock_*.obj order.
  enum ModelSlot { kConifer, kBroadleaf, kRockA, kRockB, kRockC, kModelCount };
  static constexpr int kFirstRock = kRockA;
  static constexpr int kRockVariants = kModelCount - kFirstRock;

  // Rebuild m_xf / m_cellStart from m_items. Called after every change
  // to the placement (generate, clearSquare).
  void buildInstances(const Planet &planet);
  bool treesInstanced() const;
  bool rocksInstanced() const;

  std::vector<Decoration> m_items;

  std::array<Model, kModelCount> m_models{};
  std::array<bool, kModelCount> m_modelLoaded{};
  Shader m_instShader{};
  Material m_instMaterial{};
  bool m_instReady = false;

  // Per model: instance transforms sorted by grid cell (translation
  // wrapped into [0, worldSize)), and each cell's start offset into
  // them (gridN² + 1 entries).
  std::array<std::vector<Matrix>, kModelCount> m_xf;
  std::array<std::vector<uint32_t>, kModelCount> m_cellStart;
  int m_gridN = 0;
  // Per-frame gather buffer for the visible instances of one model.
  // Reserved at build time to the largest model's count, so render()
  // never allocates.
  mutable std::vector<Matrix> m_visible;
};
