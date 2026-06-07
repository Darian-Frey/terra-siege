#pragma once

#include "raylib.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ====================================================================
// Entity-sidecar profile (Inspector roadmap Phase F).
//
// Each `assets/meshes/foo.obj` may have a parallel `foo.meta.json`
// carrying terra-siege-specific data the OBJ format can't represent:
// identity, hull/shield HP, weapons, hardpoints, AI profile, FX, etc.
// Missing sidecar is fine — defaults apply.
//
// Phase F.1 scope:
//   * Load — parse JSON into a preserved DOM + extract the small
//     typed fields F.1 understands (forward / scale / pivot).
//   * Save — re-emit the DOM (with any F.1 edits merged in) so future
//     F.2/F.3 sections we don't yet typed-extract still round-trip.
//   * Validate — collect warnings (unknown keys, bad ranges, parse
//     errors with line/col where the parser supports it) but never
//     refuse a load: the user wants to see what's there.
//
// Future F.x phases extend the typed `EntityProfile` struct with
// new sections (identity, hull, shields, weapons, ...) and add
// corresponding extract+merge code. Anything not yet typed-extracted
// still survives a round-trip thanks to the preserved DOM.
// ====================================================================
namespace tsmesh {

// Read-only viewer convenience — typed snapshot of fields F.1+F.2
// care about for the 3D overlay and the per-tool forms. Each
// section is populated only if it exists in the JSON; otherwise
// the fields stay at their defaults. `present` flags let the
// renderer skip drawing overlays for missing sections rather than
// drawing them at default values.
struct ProfileView {
  // ---- identity ----
  // entityClass picks from a small enum: "static" / "ground" / "hover"
  // / "flyer" / "projectile". Free-form for now; the IdentityTool
  // restricts the picker to that set.
  std::string displayName;
  std::string entityClass;
  std::string faction;

  // ---- transform ----
  Vector3 forward = {0, 0, 1}; // unit vector in entity-local space
  float scale = 1.0f;          // uniform scale applied at render
  Vector3 pivot = {0, 0, 0};   // entity origin offset

  // ---- hull ----
  // hullPresent flips true when any hull.* key exists so the viewer
  // can draw the collision sphere without showing default-radius
  // bubbles around hull-less entities.
  bool hullPresent = false;
  float hullHP = 100.0f;
  float hullCollisionRadius = 1.0f;
  float hullMass = 1.0f;
  float hullWreckageMetal = 0.0f;
  float hullWreckageBio = 0.0f;

  // ---- shields ----
  // shieldsPresent flips on any shields.* key. shieldModel chooses
  // the gameplay routing AND the viz: "omni" = single bubble,
  // "4-sector" = front/rear/right/left pies, "per-face" = baked
  // face groups (not yet wired into the game — F.x leftover).
  bool shieldsPresent = false;
  std::string shieldModel = "omni";
  float shieldHP = 0.0f;
  float shieldRegen = 0.0f;
  float shieldDelay = 0.0f;

  // ---- AI ----
  // aiProfile names a behavior preset the game's AI dispatch can
  // switch on (pursue-attack-evade / kamikaze / strafe-friendlies /
  // boids-swarm / stationary-turret / drift-deploy / boss-orbit /
  // harvest-loop / none). Currently informational — game-side
  // dispatch is still per-entity-type hardcoded; future Slice C
  // work makes this a real switch.
  // Ranges are honoured at runtime once the entity has cached them.
  bool aiPresent = false;
  std::string aiProfile;
  std::string targetPref; // player / friendlies / bases / nearest
  float detectionRange = 0.0f;
  float attackRange = 0.0f;
  float evadeAtHPFrac = 0.0f;
  float retreatAtHPFrac = 0.0f;

  // ---- Infection (Slice B.4) ----
  // canBeInfected gates the flip; rebootDuration is the reboot delay
  // before an infected ship re-enters combat under the player's
  // colors; speedPenaltyAfter scales its top speed once active.
  bool infectionPresent = false;
  bool canBeInfected = false;
  float rebootDuration = 3.0f;
  float speedPenaltyAfter = 0.80f;

  // ---- FX ----
  // smokeAtHPFrac is the hull fraction below which damage smoke
  // emits. deathExplosionScale multiplies the death-burst radius.
  // engineGlow is an RGB tint applied to the engine emitter colour
  // (0..255 each; not yet wired into the runtime emitter pool —
  // editable in the inspector, ignored by the game until a follow-up).
  bool fxPresent = false;
  float smokeAtHPFrac = 0.0f;
  float deathExplosionScale = 1.0f;
  unsigned char engineGlowR = 80;
  unsigned char engineGlowG = 180;
  unsigned char engineGlowB = 220;

  // ---- Weapons ----
  // Named stat blocks referenced by hardpoint.weapon. F.3 edits the
  // list directly; the runtime weapon system doesn't read these yet
  // (Slice B weapons still come from Config::*) — wiring lives behind
  // a future migration, same as F.2's hull/shield port.
  struct Weapon {
    std::string name;       // primary key — hardpoint.weapon references this
    std::string type;       // "cannon" / "plasma" / "beam" / "missile" / ...
    float fireRate = 0.20f; // seconds between shots
    float damage = 5.0f;
    float projSpeed = 90.0f;
    float range = 80.0f;
    int   ammo = -1;        // -1 = unlimited
    std::string cooldownGroup; // optional — empty = own cooldown
  };
  std::vector<Weapon> weapons;

  // ---- Hardpoints ----
  struct Hardpoint {
    std::string name;
    Vector3 pos = {0, 0, 0};
    Vector3 dir = {0, 0, 1};
    float fireArcDeg = 0.0f;
    std::string weapon;
  };
  std::vector<Hardpoint> hardpoints;
};

// Opaque holder for the preserved JSON DOM. Hides picojson behind a
// pimpl so consumers don't transitively pay its include cost.
struct ProfileDom;
struct ProfileDomDeleter { void operator()(ProfileDom *p) const noexcept; };
using ProfileDomPtr = std::unique_ptr<ProfileDom, ProfileDomDeleter>;

struct EntityProfile {
  // True iff a sidecar file existed and parsed without fatal errors.
  // False either because no file was present or because parsing failed
  // hard (look at `warnings` for the reason). When false, `view` and
  // `dom` are at their defaults.
  bool loaded = false;

  // Soft-error log — bad fields, unknown keys we want to surface to
  // the user, parser nits with line numbers when available.
  std::vector<std::string> warnings;

  // Typed view for the read-only viewer overlay + F.1 form fields.
  ProfileView view;

  // Preserved DOM. Holds the full parsed JSON tree (or a fresh empty
  // object if no file existed). saveProfile() walks this — merging
  // any view-side edits in — so unknown sections survive round-trip.
  ProfileDomPtr dom;
};

// Sidecar path next to an OBJ. `foo.obj` → `foo.meta.json`. Returned
// path is NOT checked for existence; missing is a valid state.
std::filesystem::path sidecarPathFor(const std::filesystem::path &obj);

// Load a sidecar from disk into `out`. Returns true if the file was
// found AND parsed successfully (out.loaded == true). Returns false
// otherwise — caller can still read `out` to see the defaults + any
// warnings about why the parse failed.
bool loadProfile(const std::filesystem::path &path, EntityProfile &out);

// Merge `view`'s F.1 fields (forward / scale / pivot) back into the
// preserved DOM and write the result to `path`. Creates the file if
// it doesn't exist (with a brand-new minimal object containing only
// the F.1 fields). Returns true on success.
bool saveProfile(const std::filesystem::path &path,
                 const EntityProfile &profile);

} // namespace tsmesh
