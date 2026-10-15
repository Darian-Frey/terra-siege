# Attack Vectors

Project-specific failure modes terra-siege must be resilient against.
Grouped by category. Each vector lists its detection method and severity.
Severity: Critical (must hold) | Major (regression on release blocks) | Minor (track only).

Created 2026-10-15 ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). Most entries come from the old CLAUDE.md "Critical Implementation Notes (Traps)" and from the archived rebuild docs' risk registers. **Automated tests cover only the mesh and sidecar layer** ([D-018](DECISIONS.md#d-018-doctest-tests-linked-only-to-the-mesh-library)) and there is no CI ([IMP-017](IMPROVEMENTS.md#imp-017-continuous-integration-for-build-and-tests)). Most vectors below therefore have detection "not implemented", recorded honestly as the standard requires.

## Process

### AV-001 Documentation drift misleads AI-assisted sessions

**Severity:** Major
**Description.** Most development happens with an AI partner that reads CLAUDE.md and the design docs first. Stale docs led directly to wrong beliefs: a spatial grid that doesn't exist, rivers, difficulty presets, a "never shipped" terrain rebuild, and wrong constants. An AI partner acting on these could re-implement obsolete specs, such as sine terrain, or "protect" invariants that aren't in the code.
**Detection.** Manual review: refresh CLAUDE.md at the end of every significant session (Maintenance Rules 2 and 7); compare docs with `git log` when returning after more than 2 weeks (Rule 1).
**Related decisions.** D-001, D-027
**History.** Found in the 2026-10-15 migration audit: [BUG-003](BUGS.md#bug-003-documentation-claims-rivers-ship-but-river-carving-is-disabled), [BUG-006](BUGS.md#bug-006-difficulty-presets-documented-as-shipped-but-not-implemented), [BUG-013](BUGS.md#bug-013-claudemd-key-constants-table-drifted-from-confighpp), [BUG-016](BUGS.md#bug-016-cluster-submunition-120-search-cone-documented-but-not-implemented), [BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist), [BUG-018](BUGS.md#bug-018-archived-design-docs-claim-the-terrain-rebuild-never-shipped).

### AV-002 Retired designs reintroduced

**Severity:** Minor
**Description.** Several retired designs are documented in the archive and could be brought back:

- **`FlightMode` / `CraftType`:** dual-physics complexity ([D-005](DECISIONS.md#d-005-single-newtonian-flight-model-one-craft)).
- **`diamondSquare()` / `applyRadialFalloff()`:** radial falloff breaks the seamless wrap.
- **`smooth()`:** erases high-frequency octave detail.
- **`terrain_rebuild.md` sine synthesis:** produces parallel ridge bands ([D-006](DECISIONS.md#d-006-tileable-perlin-gradient-fbm-terrain)).
- **Kamikaze drone contact damage** ([D-025](DECISIONS.md#d-025-drones-as-a-stand-off-gunship-swarm-with-a-global-cap)).
**Detection.** Manual review against DECISIONS.md before reviving any archived spec.
**Related decisions.** D-005, D-006, D-025

## World and wrap

### AV-003 Terrain loses tileability or sea-level meaning

**Severity:** Major
**Description.** Two invariants keep the terrain correct:

- **Tileability.** Every noise octave's lattice cell size must divide `HEIGHTMAP_SIZE − 1`, and `HEIGHTMAP_SIZE` must be 2ⁿ+1. Otherwise a visible seam appears at the world edge.
- **Sea level.** `FBM_SHAPE_EXPONENT` must stay symmetric around 0.5, and no post-pipeline normalise or contrast pass may be added. Otherwise `SEA_LEVEL` drifts: one past regression drowned 79% of the map.
**Detection.** Partly manual: the DEV_MODE F6 heightmap dump writes water-coverage stats and a PNG to `tests/logs/`. No static check on `FBM_LARGEST_CELL` against `HEIGHTMAP_SIZE`. Automated detection not implemented (would need a `static_assert`, or a headless generation test that checks seam continuity and the water fraction).
**Related decisions.** D-006

### AV-004 Camera clips below terrain

**Severity:** Major
**Description.** All five camera views must clamp the camera's desired position to at least `heightAt(x, z) + 3.5` and `playerY + 2.0` before lerping, or the camera clips underground when turning towards hills. The clamp lives in `GameState::applyTerrainClamp`. Its margins are inline literals (a Config violation).
**Detection.** Not implemented (visual only).
**Related decisions.** D-015

### AV-005 World-fixed objects drawn or measured without the toroidal wrap

**Severity:** Critical
**Description.** Player coordinates are never wrapped ([D-007](DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world)), so every call site that draws or measures a world-fixed object must use the nearest toroidal copy. One missed call site means objects vanish past the world edge, phantom terrain appears, or AI and radar behave wrongly. Negative inputs to `fmodf` must have `worldSize` added back.
**Detection.** Not implemented (would need a shared wrapped-delta helper plus unit tests on it). Manual check: fly more than about 4 km in one direction and confirm trees, entities and radar bearings.
**Related decisions.** D-007, D-028
**History.**

- Decorations and entities vanished past the edge; fixed 2026-10-15 (`7e846c3`).
- AI, collision and radar distances ignored the wrap: [BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap). Fixed 2026-10-15: the radar measures to the nearest copy, and `EntityManager::rebaseAround()` re-centres the simulation on the player every tick.

### AV-014 Loading progress callback lost on terrain regeneration

**Severity:** Minor
**Description.** `Planet::generate()` takes a `ProgressCb` that drives the loading screen. Terrain generation takes non-trivial time; if a regeneration path (reseed, new round) drops the callback, the window looks hung.
**Detection.** Not implemented (manual: F5 reseed in DEV_MODE should show progress).
**Related decisions.** D-006

## Combat balance

### AV-007 Cluster submunitions converge on one target

**Severity:** Major
**Description.** The Cluster Missile exists to spread damage across a swarm. If all four submunitions converge on one target, it becomes an expensive single-target missile and loses its identity. The original design held this with a 120° forward search cone that must never widen to a full sphere. In fact no cone is implemented: children reacquire across a full sphere within `MISSILE_REACQUIRE_RANGE`, and spreading comes from `CLUSTER_SPLIT_DISTANCE`.
**Detection.** Not implemented (would require logging target IDs per submunition in a scripted engagement).
**Related decisions.** D-014
**History.** Listed as a CLAUDE.md "trap" until 2026-10-15, when the audit found the cone had never been implemented ([BUG-016](BUGS.md#bug-016-cluster-submunition-120-search-cone-documented-but-not-implemented)).

### AV-008 Carrier shields regenerate under sustained Beam fire

**Severity:** Critical
**Description.** The Beam Laser must reset the target's `timeSinceHit` to 0 on every tick it is on target (through `applyDamage` / `applyShieldHit`). Otherwise Carrier sector shields regenerate during the fight and the Carrier becomes effectively unkillable. "The Beam Laser exists because of the Carrier."
**Detection.** Not implemented (manual: hold the Beam on one Carrier sector and confirm it never recharges).
**Related decisions.** D-014

### AV-010 Sidecar data silently overrides tuned values

**Severity:** Major
**Description.** `EntityProfileRegistry` values override `Config.hpp` on every spawn. A hand-edited or inspector-edited `*.meta.json` can change HP, shields or weapons with no compile-time trace, bypassing TTK derivation ([D-014](DECISIONS.md#d-014-ttk-derived-enemy-hp)). Schema validation is limited to range checks, with warnings printed to stderr.
**Detection.** Partly implemented: `tests/test_sidecar_profile.cpp` covers parse robustness (missing or malformed files, unknown keys preserved, type warnings), but not value sanity against TTK targets.
**Related decisions.** D-011, D-014

### AV-015 Infection invariants broken

**Severity:** Major
**Description.** These must hold:

- Ships can be infected at most once (`canBeInfected = false` after a flip).
- The player is never infectable.
- The Infectious Missile is a dud against targets whose shields are up.
- Auto-aim and homing weapons drop infected targets.

Breaking any of them causes faction ping-pong, missiles chasing allies, or a broken theme. Infection v2 (F-025) will change some of these rules deliberately; it needs a new decision first.
**Detection.** Not implemented.
**Related decisions.** D-023, D-020
**History.** Known gap: in-flight projectiles can still hit newly infected ships ([BUG-010](BUGS.md#bug-010-in-flight-auto-aimed-projectiles-can-still-hit-newly-infected-ships)).

### AV-016 Premature Base-mode victory once infection v2 lands

**Severity:** Major
**Description.** The Victory gate requires "no infected bases". This is trivially true until C.6 adds base infection. If C.6 lands without updating the win/loss latch, the game could declare Victory while friendly bases are still infected.
**Detection.** Not implemented. Must be checked manually when F-025 lands.
**Related decisions.** D-020

## Engine and performance

### AV-006 Heap allocation in the hot path

**Severity:** Major
**Description.** No `new` or `malloc` (or growing containers) inside `update()` or `render()`. All of these are fixed and pre-allocated:

- entity pool: 256
- projectile pool: 512
- particle pool: 2000
- radar ghost-blip pool: 32
- radar echo state, one slot per entity

Allocation causes frame-time spikes. When a pool is full, the oldest slot is recycled, which can silently evict a live entity.
**Detection.** Not implemented (would need ASan/heaptrack with an allocation hook on the tick, or a custom allocator counter in DEV_MODE). Note: `Radar::m_contacts`, `m_prevContacts` and `m_threats` are `std::vector`s cleared and refilled each tick. They stop allocating once their capacity is reached, but have no up-front `reserve`.
**Related decisions.** D-012, D-026

### AV-011 Quadratic entity queries without a spatial grid

**Severity:** Major
**Description.** Drone flocking is O(N²) over the entity pool, and nearest-target queries are linear scans. There is no spatial grid. This is fine at today's counts because `DRONE_GLOBAL_CAP` is 16, but Slice C raises concurrent entities (Landers × drones, bases, defenders) and could hit a performance cliff.
**Detection.** Not implemented (would need a DEV_MODE per-tick timing overlay, or a stress scenario with a frame-time budget).
**Related decisions.** D-012, D-025
**History.** CLAUDE.md claimed a spatial grid until 2026-10-15 ([BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist)). Fix candidate: [IMP-015](IMPROVEMENTS.md#imp-015-spatial-grid-for-entity-queries).

### AV-012 Frame-rate-dependent logic in update()

**Severity:** Major
**Description.** Physics must step at exactly `FIXED_DT` (1/120 s) with the frame time capped at `MAX_FRAME_TIME` (0.05 s). `update()` must make no raylib draw calls and `render()` must mutate no game state. Violations make behaviour vary with display rate and break determinism (and future replay, F-046). Edge-triggered input must use the custom rising-edge helper, because raylib's `IsKeyPressed` is per-frame and fires on several ticks within one frame.
**Detection.** Not implemented. The DEV_MODE F4 flight recorder (120 Hz CSV to `tests/logs/`) is a manual aid for comparing runs.
**Related decisions.** D-003

### AV-013 raylib name and default collisions

**Severity:** Major
**Description.** raylib carries traps:

- **`CameraMode` typedef.** Never name a type `CameraMode`. `CameraView` (player views) and `CamMode` (DEV Follow/FreeRoam) are separate enums.
- **Default 1000-unit far plane.** `GameState::render()` overrides it with `rlFrustum` to 3000 (three call sites). Removing it clips the roughly 8 km world.
- **Tactical view up vector.** The view needs `camera.up = {0, 0, 1}`. With `{0, 1, 0}` the look-at is degenerate and the camera spins.
- **`RAYMATH_IMPLEMENTATION`.** It must be defined only on the executables, never on `terra_siege_mesh`, to avoid duplicate symbols.
- **`MatrixMultiply(A, B)` order.** It computes `B·A`. Together with the render/physics pitch sign (`MatrixRotateX(-m_pitch)`), this has inverted the ship visual against its thrust vector "3+ times".
**Detection.** Partial: the compiler catches `CameraMode`; the linker catches `RAYMATH_IMPLEMENTATION`. The others are not implemented (visual only).
**Related decisions.** D-004, D-015

## Asset pipeline

### AV-009 OBJ save corrupts authored content

**Severity:** Major
**Description.** Inspector saves must not lose content outside their scope:

- `saveObjVertices` rewrites only `v` lines, de-duplicates its edit marker, and writes nothing when there are no edits.
- `saveObjMaterials` replaces only the face section.
- `saveObjMesh` is a full rewrite that deliberately drops `vn`/`vt`.

A broken save path silently corrupts hand-authored meshes or breaks the Blender round-trip. Separately, the loader must recompute face normals and ignore `vn`, and a malformed material name must warn and render magenta, never crash.
**Detection.** Implemented for the vertex path and loader: `tests/test_obj_save.cpp` (T-06, T-07, marker de-duplication, refusal on vertex-count mismatch) and `tests/test_obj_loader.cpp` (T-01..T-05, palette parser). Not implemented for `saveObjMaterials` / `saveObjMesh` ([IMP-016](IMPROVEMENTS.md#imp-016-tests-for-the-material-and-full-mesh-obj-save-paths)). No fixture has `vn` lines, so "ignore vn" itself isn't asserted.
**Related decisions.** D-009, D-010, D-017
