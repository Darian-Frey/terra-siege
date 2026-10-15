# Roadmap

Phased plan for terra-siege. Work runs on three tracks:

| Track | Scope | Detailed design |
| --- | --- | --- |
| **Engine** | Phases 1–6: terrain, flight, combat, shields and HUD, polish, extras | This file |
| **Features** | Game-mode slices A → B → C | [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md) (v1), [project-status/base_mode_v2.md](project-status/base_mode_v2.md) (Slice C) |
| **Tooling** | `terra-siege-inspect` phases A–G, F.1–F.6 | [project-status/terra_siege_inspect_roadmap.md](project-status/terra_siege_inspect_roadmap.md) |

Phases are append-only: completed phases are marked with an ISO date, never deleted. Feature IDs refer to [FEATURES.md](FEATURES.md). Completion dates come from `git log`; phases completed before 2026-10-15 were backfilled when the project adopted the documentation standard ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)).

## Current focus

**Slice C — Base Defence mode.** C.2 is done (2026-10-15). Next comes **C.4, the radar network** (F-023), which also settles [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range) and [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally). Decide the player's personal radar range first: the design says about 150 m, against today's 300 m. C.3 follow-ups (Lander production) can be done before or after.

Recommended order from here:

1. Slice C: C.4 → C.3 follow-ups → C.5 → C.6 → C.8
2. Inspector F.6 (resources tool) when Slice C asset authoring needs it
3. Engine Phase 4 finish (ThreatLock, [BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub)) and Phase 5 remainder, in spare cycles
4. Inspector B follow-ups and G, when useful

---

# Engine track

## Phase 1 — Foundation

**Goal:** CMake scaffold, raylib window, fixed-timestep core loop and first procedural terrain.
**Status:** Complete (2026-03-31)
**Features delivered:** — (foundation for F-001, F-006)
**Deliverables:**

- [x] CMake build fetching raylib 5.0 (`c746571`)
- [x] `Clock` fixed-timestep accumulator at 120 Hz, `Config.hpp`, `GameState` (`c746571`)
- [x] Heightmap, `Planet`, flat-shaded `TerrainChunk` mesh, free-roam camera (`7a17a57`)

**Acceptance:** Window opens, the terrain renders flat-shaded, and the camera flies freely.

---

## Phase 1.5 — Terrain scale-up

**Goal:** Full-size heightmap with water features.
**Status:** Complete (2026-04-02)
**Features delivered:** F-001 (partial), F-003 (later disabled)
**Deliverables:**

- [x] 1025² heightmap, river carving, lake flooding, debug HUD (`68e0d12`)

**Acceptance:** A 1025² terrain generates with lakes and rivers.

---

## Phase 2 — Flight, camera and rebuild

**Goal:** A playable Newtonian hovercraft over a seamless world, with the camera system.
**Status:** Complete (2026-05-03)
**Features delivered:** F-001, F-006, F-007, F-008, F-009 (ThreatLock stubbed)
**Deliverables:**

- [x] Player entity and flight physics (`d0dc353`, 2026-04-03)
- [x] Rebuild: single Newtonian flight model, one craft, FlightMode/CraftType removed (`b3f1e6d`)
- [x] Regenerating thrust charge replaces fuel (`be7724e`)
- [x] Terrain rebuild: tileable Perlin fBM, ridged mountains, toroidal wrap (`26495c8`)
- [x] Five-view camera, per-view zoom, wireframe flight HUD (`6dcf35f`)
- [x] Ground shadow, 2000-slot particle pool, engine exhaust (`9660252`)

**Acceptance:** The hovercraft flies with tilt-and-burn physics at 120 Hz over terrain that wraps with no visible edge, and all five camera keys switch views.

---

## Phase 3 — Combat foundation

**Goal:** A complete combat loop: enemy roster, weapon roster, waves, friendlies and economy.
**Status:** Complete (2026-06-01)
**Features delivered:** F-011, F-012, F-013, F-016, F-018, F-019, F-031, F-032
**Deliverables:**

- [x] 5a/5b — Entity pools, TTK-derived HP, Fighter AI, Cannon, menus, settings (`c041c64`, 2026-05-06)
- [x] 5c — Wave manager, radar tier 1 (`5763f75`, 2026-05-06)
- [x] 5d.1 — Drone swarm, Fighter EVADE engine damage (`717a615`, 2026-05-07)
- [x] 5d.2 — Seeder, Ground Turret, F7 skip-wave (`2bd918c`, 2026-05-07)
- [x] 5d.3 — Bomber (`55e44d2`, 2026-05-07)
- [x] 5d.4 — Carrier boss with 4-sector shields (`18c32da`, 2026-05-14)
- [x] 5e.1 — Plasma, homing Missile, EMP (`aafa9b1`, 2026-05-31)
- [x] 5e.2 — Beam, Cluster, Depth Charge, Auto Turret, Shield Booster (`1d97de2`, 2026-05-31)
- [x] 5g — Collector, Repair Station, Radar Booster; Bomber STRAFE_FRIENDLY (`f22f08e`, 2026-05-31)
- [x] 5f — Pre-flight loadout screen (`932eb19`, 2026-06-01)
- [x] 5h — Base entity, collector economy loop, ground tank, base auto-turret, friendly-fire filter (`092cb14`, `667e52b`, 2026-06-01)

**Acceptance:** A wave round can be played start to finish against every enemy type with the full weapon roster, and losing all friendlies ends the game.

---

## Phase 3.5 — Atmospheric terrain

**Goal:** Populate the world with visual-only terrain decorations.
**Status:** Complete (2026-06-08)
**Features delivered:** F-002
**Deliverables:**

- [x] Noise-mask forests, biome-weighted rocks, antennas, crash sites (`bdc63e5`)
- [x] Wrap-aware decoration rendering (`7e846c3`, 2026-10-15)

**Acceptance:** Forests appear on grassland across the whole world, deterministically per seed, and stay visible across the world wrap.

---

## Phase 4 — Shields and HUD

**Goal:** Directional shields and a complete tactical HUD.
**Status:** In progress
**Features delivered:** F-014, F-028, F-029; F-009 (ThreatLock outstanding)
**Deliverables:**

- [x] Player directional shields, shield pie HUD (`fec0f04`, 2026-05-15)
- [x] Radar tier 2/3: jamming, missile warnings, ghost blips, velocity arrows (`6d3a339`, 2026-06-01)
- [x] Shield impact-cap visuals (`784f027`, 2026-06-05)
- [x] Old-style pulse radar (`50506fc`, 2026-10-15)
- [ ] ThreatLock camera target lookup ([BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub))
- [ ] Radar range semantics settled ([BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range), [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally))

**Acceptance:** All five camera views behave as specified, and the radar's range limits what the player can detect.

---

## Phase 5 — Polish

**Goal:** Audio, atmosphere and post-processing.
**Status:** In progress
**Features delivered:** F-030 (in progress), F-004, F-005, F-043
**Deliverables:**

- [x] Procedural SFX with positional playback (`cdaa52c`, 2026-06-07)
- [ ] Engine thrust loop ([IMP-007](IMPROVEMENTS.md#imp-007-engine-thrust-loop-sfx))
- [ ] Day/night cycle (F-004)
- [ ] Weather: wind drift, storm fog (F-005)
- [ ] Post-FX: vignette, mild bloom (F-043)

**Acceptance:** Every gameplay event has audio feedback, and the sky, lighting and weather vary without breaking the flat-shaded aesthetic.

---

## Phase 6 — Extras

**Goal:** Open-ended additions, scheduled by demand.
**Status:** Not started
**Features delivered:** F-044, F-045, F-046
**Deliverables:**

- [ ] Leaderboard persistence (F-044)
- [ ] Cockpit camera (F-045)
- [ ] Replay recording (F-046)

**Acceptance:** Per feature; no phase-level gate.

---

# Feature track

## Slice A — Visual and AI polish

**Goal:** Damaged enemies read as damaged and behave like it.
**Status:** Complete (2026-06-03)
**Features delivered:** F-017
**Deliverables:**

- [x] Damage smoke below 50% hull; retreat AI below 40% for Fighter/Bomber/Seeder (`c99848f`)

**Acceptance:** Damaged enemies trail smoke and retreat; the Carrier smokes but fights to the death.

---

## Slice B — Infection and new weapons

**Goal:** Infection mechanic and the shield-stripping weapon set.
**Status:** Complete (2026-06-04)
**Features delivered:** F-010 (energy pool), F-011 (Shield and Infectious missiles), F-015
**Deliverables:**

- [x] B.1 — Shared primary-weapon energy pool (`d0ed9f2`)
- [x] B.2 — Shield Laser primary (`4f06aee`; later replaced by the laser pair, `d0cc533`)
- [x] B.3 — Shield Missile (`3c69e83`)
- [x] B.4 — Infection state machine for Fighter/Bomber (`44b5d99`)
- [x] B.5 — Infectious Missile and infected-ship behaviour (`0b7d64c`)

**Acceptance:** A shield-stripped Fighter or Bomber hit by an Infectious Missile fights for the player.

---

## Weapons redesign — Typed laser pair

**Goal:** Make tactical weapon switching the core combat decision.
**Status:** Complete (2026-06-22)
**Features delivered:** F-010, F-029
**Deliverables:**

- [x] Hull/Shield/Neutral damage types; red/blue laser pair; charged shot; two-step spread (`d0cc533`)
- [ ] Follow-up polish ([IMP-006](IMPROVEMENTS.md#imp-006-laser-pair-polish-dedicated-sfx-and-crosshair-colour), [IMP-005](IMPROVEMENTS.md#imp-005-consolidate-weapon-stats-into-a-weaponspec-table))

**Acceptance:** Wrong-type hits do 25% damage, and favoured-type DPS matches the 100 DPS TTK reference.

---

## Slice C — Base Defence mode

**Goal:** Asymmetric defender vs invader mode per [project-status/base_mode_v2.md](project-status/base_mode_v2.md).
**Status:** In progress
**Features delivered:** F-020, F-021, F-022, F-023, F-024, F-025, F-026, F-027
**Deliverables:**

- [x] C.1 — Game mode picker and Base Defence shell (`9380ffb`, 2026-06-11)
- [x] C.2 — Friendly base defenders, landing-pad repair/rearm, radar station entity (F-021, 2026-10-15)
- [x] C.3 — Lander entity, first cut: grounded, spawns drones (`49f005f`, 2026-06-11)
- [ ] C.3 follow-ups — fighter, collector and builder production; wreckage node; hardpoints; descent
- [ ] C.4 — Radar network (F-023)
- [ ] C.5 — Help calls (F-024)
- [ ] C.6 — Infection v2 (F-025)
- [x] C.7 — Win/loss detection and session-result screen (`cd1bcfe`, 2026-06-11)
- [ ] C.8 — Difficulty presets and base/Lander balancing (F-027, [BUG-014](BUGS.md#bug-014-lander-damage-budget-is-smaller-than-the-carriers-despite-design-intent))

**Acceptance:** Base mode can be won by clearing all Landers and retaking infected bases, and lost by losing every friendly base or the player. Radar coverage tracks the surviving network.

**Open design questions** (resolved when the relevant sub-slice lands):

- On a toroidal world with two front lines, is the player an operator of an autonomous faction, or a combatant in their own right?
- Should infected ships be recruitable at friendly bases, or stay disposable?

---

# Tooling track (`terra-siege-inspect`)

## Inspector foundation

**Goal:** Separate inspector binary with a pluggable tool registry.
**Status:** Complete (2026-06-03)
**Features delivered:** F-033 (shared mesh lib)
**Deliverables:**

- [x] OBJ pipeline, 32-colour palette, doctest harness (`47f8d74`, 2026-06-01); all entities migrated (`e133893`, 2026-06-02)
- [x] `terra-siege-inspect` binary, `Tool` interface, VertexTool, round-trip save (`0522dd7`, 2026-06-02)
- [x] Blender-style orbit camera (`b53da80`, 2026-06-03)

**Acceptance:** The inspector opens any entity OBJ, drags vertices, and saves without disturbing non-vertex lines.

---

## Inspector Phase A — Workspace and file ops

**Status:** Complete (2026-06-04, `9ac375e`)
**Features delivered:** F-035
**Acceptance:** Boots without an OBJ; open, save, save-as, close, drag-and-drop, recent files, unsaved-changes prompt.

## Inspector Phase B — Vertex editing depth

**Status:** In progress (first cut 2026-06-07, `92d0bbb`)
**Features delivered:** F-036
**Deliverables:**

- [x] Multi-select, box select, undo/redo, snap, numeric input
- [ ] Mirror, weld, split ([IMP-009](IMPROVEMENTS.md#imp-009-inspector-phase-b-mirror-weld-split))

**Acceptance:** Non-trivial meshes can be authored without leaving the inspector.

## Inspector Phase C — Materials and palette

**Status:** Complete (2026-06-11, `e157e0d`)
**Features delivered:** F-037
**Acceptance:** Faces can be recoloured from the 32-colour swatch, with an eyedropper, and saved.

## Inspector Phase D — Topology

**Status:** Complete (2026-06-11, `48ed9f8`)
**Features delivered:** F-038
**Acceptance:** Vertices and faces can be added and deleted, and winding flipped, with a full-mesh save.

## Inspector Phase E — Primitives

**Status:** Complete (2026-06-07, `771b6c1`)
**Features delivered:** F-039
**Acceptance:** Cube, plane, cylinder, cone and icosphere insert, with undo shared across tools.

## Inspector Phase F.1–F.5 — Sidecar profile tools

**Status:** Complete (2026-06-07)
**Features delivered:** F-034, F-040
**Deliverables:**

- [x] F.1 — Sidecar foundation, menu bar, Controls overlay (`a5660d7`, 2026-06-06)
- [x] F.2 — Identity/Hull/Shields tools and game-side `EntityProfileRegistry` (`c746a0a`, 2026-06-06)
- [x] F.3 — Weapons and Hardpoints tools; all spawns read sidecars (`814fa1e`, 2026-06-06)
- [x] F.4 / F.5 — AI and FX tools (`9f7e5c8`, 2026-06-07)

**Acceptance:** Every entity's gameplay profile can be edited in the inspector and takes effect on the next in-game spawn.

## Inspector Phase F.6 — Resources

**Status:** Not started
**Features delivered:** F-041
**Acceptance:** Yield on death, base capacity and production arrays can be edited per entity. Needed only for Slice C.

## Inspector Phase G — Workflow polish

**Status:** Not started
**Features delivered:** F-042
**Acceptance:** Scheduled by demand; no phase-level gate.
