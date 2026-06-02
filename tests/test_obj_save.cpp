// T-06 / T-07 from 3d_assets.md §10 — verify the inspector's
// save-back path preserves everything except the `v ` lines it's
// explicitly allowed to rewrite.
//
// T-06: save with no edits → file not written, on-disk bytes
//       byte-identical to original.
// T-07: save with one vertex moved → only that `v` line differs;
//       all other lines (comments, blank, vn/vt/f/o/g/usemtl)
//       byte-identical; edit-marker comment appended at end.
//
// We deliberately don't include T-08 (Blender → game → inspector →
// Blender round-trip with 1e-5 tolerance) because that's an
// integration test that needs Blender. T-06 + T-07 together cover
// the same correctness properties at unit-test scale.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "mesh/ObjLoader.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Read a whole file as a single byte-blob — used for T-06's "files
// are identical" check, where we want any whitespace / newline /
// trailing-byte difference to fail loudly.
std::string slurp(const std::filesystem::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeFile(const std::filesystem::path &p, const std::string &content) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(content.data(),
            static_cast<std::streamsize>(content.size()));
}

// Get a unique temp file path under the system's tmp dir so tests
// can run in parallel without colliding.
std::filesystem::path tmpFile(const char *stem) {
  static int counter = 0;
  std::filesystem::path p = std::filesystem::temp_directory_path() /
                            ("tsmesh_" + std::string(stem) + "_" +
                             std::to_string(++counter) + ".obj");
  return p;
}

// Build a small fixture OBJ representing the canonical cube. Includes
// a mix of directives so we can verify they're preserved verbatim.
std::string canonicalCubeObj() {
  return
      "# test cube fixture — written by test_obj_save\n"
      "o cube\n"
      "\n"
      "v -1.000000 -1.000000 -1.000000\n"
      "v 1.000000 -1.000000 -1.000000\n"
      "v 1.000000 1.000000 -1.000000\n"
      "v -1.000000 1.000000 -1.000000\n"
      "v -1.000000 -1.000000 1.000000\n"
      "v 1.000000 -1.000000 1.000000\n"
      "v 1.000000 1.000000 1.000000\n"
      "v -1.000000 1.000000 1.000000\n"
      "\n"
      "# faces\n"
      "usemtl c01\n"
      "f 1 3 2\n"
      "f 1 4 3\n"
      "f 5 6 7\n"
      "f 5 7 8\n";
}

} // anonymous namespace

// ---- T-06: save with no edits → file not written ----
TEST_CASE("T-06: saveObjVertices with isDirty=false is a no-op") {
  std::filesystem::path p = tmpFile("t06");
  std::string original = canonicalCubeObj();
  writeFile(p, original);

  // Vertex list matches the file's count but we pass isDirty=false
  // so saveObjVertices must NOT write to the file.
  std::vector<Vector3> verts = {
      {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
      {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
  };
  bool ok = tsmesh::saveObjVertices(p, verts, /*isDirty=*/false);
  CHECK(ok); // no-op counts as success

  // Bytes must be exactly what we wrote.
  std::string after = slurp(p);
  CHECK(after == original);

  std::filesystem::remove(p);
}

// ---- T-07: vertex edit preserves everything else ----
TEST_CASE("T-07: saveObjVertices rewrites only `v` lines + marker") {
  std::filesystem::path p = tmpFile("t07");
  std::string original = canonicalCubeObj();
  writeFile(p, original);

  // Move v1 from (-1,-1,-1) to (-1.5, -1, -1). Other 7 vertices
  // unchanged in the list passed to save.
  std::vector<Vector3> verts = {
      {-1.5f, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
      {-1, -1, 1},     {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
  };
  REQUIRE(tsmesh::saveObjVertices(p, verts, /*isDirty=*/true));

  // Reload + compare line-by-line.
  std::vector<std::string> after = tsmesh::readObjLines(p);
  std::vector<std::string> before;
  {
    std::istringstream iss(original);
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      before.push_back(line);
    }
  }

  // The save appends one edit-marker line. Every other line is
  // byte-identical to the source EXCEPT the first `v ` line.
  REQUIRE(after.size() == before.size() + 1);

  size_t differing = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (before[i] != after[i]) {
      ++differing;
      // The only allowed diff is the v1 line. Verify it's the
      // rewritten value, not arbitrary corruption.
      CHECK(before[i].substr(0, 2) == "v ");
      CHECK(after[i] == "v -1.500000 -1.000000 -1.000000");
    }
  }
  CHECK(differing == 1);

  // Last line is the edit marker.
  const std::string &last = after.back();
  CHECK(last.rfind("# edited by terra-siege inspector", 0) == 0);

  std::filesystem::remove(p);
}

// ---- Bonus: re-saving an already-edited file UPDATES the marker
// in place rather than appending a second one ----
TEST_CASE("save updates the edit-marker comment in place") {
  std::filesystem::path p = tmpFile("marker");
  std::string original = canonicalCubeObj() +
                         "# edited by terra-siege inspector 2020-01-01 00:00:00\n";
  writeFile(p, original);

  std::vector<Vector3> verts = {
      {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
      {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
  };
  REQUIRE(tsmesh::saveObjVertices(p, verts, /*isDirty=*/true));

  std::vector<std::string> after = tsmesh::readObjLines(p);
  // Exactly one edit-marker line — no duplicate.
  int markerCount = 0;
  for (const std::string &l : after) {
    if (l.rfind("# edited by terra-siege inspector", 0) == 0)
      ++markerCount;
  }
  CHECK(markerCount == 1);
  // And it's not the old timestamp anymore.
  for (const std::string &l : after) {
    if (l.rfind("# edited by terra-siege inspector", 0) == 0) {
      CHECK(l.find("2020-01-01") == std::string::npos);
    }
  }

  std::filesystem::remove(p);
}

// ---- Refuse to save if vertex counts mismatch (defensive) ----
TEST_CASE("saveObjVertices refuses on vertex-count mismatch") {
  std::filesystem::path p = tmpFile("mismatch");
  writeFile(p, canonicalCubeObj());

  // Only 3 vertices passed but the file has 8.
  std::vector<Vector3> verts = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}};
  CHECK_FALSE(tsmesh::saveObjVertices(p, verts, /*isDirty=*/true));

  std::filesystem::remove(p);
}
