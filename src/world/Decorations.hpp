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
// generation time, then static for the session. Trees follow the
// planet's FieldMap: dense forests inside the forest mask (their
// interiors filled with grove models, whole stands of one species, and
// their thinner edges with copses), and hedgerows, tree lines, poplar
// rows, dry-stone walls and fences along farmland field edges (4 m
// segments laid end to end).
//
// Render path does a radial distance check against the camera and
// skips anything past DECOR_RENDER_RADIUS — fog hides the rest anyway.
//
// Decorations are OBJ models (from scripts/meshgen/scenery.py) drawn
// with GPU instancing: there are hundreds of thousands of trees and
// bushes, far too many for a draw call each. Their transforms are built
// once per placement into a grid of DECOR_CELL_SIZE cells, and
// each frame the cells near the camera are gathered into one
// DrawMeshInstanced call per model. If a kind's models or the
// instancing shader fail to load, that kind falls back to boxes.
// Every kind is a model now: trees, boulders (rock clusters), antennas,
// and crash sites (a wreck plus scattered debris chunks).
// ====================================================================

struct Decoration {
  enum class Kind : uint8_t {
    Tree,      // One Decoration = one tree or bush (see TreeModel). Forests
               // and hedgerows are made of thousands of them.
    Rock,
    Antenna,
    CrashSite,
    Building,  // Settlement buildings and garden boundaries (BuildingModel),
               // laid out by SettlementLayout; level, never sheared.
  };
  // Settlement models, in the Decorations building-slot order (from
  // scripts/meshgen/settlements.py). Garden walls and the picket fence
  // are 4 m boundary segments, stretched along their length like hedges.
  enum class BuildingModel : uint8_t {
    FarmhouseA, FarmhouseB, BarnA, BarnB, DutchBarn, GrainSilo, OutbuildingA, OutbuildingB,
    Greenhouse, Windmill, HayBalesA, HayBalesB, Tractor,
    CottageA, CottageB, CottageC, HouseA, HouseB, HouseC, TerraceRowA, TerraceRowB,
    ChurchA, ChurchB, Chapel, Pub, VillageShop, VillageHall, WarMemorial,
    GardenWallA, GardenWallB, PicketFence,
    // Town set and street furniture (R.7).
    TownhouseA, TownhouseB, TownhouseC, ShopRowA, ShopRowB, TownHall, MarketHall,
    ApartmentBlockA, ApartmentBlockB, OfficeBlockA, OfficeBlockB, WarehouseA, WarehouseB,
    Factory, Chimney, WaterTower, PetrolStation, ParkedCarA, ParkedCarB, ParkedCarC,
    StreetLamp, TelegraphPole, RoadSign, Signpost, BusShelter, PhoneBox,
    // Bridges (R.6): spans laid along roads over water, sheared to the
    // deck slope (`slope`); piers scaled to height (`stretch` is vertical).
    BridgeStone, BridgeGirder, BridgePier,
    Count
  };

  // Which model a Tree uses, in the Decorations model-slot order. From
  // GroveConiferA on, each model is several plants in one ("stands"):
  // groves fill dense forest, copses its thinner edges, and boundary
  // segments (hedges, dry-stone walls, fences; 4 m, stretched to fit)
  // line field edges. Stands are sheared onto the ground slope.
  enum class TreeModel : uint8_t {
    Conifer, Broadleaf, Poplar, Birch, Bush, Dead,
    GroveConiferA, GroveConiferB, GroveBroadleafA, GroveBroadleafB, GroveBirch, GroveMixed,
    CopseConifer, CopseBroadleaf, CopseBirch, CopseMixed,
    HedgeA, HedgeB, HedgeTree, HedgeBirch,
    WallA, WallB, FenceA, FenceB
  };
  static constexpr bool isStand(TreeModel m) { return m >= TreeModel::GroveConiferA; }
  static constexpr bool isGrove(TreeModel m) {
    return m >= TreeModel::GroveConiferA && m <= TreeModel::GroveMixed;
  }
  static constexpr bool isSegment(TreeModel m) { return m >= TreeModel::HedgeA; }

  Kind kind = Kind::Rock;
  TreeModel tree = TreeModel::Broadleaf;
  BuildingModel building = BuildingModel::CottageA;
  Vector3 pos{0, 0, 0}; // world-space, y = terrain height at pos
  float yaw = 0.0f;     // visual rotation (purely cosmetic)
  float scale = 1.0f;   // per-instance size variation
  float stretch = 1.0f; // extra scale along local Z (boundary segments fit their edge)
  float slope = 0.0f;   // bridge spans: rise per metre along local Z (deck shear)
  uint16_t site = 0xffff; // buildings: index of their settlement site (R.8)
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
  // Rebuilding the instance data walks every decoration, so a batch of
  // clears passes rebuild = false and calls rebuildInstances() once.
  void clearSquare(const Planet &planet, float x, float z, float half,
                   float reseatHalf, bool rebuild = true);
  void rebuildInstances(const Planet &planet) { buildInstances(planet); }
  size_t count() const { return m_items.size(); }

  // Ground infection overlay (R.8a): the InfectionField texture the
  // instancing shader samples at each object's origin.
  void setInfectionTexture(Texture2D tex) {
    if (m_instReady) m_instMaterial.maps[MATERIAL_MAP_ALBEDO].texture = tex;
  }
private:
  // Instanced scenery models, in kModelFiles order.
  enum ModelSlot {
    kConifer, kBroadleaf, kPoplar, kBirch, kBush, kDeadTree,
    kGroveConiferA, kGroveConiferB, kGroveBroadleafA, kGroveBroadleafB, kGroveBirch,
    kGroveMixed,
    kCopseConifer, kCopseBroadleaf, kCopseBirch, kCopseMixed,
    kHedgeA, kHedgeB, kHedgeTree, kHedgeBirch,
    kWallA, kWallB, kFenceA, kFenceB,
    kRockA, kRockB, kRockC,
    kAntenna,
    kCrashHull, kCrashDebris,
    kBuildingFirst,
    kModelCount = kBuildingFirst + static_cast<int>(Decoration::BuildingModel::Count)
  };
  static constexpr int kFirstRock = kRockA;
  static_assert(kFenceB - kConifer == static_cast<int>(Decoration::TreeModel::FenceB),
                "tree model slots follow Decoration::TreeModel");
  static constexpr int kRockVariants = kRockC - kRockA + 1;
  static constexpr int kKindCount = 5;
  // The decoration kind each model slot belongs to. A kind is drawn
  // instanced only when every one of its slots loaded.
  static constexpr Decoration::Kind slotKind(int m) {
    return m < kRockA                ? Decoration::Kind::Tree
           : m < kAntenna            ? Decoration::Kind::Rock
           : m == kAntenna           ? Decoration::Kind::Antenna
           : m < kBuildingFirst      ? Decoration::Kind::CrashSite
                                     : Decoration::Kind::Building;
  }

  // Rebuild m_xf / m_cellStart from m_items. Called after every change
  // to the placement (generate, clearSquare).
  void buildInstances(const Planet &planet);
  std::array<bool, kKindCount> instancedKinds() const;

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
  Texture2D m_noInfection{}; // 1×1 black: clean ground before a world exists
  // Per-frame gather buffer for the visible instances of one model.
  // Reserved at build time to the largest model's count, so render()
  // never allocates.
  mutable std::vector<Matrix> m_visible;
};
