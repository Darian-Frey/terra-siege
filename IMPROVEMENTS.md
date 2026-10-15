# Improvements

Catalogue of code-quality improvements, refactors and architectural changes proposed during development. Per Maintenance Rule 8 of the documentation standard, improvements are **logged here when noticed, not silently applied**. The author decides whether to apply, defer, or decline.

This is the dual of [BUGS.md](BUGS.md): bugs are broken; improvements work but could be better.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

The register was created on 2026-10-15 ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). Entries noted before that date were backfilled from commit messages, and their `Found` date is the commit date.

## Suggested

### IMP-001: Decide the fate of the disabled river code

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/world/Heightmap.cpp](src/world/Heightmap.cpp) `carveRivers()`, `flowDownhill()`, `carveChannel()`; `RIVER_*` constants in [src/core/Config.hpp](src/core/Config.hpp)
**Effort:** small
**Description.** River carving has been dead code since `26495c8` (2026-05-03). It still compiles, carries Config constants, and misled the docs ([BUG-003](BUGS.md#bug-003-documentation-claims-rivers-ship-but-river-carving-is-disabled)).
**Proposal.** Either re-enable rivers on the Perlin generator and tune them (delivers F-003), or delete the functions and `RIVER_*` constants and leave git history as the record.
**Trade-offs.** Deleting is small; re-enabling is medium effort. Deleting loses a working starting point if rivers come back. Re-enabling needs tuning against the new generator, which was why they were switched off. Leaving them as they are keeps paying the confusion cost.
**Notes.** Tied to F-003.

### IMP-002: Remove the dead EVADE fallback for retreat-capable enemies

**Status:** suggested
**Found:** 2026-06-03 (`c99848f`)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) Fighter / Bomber / Seeder AI
**Effort:** small
**Description.** Slice A added a Retreating state below 40% hull. The older EVADE path (below 25%) is now unreachable for those types, kept only "should the thresholds ever invert".
**Proposal.** Remove the EVADE branch for retreat-capable types, or add a `static_assert` that the retreat threshold is above the evade threshold.
**Trade-offs.** Removing it loses a fallback if thresholds are retuned. The `static_assert` keeps the code but makes the invariant explicit. Doing nothing leaves unreachable code that readers have to reason about.

### IMP-003: True proportional-navigation missile guidance

**Status:** suggested
**Found:** 2026-06-04 (`ec6d66a`)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) `updateProjectile()` missile steering
**Effort:** medium
**Description.** Missile steering is a direction blend, not real proportional navigation. Its turn radius made missiles orbit close targets, which the proximity fuse added in `ec6d66a` works around.
**Proposal.** Replace the blend with PN, using acceleration proportional to the line-of-sight rate, and keep the proximity fuse as a backstop.
**Trade-offs.** PN changes how missiles feel and would need the Cluster and Shield/Infectious missile tuning redone. The current behaviour works in play, so this is mostly about polish and realism.

### IMP-004: Retire Config.hpp per-entity fallbacks once sidecars are authoritative

**Status:** suggested
**Found:** 2026-06-06 (`814fa1e`)
**Location:** [src/core/Config.hpp](src/core/Config.hpp) per-entity constants; [src/mesh/EntityProfileRegistry.hpp](src/mesh/EntityProfileRegistry.hpp)
**Effort:** medium
**Description.** Every spawn now reads hull, shields, weapons, AI and FX from `*.meta.json` sidecars, but the matching `Config.hpp` constants (`FIGHTER_HP` and similar) remain as fallbacks for missing or malformed sidecars. The same value lives in two places and can drift.
**Proposal.** Make sidecars mandatory for shipped meshes, validate them in a test, and delete the per-entity Config constants. Keep physics, world and engine constants in Config ([D-011](DECISIONS.md#d-011-sidecar-entity-profiles-by-default-registry-override-later)).
**Trade-offs.** Without the fallback, a broken sidecar breaks that entity at runtime, so a validation test becomes essential. TTK-derived HP would need the TTK arithmetic moved into sidecar authoring or the inspector.
**Notes.** Planned as a follow-up in `814fa1e`.

### IMP-005: Consolidate weapon stats into a WeaponSpec table

**Status:** suggested
**Found:** 2026-06-22 (`d0cc533`)
**Location:** [src/core/Config.hpp](src/core/Config.hpp) weapon blocks; fire paths in [src/core/GameState.cpp](src/core/GameState.cpp)
**Effort:** medium
**Description.** Each weapon's damage, range, speed, cost and kind are separate constants, threaded by hand through long `spawnProjectile` calls.
**Proposal.** A `WeaponSpec` struct and a table indexed by weapon enum. Fire paths look up the spec, and sidecar weapon profiles could map onto the same struct.
**Trade-offs.** Touches every fire path, so there is regression risk across 10 weapons. The payoff is mainly maintainability; behaviour is unchanged.

### IMP-006: Laser pair polish: dedicated SFX and crosshair colour

**Status:** suggested
**Found:** 2026-06-22 (`d0cc533`)
**Location:** [src/audio/](src/audio/); flight HUD crosshair in [src/core/GameState.cpp](src/core/GameState.cpp)
**Effort:** small
**Description.** Rapid and charged laser shots reuse the CannonShot and PlasmaShot synth voices, and the crosshair doesn't show which colour is selected.
**Proposal.** Add red and blue laser synth voices, and tint the crosshair with the active laser colour.
**Trade-offs.** Small amount of extra synth tuning. A tinted crosshair may clash with the amber HUD palette.
**Notes.** Listed as deferred in `d0cc533`. Polish for F-010.

### IMP-007: Engine thrust loop SFX

**Status:** suggested
**Found:** 2026-06-07 (`cdaa52c`)
**Location:** [src/audio/](src/audio/)
**Effort:** small
**Description.** The procedural SFX system has no continuous engine sound, the most obvious missing event.
**Proposal.** A looped synth voice whose pitch and volume follow thrust, or a WAV loop from the rFXGen pipeline (`gen_sfx.sh`).
**Trade-offs.** Looping voices need different lifetime handling from the current one-shot alias pool. A WAV asset breaks the "no audio assets" property of the first cut.
**Notes.** Completes F-030.

### IMP-008: Seeder and Carrier infection

**Status:** suggested
**Found:** 2026-06-04 (`44b5d99`)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) infection paths
**Effort:** medium
**Description.** Only Fighters and Bombers can be infected. Seeder and Carrier orbit-and-drop AI "doesn't map cleanly" to attacking former allies.
**Proposal.** Define infected behaviours: an infected Seeder drops friendly drones; an infected Carrier deploys drones against its former side.
**Trade-offs.** An infected Carrier could trivialise the boss fight. This may be better left out, and revisited alongside infection v2 (F-025).

### IMP-009: Inspector Phase B: mirror, weld, split

**Status:** suggested
**Found:** 2026-06-07 (`92d0bbb`)
**Location:** [src/inspector/VertexTool.cpp](src/inspector/VertexTool.cpp)
**Effort:** medium
**Description.** Phase B shipped a first cut. Mirror editing, vertex weld and vertex split were deferred.
**Proposal.** Implement them per [project-status/terra_siege_inspect_roadmap.md](project-status/terra_siege_inspect_roadmap.md) Phase B.
**Trade-offs.** Weld and split change topology, so they need the full-rewrite save path (`saveObjMesh`) rather than the byte-exact vertex save.
**Notes.** Completes F-036.

### IMP-010: Shader-based damage flash for OBJ entities

**Status:** suggested
**Found:** 2026-06-02 (`e133893`)
**Location:** [src/mesh/MeshRegistry.cpp](src/mesh/MeshRegistry.cpp); `assets/shaders/`
**Effort:** medium
**Description.** Baked per-vertex palette colours ignore the `WHITE` tint, so OBJ entities don't flash when hit ([BUG-011](BUGS.md#bug-011-obj-rendered-entities-lose-the-white-damage-flash)).
**Proposal.** A minimal shader with a flash uniform that blends towards white. The same hook could drive state-driven colours (infection tint, Collector cabin pip) that are currently procedural.
**Trade-offs.** This would be the first shader in the render path, so it has to keep the flat-shaded look ([D-013](DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures)). It also adds a shader-load failure mode.

### IMP-011: Retreat to home base with heal on arrival

**Status:** suggested
**Found:** 2026-06-03 (`c99848f`)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) `tickRetreat()`
**Effort:** small
**Description.** Retreating enemies fly back to their spawn point and stay there. `spawnPos` was made uniform so Slice C could re-bind it to a home base.
**Proposal.** In Base mode, point `spawnPos` at the enemy's Lander and heal on arrival.
**Trade-offs.** Healing enemies lengthen fights. Needs tuning alongside C.8.
**Notes.** Pairs with F-022.

### IMP-012: Wire or remove `RADAR_VECTOR_LOOKAHEAD`

**Status:** suggested
**Found:** 2026-10-15 (radar review session)
**Location:** [src/core/Config.hpp](src/core/Config.hpp) `RADAR_VECTOR_LOOKAHEAD`
**Effort:** trivial
**Description.** The constant is defined but nothing reads it. Velocity arrows are scaled against `NEWTON_MAX_SPEED` instead.
**Proposal.** Delete the constant, or use it to scale arrow length as "position in N seconds".
**Trade-offs.** None material. Deleting is the smallest change.

### IMP-013: Billboard particles for missile trails

**Status:** suggested
**Found:** 2026-05-03 (`9660252`)
**Location:** [src/core/Particles.hpp](src/core/Particles.hpp)
**Effort:** small
**Description.** The particle system supports a Billboard render mode "reserved for future explosions / missile trails", but missiles have no trails.
**Proposal.** Emit short-lived billboard particles behind homing missiles.
**Trade-offs.** Adds load on the particle pool, which has 2000 slots and no heap growth. Cluster salvos could exhaust it, and trails must stay within the low-poly aesthetic.

### IMP-014: Delete or populate the empty stub modules

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** `src/ai/*`, `src/weapon/*`, `src/renderer/*`, `src/hud/{HUD,WeaponDisplay,ShieldDisplay}`, `src/shield/ShieldSystem`, `src/world/SkyDome`, `src/entity/{Enemy,Friendly,Projectile}`
**Effort:** small
**Description.** About 20 source files from the initial scaffold (`48d2f79`) are empty but still compiled. They suggest a module structure that doesn't exist ([BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist)), while `GameState.cpp` (about 3,100 lines) and `EntityManager.cpp` (about 3,400 lines) hold everything.
**Proposal.** Short term: delete the empty files and their CMake entries. Longer term, extract real modules as the files grow: weapon firing out of `GameState`, HUD drawing out of `GameState`, per-type AI out of `EntityManager`.
**Trade-offs.** Effort above is for deleting; extracting real modules is large. Deleting removes placeholders that hint at intended structure. Extracting is a large refactor with regression risk and no behaviour change, so it is best done when a slice is already touching that code.

### IMP-015: Spatial grid for entity queries

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) (drone boids around lines 1430–1435 and nearest-entity queries); `src/ai/SpatialGrid.{hpp,cpp}` (empty)
**Effort:** medium
**Description.** Drone flocking is O(N²) over the entity pool, and every nearest-target query is a linear scan. This is fine at today's counts (global drone cap 16, pool 256), but Slice C multiplies concurrent entities (Landers × drones, bases, defenders).
**Proposal.** A uniform 2D grid keyed on XZ, rebuilt once per tick from the fixed pools with no heap allocation. Cell size `SPATIAL_CELL_SIZE` (already in Config). The cell lookup must be wrap-aware ([BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap)).
**Trade-offs.** Extra code and a per-tick rebuild cost that only pays off at higher entity counts. Worth profiling first. Could be done together with the wrap-aware distance fix.
**Notes.** Mitigates [AV-011](ATTACK_VECTORS.md#av-011-quadratic-entity-queries-without-a-spatial-grid).

### IMP-016: Tests for the material and full-mesh OBJ save paths

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/mesh/ObjLoader.cpp](src/mesh/ObjLoader.cpp) `saveObjMaterials()`, `saveObjMesh()`; `tests/`
**Effort:** small
**Description.** `saveObjVertices` is covered by T-06/T-07, but `saveObjMaterials` (replaces the face section) and `saveObjMesh` (full rewrite) have no tests. A regression there silently corrupts authored meshes.
**Proposal.** Add `tests/test_obj_save_paths.cpp` with fixtures that round-trip a material repaint and a topology edit, checking that comments and `o`/`g` lines survive and faces reload identically.
**Trade-offs.** Small maintenance cost; fixtures must track any change to the save contract.
**Notes.** Covers [AV-009](ATTACK_VECTORS.md#av-009-obj-save-corrupts-authored-content).

### IMP-017: Continuous integration for build and tests

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** cross-cutting (no `.github/` directory)
**Effort:** small
**Description.** Tests run only when someone runs `ctest` by hand, so every "Implemented" detection in [ATTACK_VECTORS.md](ATTACK_VECTORS.md) depends on remembering to run them.
**Proposal.** A GitHub Actions workflow on Ubuntu: install the raylib build dependencies, configure, build Debug and DEV_MODE, then run `ctest`.
**Trade-offs.** First configure fetches raylib, doctest and picojson, so runs are slower without caching. The game itself can't be exercised headless.

### IMP-018: Refresh stale references in code comments

**Status:** suggested
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/core/Config.hpp](src/core/Config.hpp) (terrain section header "Fourier (sine) synthesis", `SINE_WARP_AMPLITUDE`, `terra_rebuild/` paths); [src/hud/Radar.hpp](src/hud/Radar.hpp); [src/entity/Entity.hpp](src/entity/Entity.hpp)
**Effort:** trivial
**Description.** Comments still point at `terra_rebuild/…` (now `project-status/archive/`), and the Config terrain header still says sine synthesis, though the generator is Perlin. Stale pointers like these are how the obsolete terrain spec almost got re-implemented.
**Proposal.** Update the paths and rename the section header. Leave `SINE_WARP_AMPLITUDE` unless it is renamed together with its uses.
**Trade-offs.** None material. Comment-only change.

## Applied

_None yet._

## Declined

_None._

## Deferred

_None._
