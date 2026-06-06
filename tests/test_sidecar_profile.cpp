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

TEST_CASE("hull section parses + present flag flips on load") {
  auto path = writeTemp(R"({
    "hull": { "hp": 160, "collisionRadius": 1.6, "mass": 1.0,
              "wreckage": { "metal": 30, "bio": 0 } }
  })",
                        ".meta.json");
  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  CHECK(prof.view.hullPresent);
  CHECK(prof.view.hullHP == doctest::Approx(160.0f));
  CHECK(prof.view.hullCollisionRadius == doctest::Approx(1.6f));
  CHECK(prof.view.hullMass == doctest::Approx(1.0f));
  CHECK(prof.view.hullWreckageMetal == doctest::Approx(30.0f));
  CHECK(prof.view.hullWreckageBio == doctest::Approx(0.0f));
  std::filesystem::remove(path);
}

TEST_CASE("shields section parses (omni + 4-sector)") {
  auto pathA = writeTemp(R"({
    "shields": { "model": "omni", "hp": 40, "regen": 20, "delay": 4.0 }
  })",
                         ".meta.json");
  EntityProfile a;
  REQUIRE(loadProfile(pathA, a));
  CHECK(a.view.shieldsPresent);
  CHECK(a.view.shieldModel == "omni");
  CHECK(a.view.shieldHP == doctest::Approx(40.0f));
  std::filesystem::remove(pathA);

  auto pathB = writeTemp(R"({
    "shields": { "model": "4-sector", "hp": 250, "regen": 80, "delay": 2.0 }
  })",
                         ".meta.json");
  EntityProfile b;
  REQUIRE(loadProfile(pathB, b));
  CHECK(b.view.shieldModel == "4-sector");
  CHECK(b.view.shieldHP == doctest::Approx(250.0f));
  std::filesystem::remove(pathB);
}

TEST_CASE("F.2 round-trip: hull + shields edits survive save+reload") {
  auto path = writeTemp(R"({
    "identity": { "displayName": "T", "class": "ship-flyer",
                  "faction": "enemy" },
    "hull": { "hp": 160, "collisionRadius": 1.6, "mass": 1.0 },
    "shields": { "model": "omni", "hp": 40, "regen": 20, "delay": 4 },
    "ai": { "profile": "pursue-attack-evade", "detectionRange": 350 }
  })",
                        ".meta.json");

  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));

  // Hull-tool style edit.
  prof.view.hullHP = 200.0f;
  prof.view.hullCollisionRadius = 1.8f;
  // Shields-tool style edit — switch model + tune HP.
  prof.view.shieldModel = "4-sector";
  prof.view.shieldHP = 60.0f;
  REQUIRE(saveProfile(path, prof));

  EntityProfile reread;
  REQUIRE(loadProfile(path, reread));
  CHECK(reread.view.hullHP == doctest::Approx(200.0f));
  CHECK(reread.view.hullCollisionRadius == doctest::Approx(1.8f));
  CHECK(reread.view.shieldModel == "4-sector");
  CHECK(reread.view.shieldHP == doctest::Approx(60.0f));
  // Untouched ai block must survive the save.
  std::string text = readAll(path);
  CHECK(text.find("pursue-attack-evade") != std::string::npos);
  CHECK(text.find("detectionRange") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("save with hullsPresent=false leaves no hull block") {
  // Brand-new file, no DOM. View defaults — hullPresent stays false.
  auto path = std::filesystem::temp_directory_path() /
              "no_hull_sidecar.meta.json";
  std::filesystem::remove(path);

  EntityProfile prof;
  REQUIRE(saveProfile(path, prof));
  std::string raw = readAll(path);
  // forward/scale/pivot always written; hull should NOT appear.
  CHECK(raw.find("\"forward\"") != std::string::npos);
  CHECK(raw.find("\"hull\"") == std::string::npos);
  CHECK(raw.find("\"shields\"") == std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("F.3 weapons round-trip through load + save") {
  auto path = writeTemp(R"({
    "weapons": [
      { "name": "main-cannon", "type": "cannon",
        "fireRate": 0.20, "damage": 8, "projSpeed": 200, "range": 200 },
      { "name": "missile-pod", "type": "missile",
        "fireRate": 1.50, "damage": 60, "projSpeed": 75, "range": 600,
        "ammo": 12 }
    ]
  })",
                        ".meta.json");
  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  REQUIRE(prof.view.weapons.size() == 2);
  CHECK(prof.view.weapons[0].name == "main-cannon");
  CHECK(prof.view.weapons[0].fireRate == doctest::Approx(0.20f));
  CHECK(prof.view.weapons[1].ammo == 12);

  // Edit + save (Weapons-tool path: tweak fireRate, add a new one)
  prof.view.weapons[0].damage = 12.0f;
  tsmesh::ProfileView::Weapon w3;
  w3.name = "shield-laser";
  w3.type = "beam";
  w3.fireRate = 0.0f;
  w3.damage = 60.0f;
  w3.projSpeed = 0.0f;
  w3.range = 160.0f;
  prof.view.weapons.push_back(w3);
  REQUIRE(saveProfile(path, prof));

  EntityProfile reread;
  REQUIRE(loadProfile(path, reread));
  REQUIRE(reread.view.weapons.size() == 3);
  CHECK(reread.view.weapons[0].damage == doctest::Approx(12.0f));
  CHECK(reread.view.weapons[2].name == "shield-laser");
  CHECK(reread.view.weapons[2].type == "beam");
  std::filesystem::remove(path);
}

TEST_CASE("F.3 hardpoints add + delete round-trip") {
  auto path = writeTemp(R"({
    "hardpoints": [
      { "name": "nose", "pos": [0, 0, 1.2], "dir": [0, 0, 1],
        "weapon": "main-cannon", "fireArcDeg": 6 }
    ],
    "ai": { "detectionRange": 350 }
  })",
                        ".meta.json");
  EntityProfile prof;
  REQUIRE(loadProfile(path, prof));
  REQUIRE(prof.view.hardpoints.size() == 1);

  // Hardpoints-tool "add" — push a new mount.
  tsmesh::ProfileView::Hardpoint hp;
  hp.name = "port-wing";
  hp.pos = {-0.8f, 0.0f, 0.4f};
  hp.dir = {0.0f, 0.0f, 1.0f};
  hp.fireArcDeg = 12.0f;
  hp.weapon = "main-cannon";
  prof.view.hardpoints.push_back(hp);
  REQUIRE(saveProfile(path, prof));

  EntityProfile mid;
  REQUIRE(loadProfile(path, mid));
  REQUIRE(mid.view.hardpoints.size() == 2);
  CHECK(mid.view.hardpoints[1].name == "port-wing");
  CHECK(mid.view.hardpoints[1].fireArcDeg == doctest::Approx(12.0f));

  // Hardpoints-tool "delete" — remove the first one.
  mid.view.hardpoints.erase(mid.view.hardpoints.begin());
  REQUIRE(saveProfile(path, mid));

  EntityProfile reread;
  REQUIRE(loadProfile(path, reread));
  REQUIRE(reread.view.hardpoints.size() == 1);
  CHECK(reread.view.hardpoints[0].name == "port-wing");

  // The ai block must STILL survive both round-trips even though F.3
  // doesn't edit it — the unknown-key preservation test extended.
  std::string raw = readAll(path);
  CHECK(raw.find("detectionRange") != std::string::npos);
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
