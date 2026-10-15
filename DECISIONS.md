# Decisions

Append-only log of significant design decisions.
Each entry: D-NNN, with Decided and Recorded dates (ISO 8601; equal for normal entries, divergent for retroactive ones), status, context, alternatives, decision, consequences, and reversal conditions.
Status vocabulary: Proposed | Accepted | Superseded by D-NNN | Deprecated.

**Backfill note.** This log was created on 2026-10-15 ([D-001](#d-001-adopt-the-development-documentation-standard)). Entries D-003 to D-029 record decisions made earlier. Their `Decided` dates come from the cited commit or design document; `Recorded` is 2026-10-15. Where no date can be recovered, `Decided` says so. Provisional IDs D-A01..D-A08 from [project-status/3d_assets.md](project-status/3d_assets.md) §13 are renumbered here:

| Provisional | Now | Status |
| --- | --- | --- |
| D-A01 OBJ is the single source of truth | [D-009](#d-009-obj-text-format-as-the-single-mesh-source-of-truth) | Accepted |
| D-A02 Normals recomputed at load | [D-009](#d-009-obj-text-format-as-the-single-mesh-source-of-truth) | Accepted |
| D-A03 MTL files git-ignored | [D-010](#d-010-32-colour-palette-via-obj-material-names) | Accepted (no `.gitignore` rule yet) |
| D-A04 Palette index in material name | [D-010](#d-010-32-colour-palette-via-obj-material-names) | Accepted, amended to 32 colours |
| D-A05 Inspector is a CLI mode of the game | [D-016](#d-016-inspector-as-a-separate-binary-with-a-tool-registry) | Reversed |
| D-A06 Inspector edits vertex positions only | [D-017](#d-017-inspector-scope-widened-to-materials-topology-and-primitives) | Reversed |
| D-A07 Animation deferred to IQM | [D-024](#d-024-animation-deferred-to-iqm-obj-stays-static) | Accepted |
| D-A08 In-house OBJ parser | [D-009](#d-009-obj-text-format-as-the-single-mesh-source-of-truth) | Accepted |

---

### D-001 Adopt the development documentation standard

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** D-002

**Context.** Project docs had drifted badly from the code. The README still said "Phase 3 in progress". CLAUDE.md quoted six wrong constants, a spatial grid that doesn't exist, rivers that are disabled and difficulty presets that were never built ([BUG-003](BUGS.md#bug-003-documentation-claims-rivers-ship-but-river-carving-is-disabled), [BUG-006](BUGS.md#bug-006-difficulty-presets-documented-as-shipped-but-not-implemented), [BUG-013](BUGS.md#bug-013-claudemd-key-constants-table-drifted-from-confighpp), [BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist)). Because the project is developed mostly with an AI partner, stale docs actively mislead.

**Options.**

- **A. Patch the existing README / CLAUDE.md / ROADMAP in place.** Cheapest. Rejected: it doesn't add stable IDs or a bug/decision trail, so the same drift would recur.
- **B. Adopt the author's development documentation standard** (`project-scaffold/development_documentation.md`, revision 2026-05-21). Chosen.

**Decision.** Option B, using the retroactive path for an Active project:

- README status header added.
- New Tier 1 docs: FEATURES (F-001..F-046), CHANGELOG, LICENSE (MIT).
- ROADMAP and CLAUDE.md rewritten to the standard's shape.
- Tier 2/3 docs added: DECISIONS, ARCHITECTURE, ATTACK_VECTORS, BUGS, IMPROVEMENTS.
- All IDs assigned retroactively from `git log`.

**Consequences.**

- Maintenance Rule 8 now applies: bugs and improvement candidates are logged in BUGS.md / IMPROVEMENTS.md when found, not fixed silently.
- Docs are part of the commit (Maintenance Rule 7). A code change that invalidates a doc must update it.
- CLAUDE.md no longer duplicates constant values; it points at `Config.hpp` (one source of truth per fact).

**Reversal conditions.** Revisit if the documentation overhead consistently exceeds its value for a solo project, for example if registers go unmaintained for more than 2 months while code changes continue.

### D-002 Exempt BUILD.md and SPEC.md; keep design references in project-status/

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** D-001, D-027

**Context.** The standard lists BUILD.md and SPEC.md as Tier 2 (strongly recommended), and lets projects add their own document types if they are recorded.

**Options.**

- **A. Write BUILD.md and SPEC.md.** Rejected for now. The build is three CMake commands plus a package list, which fits in the README. The authoritative technical spec is `src/core/Config.hpp` plus the design docs; copying constants into prose is exactly the drift that caused [BUG-013](BUGS.md#bug-013-claudemd-key-constants-table-drifted-from-confighpp).
- **B. Exempt both, and record where their content lives.** Chosen.

**Decision.**

- Build instructions live in README.md §Build requirements and CLAUDE.md §Build & test commands.
- The technical specification is `Config.hpp` ([D-027](#d-027-confighpp-as-the-single-source-of-tuning-truth-transitional)) together with these design references, kept in `project-status/` as a project-specific extension:
  - `base_mode_v2.md` (Slice C design)
  - `terra_siege_inspect_roadmap.md` (inspector phases)
  - `3d_assets.md` (asset pipeline spec)
  - `game_modes_and_features.md` (v1 game-mode design; §§4–9 still apply)
- Superseded documents stay in `project-status/archive/`.

**Consequences.** The repo root holds only standard documents. Design references keep their own status headers, and are cited from FEATURES, ROADMAP and DECISIONS by path.

**Reversal conditions.** Add BUILD.md when a second platform (Windows) or non-trivial toolchain setup arrives. Add SPEC.md if a formal format or protocol emerges that isn't expressible as Config constants, such as a save-game or replay format.

### D-003 Fixed 120 Hz physics timestep

**Decided:** 2026-03-31 (from commit `c746571`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-006, F-046, [AV-012](ATTACK_VECTORS.md#av-012-frame-rate-dependent-logic-in-update)

**Context.** Hovercraft physics with aggressive inputs near terrain needs stable integration and the option of deterministic replay. Motion has to be smooth on 60 Hz and 144 Hz displays alike.

**Options.**

- **A. Variable timestep tied to frame rate.** Rejected: non-deterministic, and unstable at low frame rates.
- **B. Fixed 60 Hz with interpolated rendering.** Rejected: coarser control and guidance loops.
- **C. Fixed 120 Hz with an accumulator and render interpolation.** Chosen.

**Decision.** Option C. `main.cpp` accumulates frame time, capped at `MAX_FRAME_TIME` = 0.05 s as a spiral-of-death guard, and steps `GameState::update(FIXED_DT)` at 1/120 s. Rendering uses `alpha = accumulator / FIXED_DT`.

**Consequences.**

- No frame-rate-dependent logic in `update()`.
- No raylib draw calls in `update()`, and no state mutation in `render()`.
- Replay (F-046) becomes feasible if inputs are recorded per tick.
- Edge-triggered input needs custom rising-edge detection, because raylib's `IsKeyPressed` reports per frame, not per tick (`c041c64`).

**Reversal conditions.** Revisit if physics cost exceeds about 30% of the frame budget on target hardware, or if determinism stops mattering.

### D-004 C++17, raylib 5.0 and a single CMake build with FetchContent

**Decided:** 2026-03-31 (from commit `c746571`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** D-018, [AV-013](ATTACK_VECTORS.md#av-013-raylib-name-and-default-collisions)

**Context.** A solo project targeting Linux first and Windows later. It needs immediate-mode 3D suited to flat shading, plus audio and input, with as few dependencies as possible (`VIRUS_REMAKE_ARCHITECTURE.md` §1).

**Options.**

- **A. SDL2.** Rejected: mostly 2D, so a 3D renderer would have to be built on top.
- **B. raylib.** Chosen. It provides immediate-mode 3D, miniaudio, window, input and timing, and suits the retro look.
- **Language:** C++17, for `std::optional`, `std::variant`, structured bindings and `std::filesystem`.

**Decision.**

- C++17 with extensions off, and no exceptions in hot paths.
- raylib pinned at tag 5.0 through CMake FetchContent (shallow clone).
- One root `CMakeLists.txt`, no submodules.
- `DEV_MODE` is a CMake option that adds a compile definition.

**Consequences.**

- First configure needs network access.
- raylib quirks must be guarded: the `CameraMode` typedef collision, and the default 1000-unit far plane that needs an `rlFrustum` override.
- Later dependencies follow the same header-only FetchContent pattern: doctest ([D-018](#d-018-doctest-tests-linked-only-to-the-mesh-library)) and picojson ([D-011](#d-011-sidecar-entity-profiles-by-default-registry-override-later)).

**Reversal conditions.** Add SDL2 only if controller rumble or a platform quirk needs something raylib doesn't cover.

### D-005 Single Newtonian flight model, one craft

**Decided:** 2026-05-03 (from commit `b3f1e6d`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-006, F-007, D-008

**Context.** The Phase 2 design (`project-status/archive/project-roadmap.md`) had two flight modes, Arcade and Classic, with five craft profiles (Delta, Forward Swept, X-36, YB-49, Saucer). That meant two physics models and several layers of abstraction for little gain. The goal was the *Virus*/*Zarch* "tilt and burn" feel.

**Options.**

- **A. Keep dual physics with craft profiles.** Rejected: complexity, and it diluted the core feel.
- **B. One Newtonian model.** Chosen. Thrust along the local up axis, constant world gravity, low drag, one hovercraft mesh.

**Decision.** Option B. `FlightMode` and `CraftType` were removed, along with the old F5 (craft cycle) and F6 (mode toggle) dev keys. Roll is limited to ±45°. The mouse is the primary input.

**Consequences.**

- Do not re-add `FlightMode` or `CraftType`.
- Difficulty comes from flight-assist levels, not from craft choice.
- `NEWTON_DRAG` was later raised from 0.02 to 0.15 for meaningful air resistance (`26495c8`).
- Q/E roll keys were later retired ([D-021](#d-021-typed-redblue-laser-pair-replaces-cannon-and-shield-laser)).

**Reversal conditions.** Revisit only if a second vehicle type becomes a core feature. The helicopter mode in `game_modes_and_features.md` §8 is an open candidate and would need its own decision.

### D-006 Tileable Perlin gradient fBM terrain

**Decided:** 2026-05-03 (from commit `26495c8`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-001, D-007, [AV-003](ATTACK_VECTORS.md#av-003-terrain-loses-tileability-or-sea-level-meaning), [BUG-018](BUGS.md#bug-018-archived-design-docs-claim-the-terrain-rebuild-never-shipped)

**Context.** Phases 1–1.5 used Diamond-Square with a 12-pass blur and a radial falloff to make one continent in an ocean. That cannot tile seamlessly, and the falloff produced an ocean ring around the world.

**Options.**

- **A. Diamond-Square plus radial falloff.** Shipped 2026-03-31 to 2026-05-03. Rejected: no seamless wrap; chaotic river channels.
- **B. Fourier sine synthesis** (after Braben's *Zarch*; spec `project-status/archive/terrain_rebuild.md`). Shipped in `50fa26e` on 2026-05-03 with integer cycle counts so it wrapped exactly. Rejected the same day: "sine sums produced visible parallel ridge bands no matter how many terms or how strong the warp."
- **C. Tileable Perlin gradient fBM.** Chosen. Gradient noise has no preferred direction.

**Decision.** Option C:

- 7 octaves with lattice cells from 512 down to 8, each dividing `HEIGHTMAP_SIZE − 1` = 1024, so the noise tiles exactly.
- A ridged-multifractal mountain layer (Musgrave `(1−|n|)²`) masked to highlands.
- A coupled 2D domain warp.
- A symmetric shape exponent around 0.5, so `SEA_LEVEL` keeps its meaning.
- No post-pipeline `normalise()` / `shapeContrast()`: their cumulative downshift drowned 79% of the map.

**Consequences.**

- `diamondSquare()`, `smooth()`, `applyRadialFalloff()` and `TERRAIN_ROUGHNESS` are retired. Do not re-add them.
- `terrain_rebuild.md` is obsolete. Do not implement it.
- Rivers were disabled in the same commit (F-003, [IMP-001](IMPROVEMENTS.md#imp-001-decide-the-fate-of-the-disabled-river-code)).

**Reversal conditions.** Revisit if Perlin shows visible lattice artefacts at gameplay altitudes, or if a planned feature (such as rivers) can't be made to work on this generator.

### D-007 Unwrapped player coordinates on a toroidal world

**Decided:** 2026-05-03 (from commit `50fa26e`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-001, F-002, F-018, [AV-005](ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap), [BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap)

**Context.** The world must feel infinite, with no edge. The terrain spec proposed teleport-wrapping the player.

**Options.**

- **A. Teleport-wrap the player at the edge** (`Player::wrapPosition()`). Rejected: discontinuities in the camera-follow lerp, and the camera and every entity would need to wrap together.
- **B. Infinite live-computed terrain.** Rejected: unnecessary cost.
- **C. Never wrap the player; draw the world around them.** Chosen.

**Decision.** Option C:

- Player coordinates grow without bound. Single-precision floats hold sub-metre accuracy out to about 8 million units.
- `Planet::draw` renders each chunk at the tile offset nearest the camera.
- `Heightmap::sample` and `Planet::heightAt` wrap their queries internally.
- World-fixed objects (decorations, entities) render at `Planet::nearestCopy(pos, camera)`, added 2026-10-15 in `7e846c3`.

**Consequences.**

- Every distance or draw involving world-fixed objects must allow for the wrap.
- AI, collision and radar ignored the wrap until 2026-10-15 ([BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap)). Now `EntityManager::rebaseAround()` shifts every entity and projectile onto the world copy nearest the player each tick, so existing AI code measures correctly without per-call-site wrap maths.
- `Player::wrapPosition()` is a deliberate no-op kept for API compatibility.

**Reversal conditions.** Revisit if float precision causes visible jitter in realistic sessions, or if making every distance query wrap-aware proves more costly than wrapping the whole simulation.

### D-008 Regenerating thrust charge instead of fuel

**Decided:** 2026-05-03 (from commit `be7724e`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-006, D-005

**Context.** The flight rebuild introduced finite fuel, which pushed players to ration flight rather than fly aggressively.

**Options.**

- **A. Finite fuel with refuelling.** Rejected: rationing slows play.
- **B. Regenerating thrust charge.** Chosen.

**Decision.** A thrust charge (cap 100) drains while burning and recharges while coasting. Players manage "burn-vs-recover tempo rather than ration a finite fuel pool". `NEWTON_FUEL_*` constants were retired.

**Consequences.** HUD gauge reads THRUST. Repair stations heal hull, not fuel.

**Reversal conditions.** Revisit if a resource-scarcity mode (such as the v1 resource system) needs flight endurance as a cost.

### D-009 OBJ text format as the single mesh source of truth

**Decided:** 2026-05-20 (from `3d_assets.md` header, provisional D-A01 / D-A02 / D-A08; implemented in commit `47f8d74`, 2026-06-01)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, `3d_assets.md` draft)
**Related:** F-033, D-010, D-016, D-024, [AV-009](ATTACK_VECTORS.md#av-009-obj-save-corrupts-authored-content), [BUG-011](BUGS.md#bug-011-obj-rendered-entities-lose-the-white-damage-flash)

**Context.** All entity meshes were procedural C++ geometry, which was slow to author and impossible to edit visually. An asset pipeline was needed without losing the flat-shaded look or adding a build-time conversion step.

**Options.**

- **A. Keep procedural geometry only.** The original rule ("no model files"). Rejected: authoring cost.
- **B. glTF/GLB.** Rejected: binary, doesn't diff, and is heavier than needed.
- **C. OBJ.** Chosen. Text, diffs cleanly in git, native Blender export and raylib `LoadModel`, and trivially parseable.

**Decision.** Option C:

- Meshes live in `assets/meshes/*.obj` and load at startup into `MeshRegistry` keyed by `EntityType`.
- **Normals:** face normals are always recomputed at load, and `vn` lines are ignored, so the faceted look doesn't depend on Blender's export state (D-A02).
- **Parser:** an in-house OBJ parser, rather than a third-party dependency (D-A08).
- **Fallback:** a missing OBJ falls back to procedural rendering, so migration could be partial.
- **Drawing:** `DrawModelEx` (not `rlPushMatrix`) for yaw-rotated entities.

**Consequences.**

- Procedural geometry remains for terrain, particles, shield caps and state-driven overlays.
- Tinting with `WHITE` can't override baked palette colours, so OBJ entities lose the damage flash. This is an accepted trade-off ([BUG-011](BUGS.md#bug-011-obj-rendered-entities-lose-the-white-damage-flash)).
- The Lander has no OBJ yet and renders procedurally.

**Reversal conditions.** Revisit if animated meshes are needed ([D-024](#d-024-animation-deferred-to-iqm-obj-stays-static)), or if parse time becomes noticeable at startup.

### D-010 32-colour palette via OBJ material names

**Decided:** 2026-05-20 (from `3d_assets.md`, provisional D-A03 / D-A04; amended to 32 colours in commit `47f8d74`, 2026-06-01)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, `3d_assets.md` draft)
**Related:** F-033, F-037, D-009, D-013

**Context.** Meshes need colour without texture mapping or per-vertex colour data, and must match the original *Virus*-style palette.

**Options.**

- **A. Per-vertex colours.** Rejected: allows gradients within a face, which breaks the faceted look.
- **B. MTL material colours.** Rejected: a Blender-local file that drifts from the in-game palette.
- **C. Palette index encoded in the material name.** Chosen.

**Decision.**

- Material names `c00`..`c31` (alias `palette_NN`) index a fixed palette in `src/mesh/Palette.hpp`.
- Unknown names warn and render magenta; they never crash.
- MTL files are ignored by the loader and are not committed (D-A03).
- The spec said 16 colours. It was extended to 32 so that indices 16–31 match the hand-coded ship colours from the procedural era.

**Consequences.** Recolouring a mesh means editing material names. Inspector Phase C (F-037) gives this a UI. There is no `*.mtl` rule in `.gitignore` yet; none exist today.

**Reversal conditions.** Revisit if more than 32 colours are routinely needed, or if a shader-based colouring path ([IMP-010](IMPROVEMENTS.md#imp-010-shader-based-damage-flash-for-obj-entities)) makes per-instance colour cheap.

### D-011 Sidecar entity profiles by default, registry override later

**Decided:** 2026-06-03 (from commit `d1b28be`; implemented in `a5660d7` / `c746a0a`, 2026-06-06)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-034, F-040, D-027, [IMP-004](IMPROVEMENTS.md#imp-004-retire-confighpp-per-entity-fallbacks-once-sidecars-are-authoritative), [AV-010](ATTACK_VECTORS.md#av-010-sidecar-data-silently-overrides-tuned-values)

**Context.** OBJ can't carry gameplay data such as forward axis, hardpoints, HP, shields, weapons, AI or FX. Hard-coding these in `Config.hpp` meant a recompile for every tweak, and they couldn't be edited next to the mesh.

**Options.**

- **A. Keep everything in Config.hpp.** Rejected: no authoring loop.
- **B. A registry file mapping many meshes to profiles with per-faction overrides.** Deferred: not needed until one mesh is reused across factions.
- **C. One `*.meta.json` sidecar per OBJ.** Chosen.

**Decision.** Option C:

- `foo.meta.json` sits beside `foo.obj`. A missing sidecar is fine; defaults apply.
- Unknown keys warn and are preserved on save (DOM-preserving, picojson v1.3.0 header-only).
- The game-side `EntityProfileRegistry` is scanned at init and read on every spawn, so inspector edits take effect on the next spawn.
- Migration is opt-in per entity type.

**Consequences.**

- Per-entity constants now exist twice (sidecar plus Config fallback) until [IMP-004](IMPROVEMENTS.md#imp-004-retire-confighpp-per-entity-fallbacks-once-sidecars-are-authoritative) lands.
- Sidecar values can bypass TTK arithmetic ([AV-010](ATTACK_VECTORS.md#av-010-sidecar-data-silently-overrides-tuned-values)).
- Physics, world and engine constants stay in Config.

**Reversal conditions.** Add the registry override when one mesh must carry different stats per faction, for example a captured-friendly variant in Slice C.

### D-012 Flat type-tagged entity pools, no ECS, no hot-path allocation

**Decided:** 2026-05-06 (from commit `c041c64`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-016, [AV-006](ATTACK_VECTORS.md#av-006-heap-allocation-in-the-hot-path), [AV-011](ATTACK_VECTORS.md#av-011-quadratic-entity-queries-without-a-spatial-grid)

**Context.** Fewer than 500 live entities. The goals were cache-friendliness, no pointer-chasing polymorphism and no allocation during play.

**Options.**

- **A. An ECS framework.** Rejected: overhead out of proportion to the entity count.
- **B. A deep inheritance hierarchy.** Rejected: virtual dispatch and scattered allocations.
- **C. One type-tagged `Entity` struct in fixed pools.** Chosen.

**Decision.**

- `EntityManager` owns two fixed `std::array` pools: `ENTITY_POOL_SIZE` (256, enemies and friendlies) and `PROJECTILE_POOL_SIZE` (512, separate because projectiles have a different update body).
- Allocation is round-robin and recycles the oldest slot when full.
- The particle pool (2000) and the radar ghost-blip pool (32) are also pre-allocated.
- No `new` or `malloc` inside `update()` or `render()`.

**Consequences.**

- Adding entity state grows every slot, so new behaviour reuses existing fields where it can (for example, drones reuse `stateTimer` / `targetPos`).
- Per-type AI lives in `EntityManager.cpp` behind a type switch. The `Enemy`, `Friendly` and `Projectile` files are empty.
- No spatial grid exists yet.

**Reversal conditions.** Revisit if live entity counts approach the pool limits in normal play (Slice C), or if `EntityManager.cpp` becomes unmaintainable ([IMP-014](IMPROVEMENTS.md#imp-014-delete-or-populate-the-empty-stub-modules)).

### D-013 Flat-shaded low-poly aesthetic, no textures

**Decided:** not recorded; in force from the first commits (2026-03-31)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** D-010, F-043

**Context.** The target is "modernised but stylistically similar" to the 1988 original: "looks like it could have run in 1992 on very powerful hardware — not a modern game with a retro filter applied."

**Options.**

- **A. Modern rendering with a retro post-filter.** Rejected: wrong feel.
- **B. Genuinely flat-shaded geometry.** Chosen.

**Decision.**

- Flat-shaded polygons with visible faces. No texture mapping and no specular.
- One directional sun, ambient about 0.45.
- One colour per face, from height or biome bands or the 32-colour palette.
- Fog matches the sky colour at maximum draw distance.
- Only simple particles.

**Consequences.**

- Shield visuals (impact caps), post-FX and any future shaders must preserve flat faces.
- Gradients within a face are forbidden.

**Reversal conditions.** None planned. This is the project's identity.

### D-014 TTK-derived enemy HP

**Decided:** 2026-05-06 (from commit `c041c64`; design in `project-status/archive/combat_tuning.md`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-016, D-021, [AV-008](ATTACK_VECTORS.md#av-008-carrier-shields-regenerate-under-sustained-beam-fire), [AV-010](ATTACK_VECTORS.md#av-010-sidecar-data-silently-overrides-tuned-values)

**Context.** Hand-tuning individual HP values made balance incoherent across weapons and enemies.

**Options.**

- **A. Tune HP per enemy by feel.** Rejected: no anchor.
- **B. Derive HP from time-to-kill targets.** Chosen.

**Decision.**

- `total_HP = TTK × reference DPS`, where the reference is `CANNON_DPS` = 100. Shield HP is total × shield fraction; hull HP is the remainder.
- Rebalance by changing TTK, not HP: "adjust the principle first, then re-derive."
- Design principles kept from the combat design:
  - Every weapon has a purpose.
  - The Beam Laser exists because of the Carrier.
  - The Depth Charge exists because of ground targets.
  - Shield recharge delay must be longer than a typical engagement burst.

**Consequences.**

- When the Cannon was removed, the `CANNON_*` constants were kept as the DPS reference, and laser favoured-type DPS was matched to 100 ([D-021](#d-021-typed-redblue-laser-pair-replaces-cannon-and-shield-laser)).
- The Lander departs from TTK derivation ([BUG-014](BUGS.md#bug-014-lander-damage-budget-is-smaller-than-the-carriers-despite-design-intent)).

**Reversal conditions.** Revisit if sidecars become authoritative and the TTK derivation moves into authoring tools ([IMP-004](IMPROVEMENTS.md#imp-004-retire-confighpp-per-entity-fallbacks-once-sidecars-are-authoritative)).

### D-015 Five camera views, with Tactical using a Z-up camera

**Decided:** 2026-05-03 (from commit `6dcf35f`; design in `project-status/archive/camera_system.md`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-009, [AV-004](ATTACK_VECTORS.md#av-004-camera-clips-below-terrain), [AV-013](ATTACK_VECTORS.md#av-013-raylib-name-and-default-collisions), [BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub)

**Context.** Newtonian flight near terrain needs different views for combat, terrain avoidance and battlefield awareness.

**Options.** A single chase camera was rejected: it's poor for terrain avoidance and for overhead awareness.

**Decision.**

- Keys 1–5 select the views, with zoom remembered per view:
  - **Chase:** yaw-only follow, so the horizon stays level.
  - **Velocity:** follows the velocity vector.
  - **Tactical:** overhead, north up, with `camera.up = {0, 0, 1}`.
  - **ThreatLock:** keeps the nearest threat in frame (hysteresis, rotation cap).
  - **Classic:** fixed world-space diagonal, like the original.
- `CameraView` is distinct from the DEV-only `CamMode` (Follow / FreeRoam), and neither may be named `CameraMode` (a raylib typedef).
- Every view clamps the camera above the terrain.

**Consequences.** ThreatLock's target lookup was never implemented ([BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub)).

**Reversal conditions.** Drop a view if playtesting shows it's never used.

### D-016 Inspector as a separate binary with a Tool registry

**Decided:** 2026-06-02 (from commit `0522dd7`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-035..F-042, D-009, D-017, D-018

**Context.** `3d_assets.md` (provisional D-A05) planned the inspector as a `--inspect` CLI mode of the game binary, to avoid duplicating raylib setup, and listed a separate binary as an anti-goal.

**Options.**

- **A. `--inspect` mode in the game.** Rejected once built: the editor and the game share nothing at runtime, and editor-only code would bloat the game.
- **B. Separate `terra-siege-inspect` executable sharing a static mesh library.** Chosen.

**Decision.**

- `terra-siege-inspect` links the `terra_siege_mesh` static library (ObjLoader, MeshRegistry, Palette, SidecarProfile, EntityProfileRegistry), shared with the game and the tests.
- Editing features are `Tool` subclasses (`handleInput`, `render3D`, `renderHud`, `save`, `isDirty`, `canRunWithoutMesh`) that TAB cycles through, so new tools plug in without touching `Inspector`.
- `RAYMATH_IMPLEMENTATION` is defined on each executable, not on the library.
- No external GUI library: plain raylib drawing, with ImGui as a later option "if the UI gets dense". This also reverses the `3d_assets.md` raygui plan.

**Consequences.**

- One CMake build produces two executables.
- Twelve tools ship.
- The game's `--inspect` branch was removed.

**Reversal conditions.** Merge back only if the inspector needs live game state (in-game hotswap, F-042), which is an open question.

### D-017 Inspector scope widened to materials, topology and primitives

**Decided:** 2026-06-07 (from commit `771b6c1`; extended by `e157e0d` and `48ed9f8`, 2026-06-11)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-036..F-039, D-016, [AV-009](ATTACK_VECTORS.md#av-009-obj-save-corrupts-authored-content)

**Context.** `3d_assets.md` (provisional D-A06) limited the inspector to editing vertex positions, to guarantee byte-exact round-trips, and left topology to Blender. In practice, small mesh changes still forced a Blender round-trip.

**Options.**

- **A. Vertex-only.** Rejected: the authoring loop stayed slow.
- **B. Add materials, topology, primitives and shared undo, with separate save paths.** Chosen.

**Decision.** Three save paths in `ObjLoader`:

- `saveObjVertices`: byte-exact; rewrites only `v` lines.
- `saveObjMaterials`: replaces the face section.
- `saveObjMesh`: full rewrite, because topology changes "break the byte-exact preservation contract".

Undo is held at inspector level as full-mesh snapshots (32 deep) and is shared across tools.

**Consequences.**

- Byte-exact round-trip now holds only for vertex-only edits.
- `saveObjMesh` drops `vn` and `vt`, which are recomputed or unused anyway.
- The material and mesh save paths have no tests ([IMP-016](IMPROVEMENTS.md#imp-016-tests-for-the-material-and-full-mesh-obj-save-paths)).

**Reversal conditions.** Revisit if inspector saves corrupt Blender round-trips in practice.

### D-018 doctest, tests linked only to the mesh library

**Decided:** 2026-06-01 (from commit `47f8d74`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** D-004, D-016, [IMP-017](IMPROVEMENTS.md#imp-017-continuous-integration-for-build-and-tests)

**Context.** The mesh pipeline needed regression tests without slowing the build or adding a heavy dependency.

**Options.**

- **A. Catch2.** Rejected: about 10× slower to compile, with a larger dependency surface.
- **B. doctest v2.4.11 (header-only, FetchContent).** Chosen.

**Decision.**

- `TERRA_SIEGE_BUILD_TESTS` defaults to ON.
- Each `tests/test_*.cpp` is its own executable, so failures bisect easily.
- Tests link `terra_siege_mesh`, not the game.

**Consequences.**

- Game logic (physics, combat, terrain, wrap) has no automated tests.
- Most attack vectors outside the mesh/sidecar layer have detection "not implemented" ([ATTACK_VECTORS.md](ATTACK_VECTORS.md)).
- There is no CI.

**Reversal conditions.** Extend the tests to game logic when a pure function (wrap math, TTK derivation, damage routing) can be extracted from raylib-dependent code.

### D-019 Feature track delivered in slices A → B → C

**Decided:** 2026-06-03 (from commit `d1b28be`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-017, F-015, F-020, D-020

**Context.** The game-modes design (`project-status/game_modes_and_features.md`) was about 1,100 lines, too much to land at once.

**Options.**

- **A. Implement the whole design together.** Rejected.
- **B. Three independently shippable slices.** Chosen.

**Decision.**

- **Slice A** (smoke and retreat) first, because it is small, code-only and "proves nothing breaks".
- **Slice B** (infection and new weapons), because "the infection mechanic is shippable without committing to the full RTS build".
- **Slice C** (Base mode) last.
- Open design questions are resolved per slice when that slice lands, not upfront.

**Consequences.** Slices A and B shipped 2026-06-03/04. Slice C is in progress ([ROADMAP.md](ROADMAP.md)).

**Reversal conditions.** None; this is a sequencing decision.

### D-020 Base Mode v2: asymmetric defender vs invader

**Decided:** 2026-06-02 (from `project-status/base_mode_v2.md` "Active design as of 2026-06-02")
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-020..F-027, D-019, D-023

**Context.** The v1 design (`game_modes_and_features.md` Part 2) was a symmetric 20v20 war, with autonomous economies on both sides and the world split along Z.

**Options.**

- **A. Symmetric 20v20 with economies.** Rejected: an RTS-scale build with unclear player agency on a toroidal world.
- **B. Asymmetric defence.** Chosen. "The friendly side is infrastructure under attack, not an economy at war."

**Decision.**

- **Friendly side:** pre-placed bases and a radar network. The side is passive: no collectors or builders; defenders spawn only when attacked; landing-pad repair; help calls.
- **Enemy side:** a few grounded Landers that each run their own production. No Landers arrive mid-game, so the threat is finite.
- **Infection:** needs shields stripped and hull ≤30%; works in both directions; bases can re-flip.
- **Radar:** the network degrades as nodes fall; the player keeps a personal radar.
- **Win:** all Landers destroyed and all infected bases retaken.
- **Loss:** all friendly bases lost, or the player destroyed.

**Consequences.**

- v1 Part 2 is superseded. v1 §§4–9 (resources, ship infection, orbit drones, weapons, helicopter, smoke) still apply where not overridden.
- Open questions are listed in `base_mode_v2.md` and [ROADMAP.md](ROADMAP.md) Slice C.

**Reversal conditions.** Per `base_mode_v2.md` open questions. For example, reconsider continuous Lander arrivals if rounds end too quickly in playtesting.

### D-021 Typed red/blue laser pair replaces Cannon and Shield Laser

**Decided:** 2026-06-22 (from commit `d0cc533`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-010, F-029, D-014, [BUG-005](BUGS.md#bug-005-holding-q-or-e-to-select-a-laser-colour-suppresses-auto-level-roll), [IMP-005](IMPROVEMENTS.md#imp-005-consolidate-weapon-stats-into-a-weaponspec-table), [IMP-006](IMPROVEMENTS.md#imp-006-laser-pair-polish-dedicated-sfx-and-crosshair-colour)

**Context.** With the Cannon as the default primary and a separate Shield Laser (Slice B.2), choosing a weapon wasn't an interesting moment-to-moment decision.

**Options.**

- **A. Keep Cannon plus Shield Laser.** Rejected: weak tactical choice.
- **B. A typed laser pair with a damage-type curve.** Chosen: "tactical weapon switching mid-engagement is now the core combat decision."

**Decision.**

- New `DamageType` {Hull, Shield, Neutral}. One knob, `WRONG_TYPE_MULT` = 0.25, controls the whole rock-paper-scissors curve, applied per overflow stage.
- Q selects red (Hull), E selects blue (Shield). LMB auto-fires; Shift+LMB charges a heavy shot.
- A shared laser battery covers both.
- Two-step spread: 1.5° within 150 m, 8° beyond. Charged shots have no spread.
- Primaries are now LaserPair / Plasma / Beam.
- Q/E roll keys are retired.

**Consequences.**

- Favoured-type DPS equals the 100 DPS TTK reference, so no enemy HP recalibration was needed.
- The v1 §7 "4-weapon cycle / Shield Laser" text is stale.
- Flight assist still reads Q/E as roll input ([BUG-005](BUGS.md#bug-005-holding-q-or-e-to-select-a-laser-colour-suppresses-auto-level-roll)).

**Reversal conditions.** Revisit if playtesting shows players stick to one colour, which would mean the 0.25 curve is too flat.

### D-022 Procedural in-engine SFX synthesis

**Decided:** 2026-06-07 (from commit `cdaa52c`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-030, [IMP-007](IMPROVEMENTS.md#imp-007-engine-thrust-loop-sfx)

**Context.** Phase 5 needed audio. The architecture plan assumed pre-loaded sound files, but none existed, and asset licensing and authoring were overhead.

**Options.**

- **A. Sampled WAV assets.** Deferred: an rFXGen pipeline (`gen_sfx.sh`) exists for a later higher-fidelity pass.
- **B. Procedural synthesis at startup.** Chosen.

**Decision.**

- Each sound is synthesised from layered saw, sub and noise, with bit-crush and a tanh soft clip, and per-shot pitch variance. Musical sounds skip variance and crush.
- Each SFX has a pool of 6 aliases so sounds can overlap.
- Panning is manual, from the dot product with the listener's right vector, with linear distance falloff (raylib has no 3D audio).

**Consequences.** There are no audio assets in the repo. Continuous sounds (engine loop) don't fit the one-shot alias pool.

**Reversal conditions.** Switch to sampled assets if synthesis can't reach acceptable quality for key events.

### D-023 Ship infection rules (v1)

**Decided:** 2026-06-04 (from commits `44b5d99` and `0b7d64c`; design in `game_modes_and_features.md` §5)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-015, F-025, D-020, [IMP-008](IMPROVEMENTS.md#imp-008-seeder-and-carrier-infection), [BUG-009](BUGS.md#bug-009-infected-ships-still-show-as-hostile-on-radar), [BUG-010](BUGS.md#bug-010-in-flight-auto-aimed-projectiles-can-still-hit-newly-infected-ships)

**Context.** Slice B introduced converting enemy ships.

**Options.**

- Infectable types: all enemies, or only those whose AI makes sense attacking former allies. The latter was chosen.
- Re-infection: allowed, or once only. Once only was chosen.

**Decision.**

- Only Fighters and Bombers can be infected. Seeder and Carrier "orbit-and-drop AI doesn't map cleanly"; drones and ground units are excluded.
- Shields must be at zero; otherwise the Infectious Missile is a dud.
- A 3 s reboot (frozen, immune), then a permanent flip with `canBeInfected = false`.
- Infected ships fight where they are and don't retreat.
- Auto-aim weapons skip infected ships; manual aim can still hit them.
- The player can't be infected.
- Added in code, beyond the design doc: infected ships bleed 1% of max hull per second (about 100 s of life). This departs from v1 §5.3 "hull HP unchanged" and keeps converted ships disposable.

**Consequences.** Base infection v2 (F-025) changes the gate (shields stripped and hull ≤30%), makes it symmetric and allows re-flips. That will need its own decision when C.6 lands.

**Reversal conditions.** Revisit the bleed if infected allies feel pointless in playtesting. Revisit recruitability at bases (an open question).

### D-024 Animation deferred to IQM; OBJ stays static

**Decided:** 2026-05-20 (from `3d_assets.md` §12, provisional D-A07)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, `3d_assets.md` draft)
**Related:** D-009

**Context.** OBJ has no animation. Some entities have moving parts, such as turrets and the radar dish.

**Options.**

- **A. Bolt animation conventions onto OBJ.** Rejected: muddles the format contract.
- **B. Animate procedurally now, and use IQM only when a skinned or animated mesh is really needed.** Chosen.

**Decision.** Moving parts (Base turret, radar dish, Collector cabin pip) are procedural overlays driven by state. If animation is ever needed, introduce IQM for those meshes only, and don't retrofit existing OBJ assets.

**Consequences.** Moving parts stay procedural and outside the inspector's reach.

**Reversal conditions.** The first entity that needs skeletal or keyframe animation.

### D-025 Drones as a stand-off gunship swarm with a global cap

**Decided:** 2026-06-06 (from commit `bacf480`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-016, [AV-011](ATTACK_VECTORS.md#av-011-quadratic-entity-queries-without-a-spatial-grid)

**Context.** Kamikaze drones (5d.1) with boids flocking were either trivial or overwhelming, and Seeders and Carriers could flood the pool.

**Options.**

- **A. Kamikaze contact-damage swarm.** Rejected: binary threat.
- **B. Light gunships that keep their distance and orbit, with a global cap.** Chosen: the threat "is the swarm count, not any one barrel".

**Decision.**

- Drones hold a 25–40 m stand-off band with orbital drift and fire light shots.
- `DRONE_GLOBAL_CAP` = 16. Deploys at the cap are skipped without consuming the cooldown.
- Seeder and Carrier deploy intervals were cut 30% to compensate.

**Consequences.**

- The cap bounds the O(N²) boids cost.
- `base_mode_v2.md` still calls drones a "kamikaze swarm escort" (stale wording).

**Reversal conditions.** Raise or remove the cap if a spatial grid ([IMP-015](IMPROVEMENTS.md#imp-015-spatial-grid-for-entity-queries)) removes the performance constraint and play needs bigger swarms.

### D-026 Old-style pulse radar

**Decided:** 2026-10-15 (from commit `50506fc`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** F-028, F-023, [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range)

**Context.** The radar showed every contact in real time with distance-based blinking. The author wanted the feel of a classic radar scope.

**Options.**

- **A. Real-time blips with proximity blink.** Replaced.
- **B. Pulse painting.** Chosen. Every `RADAR_PULSE_PERIOD` (1 s) a ring expands to the edge over 0.6 s. Contacts are painted at the position they occupy when the ring reaches them, then fade to 25% until repainted.

**Decision.**

- Option B.
- Echo state is a fixed per-pool-slot array keyed by entity id, with no hot-path allocation.
- Missile warnings, Carrier jamming and ghost blips stay real-time, so the player isn't blind to incoming fire.

**Consequences.**

- Fast contacts jump between paints, which is deliberate.
- New entities stay dark until the pulse reaches them.
- The radar's range semantics are still open ([BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range)).

**Reversal conditions.** Shorten the period or sweep if playtesting shows the 1 s refresh makes combat unfair.

### D-027 Config.hpp as the single source of tuning truth (transitional)

**Decided:** 2026-03-31 (from commit `c746571`; transitional scope set 2026-06-03, `948d6e8`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** D-002, D-011, [IMP-004](IMPROVEMENTS.md#imp-004-retire-confighpp-per-entity-fallbacks-once-sidecars-are-authoritative)

**Context.** Magic numbers scattered through logic made tuning untraceable.

**Options.**

- **A. Constants local to each file.** Rejected.
- **B. Runtime JSON config.** Deferred: a live-reload layer could be added later.
- **C. One `constexpr` header.** Chosen.

**Decision.**

- Every gameplay constant lives in `src/core/Config.hpp` as `constexpr`. No magic numbers in logic code. Playtest tunables are marked `// TUNE`.
- Per-entity constants are migrating to sidecars ([D-011](#d-011-sidecar-entity-profiles-by-default-registry-override-later)).
- Physics, world and engine constants stay in Config.

**Consequences.**

- Config.hpp (about 830 lines) is the de facto technical spec ([D-002](#d-002-exempt-buildmd-and-specmd-keep-design-references-in-project-status)).
- Docs must link to it rather than copy values.
- Known violations: the camera clamp margins, the decoration render radius (800) and some fire-cone dot thresholds are inline literals.

**Reversal conditions.** Revisit if live tuning without recompiling becomes a frequent need.

### D-028 Friendlies scattered world-wide on grassland

**Decided:** 2026-10-15 (from commit `7e846c3`)
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** F-018, F-020, D-007, [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally)

**Context.** All friendlies spawned inside a 220 m ring around the player's start, so the rest of the world was empty of anything to defend.

**Options.**

- **A. Keep the spawn ring.** Rejected.
- **B. Scatter everything world-wide.** Rejected for the home Base and its collectors: the collector delivery loop needs them close together, and the player needs a visible home.
- **C. Keep the home Base and collectors near the player; scatter the rest.** Chosen.

**Decision.**

- Repair stations, radar boosters and Base-mode extra bases are rejection-sampled on grassland (`Planet::isGrassland`, `BIOME_*` bands) with a gentle slope.
- Spacing is wrap-aware, starting at `FRIENDLY_SPAWN_SPREAD_DIST` (500 m) and halving per pass on sparse seeds, with the old ring as the last resort.

**Consequences.**

- Friendlies can now be far from the player and across the world seam, which exposes [BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap) and [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally).
- Base-mode bases may be too far apart for a connected radar network. Tune with `NETWORK_LINK_RANGE` in C.4.

**Reversal conditions.** Constrain base placement if C.4 needs bases within link range of each other.

### D-029 Exempt CLAIMS.md

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** D-001

**Context.** The standard requires CLAIMS.md for projects that make empirical or theoretical assertions.

**Options.** Write CLAIMS.md (rejected) or exempt it (chosen).

**Decision.** terra-siege makes no research claims. Its falsifiable statements are engineering acceptance criteria, which live in [FEATURES.md](FEATURES.md).

**Consequences.** None.

**Reversal conditions.** Add CLAIMS.md if the project starts asserting measurable properties outside feature acceptance, such as performance figures published in the README.

### D-030 Base defenders reuse the allied-Fighter path; pads replace Repair Stations in Base mode

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** F-021, F-023, D-020, D-023, [BUG-026](BUGS.md#bug-026-enemy-fire-never-damages-friendly-structures-or-allied-ships)

**Context.** C.2 needs friendly bases that launch defenders when attacked, repair and rearm a docked player, and a radar station entity for the C.4 network. The codebase had no friendly-ship concept apart from infected ships. Allegiance was decided by copied hard-coded type lists.

**Options.**

- **A. A new `Defender` AI state or faction field, with its own targeting and firing.** Rejected: it duplicates the allied-ship path, and every allegiance check would need a third case.
- **B. Reuse the Infected (allied) Fighter path, with a `homeBaseId` field.** Chosen. It already provides allied targeting, player-owned shots, auto-aim exemption, a green tint, and now green radar blips.
- **Repair:** keep Repair Stations in Base mode, or move repair to base landing pads as the v2 design says. Pads were chosen.

**Decision.**

- **Defenders.** A defender is a Fighter spawned in `AIState::Infected` with `homeBaseId` set. Compared with an infected ship, it:
  - doesn't bleed hull;
  - engages only hostiles within `BASE_DEFENDER_LEASH` of its launch point;
  - flies home and despawns when nothing is in range.
- **Launching.** A base launches defenders while `timeSinceHit` < `BASE_UNDER_ATTACK_WINDOW`, or while a hostile is within `BASE_ALERT_RANGE`, at most `BASE_DEFENDER_MAX` alive per base. Allied ships fire within a wider nose cone (`ALLIED_FIRE_CONE_DOT`, about 45°), because Fighter turn rates can't line up on drones orbiting at 25–40 m. Playtesting found defenders and drones locked in a no-damage stalemate without it.
- **Landing pads.** A landed player within `BASE_PAD_RADIUS` is healed, then rearmed after `BASE_PAD_REARM_TIME`.
- **Base-mode placement.** Base mode places Bases and RadarStations only. Repair Stations and Radar Boosters are Wave-mode entities.
- **Allegiance.** Friendly structures are defined once, in `isFriendlyStructure()`.
- **Loss condition.** Base mode is lost when all *bases* fall (`liveDefendedCount`).
- **Modes.** Defenders and pads apply in both game modes. Docking counts as either landing or hovering low and slow over the pad.
- **Landing platform.** Each base stands on a 36 m platform raised `BASE_PAD_HEIGHT` above levelled ground, with the tower at the NE corner. Ramps on the south and east sides, where that side stands above the ground beside it, are the only way Collectors get on or off: they follow waypoint routes (approach, ramp top, centre) through `surfaceHeightAt()`, with a corner detour when a direct leg would cross the platform. The terrain under it is levelled: `Planet::flattenPad` levels the terrain to the footprint's mean height with a 10 m blend and rebuilds the affected chunks; `Decorations::clearSquare` clears the site. This keeps landing physics, ground vehicles and the camera on a real flat surface, rather than a slab floating over a slope.
- **Enemy aggression.** Fighters, Drones and Tanks pick their target with `hostileTargetFor()`. Raiders (`ENEMY_RAIDER_PERCENT`, fixed per ship by id) always prefer a friendly in range. Everyone else uses the nearest friendly structure or allied ship inside their detection range when it is closer than the player × `ENEMY_FRIENDLY_PRIORITY`, otherwise the player. Landers deploy when any friendly is in range. Bombers keep their own strafe logic.

**Consequences.**

- Making bases attackable exposed [BUG-026](BUGS.md#bug-026-enemy-fire-never-damages-friendly-structures-or-allied-ships): enemy shots never hit friendlies. That fix applies in both modes, so Wave mode is harder too.
- Without the aggression rule, playtesting (2026-10-15) found that bases were never attacked and defenders never launched. With it, attacks spread across the map, and Wave mode is harder because Collectors and the home Base now come under deliberate fire.
- Defenders are counted in `m_liveEnemies` and in WaveManager's alive count, like infected ships. This is harmless in Base mode, which has no waves.
- C.6 (infection v2) must keep defenders (`homeBaseId != 0`) distinct from infected ships.

**Reversal conditions.**

- Give defenders their own state if they need behaviour the infected path can't express, such as formation flying or escorting the player.
- Bring Repair Stations back to Base mode if pad-only repair makes the map feel too sparse.

### D-031 Player's personal radar stays at 300 m in Base Defence mode

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** F-023, F-028, D-026, [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range)

**Context.** `base_mode_v2.md` proposed a short-range personal radar of about 150 m for C.4, with long-range awareness coming only from the base and radar-station network. The player's radar is 300 m today, and the old-style pulse sweep (D-026) is built around it.

**Options.**

- **A. Cut the personal radar to about 150 m, as the design proposes.** Rejected: halving it changes how the game feels and how the pulse sweep reads.
- **B. Keep the personal radar at 300 m. The network adds coverage beyond it.** Chosen.

**Decision.** In C.4 the player's personal radar stays at `RADAR_BASE_RANGE` (300 m). The radar network adds contacts beyond that range, from clusters of linked bases and radar stations.

**Consequences.** C.4 still needs to settle [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range): contacts outside both the personal and the network coverage should stop showing as edge-of-disc markers.

**Reversal conditions.** Revisit if playtesting shows the network adds nothing because 300 m already covers the fights that matter.

### D-032 Radar network: relay chains, base-anchored clusters, pulse-painted rim markers

**Decided:** 2026-10-15
**Recorded:** 2026-10-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude Code)
**Related:** F-023, F-028, D-020, D-026, D-031, [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range), [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally)

**Context.** C.4 needs a radar network whose coverage degrades as nodes fall. Towers were scattered at least 500 m apart, so with a link range of about 300–400 m almost nothing linked. The player's personal radar stays at 300 m (D-031).

**Options.**

- **Placement.** Choices were relay chains between bases (chosen), more random towers, or an even grid.
- **Which clusters feed the player.** Choices were clusters containing a base (chosen), only the cluster the player is near, or the whole network.
- **Showing contacts beyond 300 m.** Choices were rim markers painted by tower pulses (chosen), or adding a zoomed-out network map (deferred).
- **Modes.** The network applies in both modes (chosen) rather than Base Defence only.

**Decision.**

- **Nodes and links.** Nodes are bases, radar towers and Radar Boosters, linked within `NETWORK_LINK_RANGE` (400 m, wrap-aware). `EntityManager::updateRadarNetwork()` runs union–find every tick with fixed arrays, and a cluster is online while it contains a living base.
- **Relay chains.** Towers every 350 m join each base to its 2 nearest bases, up to `NETWORK_MAX_CHAIN_TOWERS`.
- **Coverage.** The player covers 300 m. Each online tower or booster covers `RADAR_STATION_RANGE` (300 m).
- **Painting.** A contact shows only while covered. It is painted by the player's pulse, or by a covering node's pulse at `RADAR_STATION_PULSE_OFFSET` plus its distance divided by the ring speed. Uncovered contacts are dropped; the existing ghost logic marks their last position.
- **Booster.** The Wave-mode Booster becomes an ordinary node, removing the global 500 m boost.
- **Defended count.** Radar towers no longer count towards the defended-friendlies total.

**Consequences.**

- Fixes BUG-001 and BUG-002.
- Wave mode gets about 40 relay towers, so `ENTITY_POOL_SIZE` was raised to 512.
- Raiders target towers as friendlies, so chains erode under attack and coverage visibly degrades.
- Help calls (C.5) can propagate along the same graph.

**Reversal conditions.**

- Add the network map view if rim markers alone leave players unable to read where coverage gaps are.
- Revisit chain density if tower counts hurt frame rate or crowd the landscape.
