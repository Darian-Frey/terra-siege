#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "mesh/SidecarProfile.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using tsmesh::EntityProfile;
using tsmesh::loadProfile;
using tsmesh::saveProfile;
using tsmesh::sidecarPathFor;

namespace {

// Small helper — write a temp file, return the path. Lives in the
// system temp dir so each test runs in isolation.
std::filesystem::path writeTemp(const std::string &contents,
                                const std::string &suffix) {
  auto base = std::filesystem::temp_directory_path();
  static int counter = 0;
  ++counter;
  auto p = base /
           (std::string("sidecar_test_") + std::to_string(counter) + suffix);
  std::ofstream f(p, std::ios::out | std::ios::trunc);
  REQUIRE(f.is_open());
  f << contents;
  return p;
}

std::string readAll(const std::filesystem::path &p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

} // anonymous namespace

TEST_CASE("sidecarPathFor swaps .obj for .meta.json") {
  auto p =
      sidecarPathFor(std::filesystem::path("assets/meshes/fighter.obj"));
  CHECK(p.filename().string() == "fighter.meta.json");
}

TEST_CASE("missing sidecar → loaded=false, defaults stand") {
  EntityProfile prof;
  std::filesystem::path nowhere = std::filesystem::temp_directory_path() /
                                  "does_not_exist_xyz.meta.json";
  bool ok = loadProfile(nowhere, prof);
  CHECK_FALSE(ok);
  CHECK_FALSE(prof.loaded);
  CHECK(prof.view.scale == 1.0f);
  CHECK(prof.view.forward.z == 1.0f);
  // Missing files don't generate warnings — that's a normal state.
  CHECK(prof.warnings.empty());
}

TEST_CASE("malformed JSON → loaded=false + warning, doesn't crash") {
  auto path = writeTemp("{ this is not valid", ".meta.json");
  EntityProfile prof;
  bool ok = loadProfile(path, prof);
  CHECK_FALSE(ok);
  CHECK_FALSE(prof.loaded);
  CHECK(prof.warnings.size() >= 1);
  std::filesystem::remove(path);
}

TEST_CASE("typed extraction: forward / scale / pivot / identity") {
  auto path = writeTemp(R"({
    "identity": { "displayName": "TestShip", "class": "ship-flyer",
                  "faction": "enemy" },
    "forward": [0, 0, 1],
    "scale": 1.5,
    "pivot": [0.5, 0, -0.25]
  })",
                        ".meta.json");
  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  CHECK(prof.loaded);
  CHECK(prof.view.displayName == "TestShip");
  CHECK(prof.view.entityClass == "ship-flyer");
  CHECK(prof.view.faction == "enemy");
  CHECK(prof.view.scale == doctest::Approx(1.5f));
  CHECK(prof.view.pivot.x == doctest::Approx(0.5f));
  CHECK(prof.view.pivot.z == doctest::Approx(-0.25f));
  std::filesystem::remove(path);
}

TEST_CASE("hardpoints array parses into typed list") {
  auto path = writeTemp(R"({
    "hardpoints": [
      { "name": "nose", "pos": [0, 0, 1.2], "dir": [0, 0, 1],
        "weapon": "main-cannon", "fireArcDeg": 6 },
      { "name": "tail", "pos": [0, 0, -1.2], "dir": [0, 0, -1],
        "weapon": "rear-blaster", "fireArcDeg": 12 }
    ]
  })",
                        ".meta.json");
  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  REQUIRE(prof.view.hardpoints.size() == 2);
  CHECK(prof.view.hardpoints[0].name == "nose");
  CHECK(prof.view.hardpoints[0].pos.z == doctest::Approx(1.2f));
  CHECK(prof.view.hardpoints[0].fireArcDeg == doctest::Approx(6.0f));
  CHECK(prof.view.hardpoints[1].weapon == "rear-blaster");
  std::filesystem::remove(path);
}

TEST_CASE("save preserves unknown keys for forward-compat with F.2+") {
  // Sidecar with an F.4-ish "ai" section we don't fully type-extract
  // for editing yet. After load → save (with no F.1 edits), the ai
  // block must survive byte-for-byte (modulo whitespace).
  auto path = writeTemp(R"({
    "identity": { "displayName": "X", "class": "ship-flyer",
                  "faction": "enemy" },
    "ai": { "profile": "pursue-attack-evade", "detectionRange": 350,
            "attackRange": 180 },
    "forward": [0, 0, 1],
    "scale": 1.0,
    "pivot": [0, 0, 0],
    "extra_unknown_section": { "preserveMe": true,
                                "alsoMe": [1, 2, 3] }
  })",
                        ".meta.json");

  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  REQUIRE(prof.loaded);

  // Edit the pivot (F.1 mini-tool surface) and save.
  prof.view.pivot = {1.0f, 0.0f, -1.0f};
  REQUIRE(saveProfile(path, prof));

  std::string rewritten = readAll(path);
  // The unknown section must survive.
  CHECK(rewritten.find("extra_unknown_section") != std::string::npos);
  CHECK(rewritten.find("preserveMe") != std::string::npos);
  // The AI block we don't yet fully edit also survives.
  CHECK(rewritten.find("detectionRange") != std::string::npos);
  // And our edit applied.
  EntityProfile reread;
  REQUIRE(loadProfile(path, reread));
  CHECK(reread.view.pivot.x == doctest::Approx(1.0f));
  CHECK(reread.view.pivot.z == doctest::Approx(-1.0f));
  std::filesystem::remove(path);
}

TEST_CASE("save on missing file creates a minimal valid sidecar") {
  auto path = std::filesystem::temp_directory_path() /
              "fresh_sidecar.meta.json";
  std::filesystem::remove(path); // make sure it doesn't exist

  EntityProfile prof; // defaults — no DOM, no view edits
  prof.view.forward = {0, 0, 1};
  prof.view.scale = 2.0f;
  prof.view.pivot = {0, 0, 0};
  REQUIRE(saveProfile(path, prof));

  EntityProfile reread;
  REQUIRE(loadProfile(path, reread));
  CHECK(reread.view.scale == doctest::Approx(2.0f));
  std::filesystem::remove(path);
}

TEST_CASE("wrong-type fields warn but don't fail the load") {
  auto path = writeTemp(R"({
    "scale": "not a number",
    "forward": [0, 0, 1],
    "identity": { "displayName": 42 }
  })",
                        ".meta.json");
  EntityProfile prof;
  bool ok = loadProfile(path, prof);
  CHECK(ok);
  CHECK(prof.loaded);
  // Scale should stay at default (1.0) because the parse warned and
  // didn't overwrite.
  CHECK(prof.view.scale == doctest::Approx(1.0f));
  // displayName should stay empty because it was the wrong type.
  CHECK(prof.view.displayName.empty());
  CHECK(prof.warnings.size() >= 2);
  std::filesystem::remove(path);
}
