// T-01..T-05 from 3d_assets.md §10 — verify the OBJ loader against the
// fixture meshes in tests/fixtures/meshes/.
//
// Tests don't link the rest of the game — just the mesh subsystem +
// raylib (for Vector3/Color types). uploadModel() is NOT exercised
// here because it requires a GL context; that path is covered by
// the actual game build.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "mesh/ObjLoader.hpp"
#include "mesh/Palette.hpp"

#include <filesystem>

namespace {

// Resolve a fixture path from the compile-time-defined fixtures root.
// CMake passes TERRA_SIEGE_FIXTURE_ROOT pointing at tests/fixtures so
// the tests can run from any working directory.
std::filesystem::path fixture(const char *name) {
  return std::filesystem::path(TERRA_SIEGE_FIXTURE_ROOT) / "meshes" / name;
}

} // namespace

// ---- T-01: canonical cube (8 verts, 12 tris, single material c01) ----
TEST_CASE("T-01: load canonical cube") {
  auto r = tsmesh::loadMesh(fixture("cube.obj"));
  REQUIRE(r.ok());
  CHECK(r.mesh.vertices.size() == 8);
  CHECK(r.mesh.indices.size() == 36); // 12 tris × 3
  CHECK(r.mesh.faceNormals.size() == 12);
  CHECK(r.mesh.facePalette.size() == 12);
  // All faces use c01 (white) → palette index 1.
  for (int idx : r.mesh.facePalette) {
    CHECK(idx == 1);
  }
}

// ---- T-02: cube authored as 6 quads → triangulates to 12 tris ----
TEST_CASE("T-02: quads triangulate to N×2 triangles") {
  auto r = tsmesh::loadMesh(fixture("cube_quads.obj"));
  REQUIRE(r.ok());
  CHECK(r.mesh.vertices.size() == 8);
  CHECK(r.mesh.indices.size() == 36); // 6 quads × 2 tris × 3 verts
  CHECK(r.mesh.faceNormals.size() == 12);
}

// ---- T-03: usemtl tracking across multiple materials ----
TEST_CASE("T-03: usemtl c11 maps to palette index 11") {
  auto r = tsmesh::loadMesh(fixture("cube_multimat.obj"));
  REQUIRE(r.ok());
  CHECK(r.mesh.facePalette.size() == 12);
  // First 10 faces are c08, last 2 are c11.
  for (size_t i = 0; i < 10; ++i) {
    CHECK(r.mesh.facePalette[i] == 8);
  }
  CHECK(r.mesh.facePalette[10] == 11);
  CHECK(r.mesh.facePalette[11] == 11);
}

// ---- T-04: bogus material name → fallback (-1) ----
TEST_CASE("T-04: unrecognised material name falls back to -1") {
  auto r = tsmesh::loadMesh(fixture("cube_bogus_mat.obj"));
  REQUIRE(r.ok());
  for (int idx : r.mesh.facePalette) {
    CHECK(idx == -1);
  }
}

// ---- T-05: degenerate triangle skipped, remaining mesh intact ----
TEST_CASE("T-05: degenerate triangle skipped, mesh loads") {
  auto r = tsmesh::loadMesh(fixture("cube_degenerate.obj"));
  REQUIRE(r.ok());
  // 12 normal tris + 1 degenerate tri in the file. Loader skips the
  // degenerate one and emits exactly 12 faces.
  CHECK(r.mesh.indices.size() == 36);
  CHECK(r.mesh.faceNormals.size() == 12);
}

// ---- Palette parser unit tests (extra coverage beyond T-01..T-05) ----
TEST_CASE("palette index parsing — accepted forms") {
  CHECK(tsmesh::parsePaletteIndex("c00") == 0);
  CHECK(tsmesh::parsePaletteIndex("c07") == 7);
  CHECK(tsmesh::parsePaletteIndex("c15") == 15);
  CHECK(tsmesh::parsePaletteIndex("c31") == 31);
  CHECK(tsmesh::parsePaletteIndex("palette_00") == 0);
  CHECK(tsmesh::parsePaletteIndex("palette_07") == 7);
  CHECK(tsmesh::parsePaletteIndex("palette_31") == 31);
  // Single-digit form should also work for compactness.
  CHECK(tsmesh::parsePaletteIndex("c0") == 0);
  CHECK(tsmesh::parsePaletteIndex("c9") == 9);
}

TEST_CASE("palette index parsing — rejected forms") {
  CHECK(tsmesh::parsePaletteIndex("") == -1);
  CHECK(tsmesh::parsePaletteIndex("c") == -1);
  CHECK(tsmesh::parsePaletteIndex("c32") == -1);       // out of range
  CHECK(tsmesh::parsePaletteIndex("c99") == -1);       // out of range
  CHECK(tsmesh::parsePaletteIndex("c100") == -1);      // too many digits
  CHECK(tsmesh::parsePaletteIndex("cAB") == -1);       // non-digit
  CHECK(tsmesh::parsePaletteIndex("not_a_palette") == -1);
  CHECK(tsmesh::parsePaletteIndex("palette_") == -1);  // empty body
  CHECK(tsmesh::parsePaletteIndex("palette_AB") == -1);// non-digit body
}

// Bonus — confirm the colour table is the size the spec calls for.
TEST_CASE("palette table size matches 32-colour spec") {
  // sizeof / sizeof first elem — would be a compile-time error if
  // the array shrinks below 32.
  CHECK(sizeof(tsmesh::kPalette) / sizeof(tsmesh::kPalette[0]) == 32);
}
