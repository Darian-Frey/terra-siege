# Features

Capabilities terra-siege offers or intends to offer, with priorities and acceptance criteria.
MoSCoW priorities: Must / Should / Could / Won't.
Status vocabulary: Not started | In progress | Complete | Withdrawn.
IDs are append-only; withdrawn features keep their ID with `Status: Withdrawn`.

Feature IDs F-001..F-046 were assigned retroactively on 2026-10-15 when the project adopted the development documentation standard (see [D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). Completion dates and commits come from `git log`.

## Target users

Players who want a modern take on the 1988 Argonaut game *Virus*: flat-shaded low-poly terrain, Newtonian "tilt and burn" flight and planetary defence. Also the solo developer, who uses the `terra-siege-inspect` tool to author entity meshes and their gameplay profiles.

## Out of scope

- Texture-mapped or photoreal rendering. Flat-shaded polygons only; low-poly is the aesthetic ([D-013](DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures)).
- Mesh formats other than OBJ: no GLB, FBX or STL import ([D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth)).
- A general-purpose modeller. `terra-siege-inspect` is a focused OBJ and sidecar editor, not a Blender replacement.
- Multiple player craft or switchable flight models ([D-005](DECISIONS.md#d-005-single-newtonian-flight-model-one-craft)).
- Multiplayer and networking.
- Affiliation with, or use of assets from, Argonaut Software or any *Virus* rights holder.

## Features

### World

### F-001 Procedural toroidal terrain

**Priority:** Must
**Status:** Complete (2026-05-03, `26495c8`)
**Acceptance:**

- 1025² heightmap from tileable Perlin gradient fBM, with a ridged-multifractal mountain layer and coupled 2D domain warp
- The world wraps seamlessly on both axes: chunks render at the copy nearest the camera, with no visible edge
- `Planet::heightAt()` accepts any coordinate, including negative or out-of-range values
- Sea level holds about 30% ocean, plus inland lakes flood-filled at depressions
- The F5 reseed (DEV_MODE) regenerates deterministically from a seed

**Notes:** Generator choice in [D-006](DECISIONS.md#d-006-tileable-perlin-gradient-fbm-terrain). Wrap model in [D-007](DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world).

### F-002 Atmospheric terrain decorations

**Priority:** Should
**Status:** Complete (2026-06-08, `bdc63e5`; wrap-aware rendering 2026-10-15, `7e846c3`)
**Acceptance:**

- Noise-mask forests on grassland only, plus biome-weighted rocks, antennas and crash sites
- Placement is deterministic for a given terrain seed
- Visual only: no collision, AI or entity-pool slots
- Distance-culled at 800 m and drawn at the toroidal copy nearest the camera

### F-003 Rivers

**Priority:** Could
**Status:** Not started
**Acceptance:**

- Rivers carve channels downhill from mountain sources to ocean or lake
- Rivers render with their own water colour band

**Notes:** A first implementation shipped in Phase 1.5 (`68e0d12`) and was disabled when the terrain was rebuilt (`26495c8`). `carveRivers()`, `flowDownhill()` and `carveChannel()` are still in [Heightmap.cpp](src/world/Heightmap.cpp) but are never called. See [BUG-003](BUGS.md#bug-003-documentation-claims-rivers-ship-but-river-carving-is-disabled).

### F-004 Day/night cycle

**Priority:** Could
**Status:** Not started
**Acceptance:**

- The sun direction and the sky/fog colour animate over a configurable period
- The flat-shaded lighting model is kept (one directional sun, ambient around 0.45)

### F-005 Weather

**Priority:** Could
**Status:** Not started
**Acceptance:**

- Wind drift affects the player and projectiles
- Storm fog reduces visibility range

### Flight and camera

### F-006 Newtonian hovercraft flight

**Priority:** Must
**Status:** Complete (2026-05-03, `b3f1e6d` + `be7724e`)
**Acceptance:**

- Single "tilt and burn" model: thrust along the craft's local up axis against constant world gravity
- Physics runs at a fixed 120 Hz, independent of frame rate
- A regenerating thrust charge drains while burning and recharges while coasting. The flight ceiling cuts thrust above the AGL limit
- Landing, crash and lost-power death states (the wreck falls and explodes on impact)

**Notes:** [D-003](DECISIONS.md#d-003-fixed-120-hz-physics-timestep), [D-005](DECISIONS.md#d-005-single-newtonian-flight-model-one-craft), [D-008](DECISIONS.md#d-008-regenerating-thrust-charge-instead-of-fuel).

### F-007 Flight assist levels

**Priority:** Must
**Status:** Complete (2026-05-03, `b3f1e6d`)
**Acceptance:**

- Four levels (0 Raw, 1 Minimal, 2 Standard, 3 Full) with coefficients `Config::ASSIST_LEVEL_COEFFS`
- Level 1 auto-levels roll. Level 2 also damps pitch. Level 3 adds a terrain look-ahead pull-up
- F2 cycles levels in DEV_MODE

**Notes:** See [BUG-005](BUGS.md#bug-005-holding-q-or-e-to-select-a-laser-colour-suppresses-auto-level-roll).

### F-008 Ground shadow and exhaust particles

**Priority:** Should
**Status:** Complete (2026-05-03, `9660252`)
**Acceptance:**

- A shadow disc drapes over the terrain under the craft and fades with altitude
- Engine exhaust particles come from a pre-allocated 2000-slot pool with gravity and bounce flags

### F-009 Five-view camera system

**Priority:** Must
**Status:** In progress
**Acceptance:**

- Keys 1–5 select Chase, Velocity, Tactical (overhead, north up), ThreatLock and Classic
- Zoom is remembered per view
- All views clamp the camera above the terrain ([AV-004](ATTACK_VECTORS.md#av-004-camera-clips-below-terrain))
- ThreatLock rotates to keep the nearest enemy in frame

**Notes:** Delivered 2026-05-03 (`6dcf35f`). ThreatLock is still a stub that falls back to chase orientation. See [BUG-004](BUGS.md#bug-004-threatlock-camera-view-is-a-stub).

### Combat

### F-010 Primary weapons

**Priority:** Must
**Status:** Complete (2026-06-22, `d0cc533`)
**Acceptance:**

- Red/blue laser pair as the default primary: Q selects red (Hull-typed), E selects blue (Shield-typed)
- Holding LMB auto-fires. Holding Shift+LMB charges a heavy shot, fired on release
- Plasma (splash) and Beam Laser cycle with Tab
- Damage types Hull / Shield / Neutral. Wrong-type hits do 25% damage
- At the favoured damage type, laser DPS matches the 100 DPS reference used for TTK-derived enemy HP

**Notes:** Replaced the Cannon (`c041c64`) and the Shield Laser (`4f06aee`). [D-021](DECISIONS.md#d-021-typed-redblue-laser-pair-replaces-cannon-and-shield-laser). Deferred polish is in [IMP-006](IMPROVEMENTS.md#imp-006-laser-pair-polish-dedicated-sfx-and-crosshair-colour).

### F-011 Secondary weapons

**Priority:** Must
**Status:** Complete (2026-06-04, `0b7d64c`)
**Acceptance:**

- RMB fires and Z cycles: homing Missile, Cluster Missile, Depth Charge, Shield Missile, Infectious Missile
- Missiles have a proximity fuse
- Cluster submunitions split at `CLUSTER_SPLIT_DISTANCE` and spread across a swarm rather than converging on one target

**Notes:** The 120° submunition search cone in the original combat design was never implemented. Submunitions reacquire across a full sphere within `MISSILE_REACQUIRE_RANGE`. See [BUG-016](BUGS.md#bug-016-cluster-submunition-120-search-cone-documented-but-not-implemented) and [AV-007](ATTACK_VECTORS.md#av-007-cluster-submunitions-converge-on-one-target).

### F-012 Special weapons and auto turret

**Priority:** Should
**Status:** Complete (2026-05-31, `1d97de2`)
**Acceptance:**

- F fires and X cycles: EMP (stuns entities in radius) and Shield Booster
- T toggles the Auto Turret, which engages hostiles automatically

### F-013 Pre-flight loadout selection

**Priority:** Should
**Status:** Complete (2026-06-01, `932eb19`)
**Acceptance:**

- A loadout screen between the main menu and play selects the weapons
- The selection is saved to settings and offered again after game over

### F-014 Player directional shields

**Priority:** Must
**Status:** Complete (2026-05-15, `fec0f04`; impact-cap visuals 2026-06-05, `784f027`)
**Acceptance:**

- Four sectors (front, rear, left, right) with independent HP and regeneration
- A hit drains the sector facing the attacker before any damage reaches the hull
- A shield pie HUD shows each sector's state
- Localised impact caps appear only when a shield is hit

### F-015 Ship infection (v1)

**Priority:** Should
**Status:** Complete (2026-06-04, `44b5d99` + `0b7d64c`)
**Acceptance:**

- An Infectious Missile hit on a shield-stripped Fighter or Bomber flips it to the player's faction
- Infected ships bleed hull over time and are excluded from auto-aim
- A failed infection (shields still up) is a dud

**Notes:** Seeder and Carrier infection deferred. See [IMP-008](IMPROVEMENTS.md#imp-008-seeder-and-carrier-infection).

### Enemies and friendlies

### F-016 Enemy roster

**Priority:** Must
**Status:** Complete (2026-06-01, `667e52b`)
**Acceptance:**

- Drone (stand-off gunship swarm, global cap 16), Fighter, Seeder, Bomber, Carrier (4-sector shield boss) and Ground Tank
- Enemy HP derives from time-to-kill targets × 100 DPS reference ([D-014](DECISIONS.md#d-014-ttk-derived-enemy-hp))
- Fighter AI: IDLE → PURSUE → ATTACK → EVADE. Bomber adds STRAFE_FRIENDLY
- Per-entity stats come from sidecar profiles (F-034)

### F-017 Damage smoke and retreat AI

**Priority:** Should
**Status:** Complete (2026-06-03, `c99848f`)
**Acceptance:**

- Enemies trail smoke below 50% hull
- Fighter, Bomber and Seeder retreat to their spawn point below 40% hull
- The Carrier smokes but never retreats

### F-018 Friendly units and collector economy

**Priority:** Must
**Status:** Complete (2026-06-01, `092cb14` + `667e52b`; world-wide placement 2026-10-15, `7e846c3`)
**Acceptance:**

- Collector, Repair Station, Radar Booster and Base (with an auto-turret)
- Collectors run a delivery loop to the Base that scores points
- Friendlies other than the home Base and its collectors spawn on grassland across the whole world
- Losing all friendlies ends the session

### Game modes

### F-019 Wave mode

**Priority:** Must
**Status:** Complete (2026-05-06, `5763f75`)
**Acceptance:**

- The wave manager spawns on a stagger, with intermissions between waves
- The wave table introduces each enemy type on its own before mixing them

### F-020 Base Defence mode

**Priority:** Must
**Status:** In progress
**Acceptance:**

- A menu picker selects Wave or Base mode, and the choice persists across launches
- Asymmetric defender vs invader: pre-placed friendly bases against grounded enemy Landers
- Win: all Landers cleared and all infected bases retaken. Lose: all friendly bases lost, or the player destroyed
- Built from sub-features F-021 to F-027

**Notes:** Design in [project-status/base_mode_v2.md](project-status/base_mode_v2.md). Shell delivered 2026-06-11 (`9380ffb`). Slice C in [ROADMAP.md](ROADMAP.md).

### F-021 Friendly base and radar station entities (C.2)

**Priority:** Must
**Status:** Complete (2026-10-15)
**Acceptance:**

- A base that has been hit in the last `BASE_UNDER_ATTACK_WINDOW` seconds, or has a hostile within `BASE_ALERT_RANGE`, launches allied Fighter defenders, up to `BASE_DEFENDER_MAX` per base. Defenders engage hostiles within `BASE_DEFENDER_LEASH` of home and land (despawn) when none remain
- Each base stands on a raised 36 m landing platform (`BASE_PAD_HALF_SIZE`, `BASE_PAD_HEIGHT`) on levelled terrain with decorations cleared, with the control tower at its NE corner. The south and east sides get a ramp wherever they stand above the adjacent ground. Collectors reach and leave the platform only by those ramps, detouring around the platform when needed. A player docked anywhere on the platform (landed, or hovering below `BASE_PAD_HOVER_AGL` at under `BASE_PAD_HOVER_SPEED`) is repaired continuously and fully rearmed after `BASE_PAD_REARM_TIME`. The HUD shows ON PAD
- A radar station entity exists as a network node, ready for C.4
- Defenders and pads work in both game modes. Base-mode placement uses bases and radar stations, with no Repair Stations or Radar Boosters
- Enemy fire can damage friendly structures and allied ships ([BUG-026](BUGS.md#bug-026-enemy-fire-never-damages-friendly-structures-or-allied-ships))
- Fighters, Drones and Tanks attack a nearby friendly instead of the player when it is closer (`ENEMY_FRIENDLY_PRIORITY`). A fixed share of them (`ENEMY_RAIDER_PERCENT`, "raiders") always prefer friendlies in range. Landers deploy drones when any friendly is in range

**Notes:** [D-030](DECISIONS.md#d-030-base-defenders-reuse-the-allied-fighter-path-pads-replace-repair-stations-in-base-mode). Radar stations use `radar_station.obj`: a red/white lattice tower with a white radome, about 21 m tall, 476 tris (above the 200-tri guideline for the lattice silhouette; only a handful exist at once). Wave mode also places `WAVE_MODE_EXTRA_BASE_COUNT` extra bases and `WAVE_MODE_RADAR_STATION_COUNT` radar towers across the map. Losing all bases (not every friendly) ends a Base-mode round.

### F-022 Lander entity (C.3)

**Priority:** Must
**Status:** In progress
**Acceptance:**

- Landers start grounded and act as enemy bases
- They produce drones, fighters, collectors and builders
- They drop a wreckage node on death

**Notes:** First cut 2026-06-11 (`49f005f`): grounded, spawns drones, 4-sector shields. The other production types, wreckage, hardpoints and descent are deferred.

### F-023 Radar network (C.4)

**Priority:** Must
**Status:** Not started
**Acceptance:**

- A node connectivity graph shares detection across connected radar nodes
- HUD areas the network doesn't reach are fogged
- The player keeps a short-range personal radar throughout
- Coverage degrades as nodes are lost

**Notes:** Interacts with [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range) and [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally).

### F-024 Help calls (C.5)

**Priority:** Should
**Status:** Not started
**Acceptance:**

- A damaged node broadcasts a help call, which propagates along the radar network
- A HUD indicator shows the source of the call

### F-025 Infection v2 (C.6)

**Priority:** Must
**Status:** Not started
**Acceptance:**

- Infection requires stripped shields and hull ≤30%
- It works in both directions (player infects enemies, invaders infect bases)
- Bases can be re-flipped freely

### F-026 Win/loss detection and session-result screen (C.7)

**Priority:** Must
**Status:** Complete (2026-06-11, `cd1bcfe`)
**Acceptance:**

- `SessionResult` is Victory, PlayerDestroyed or FriendliesLost
- The world freezes and a shared result overlay shows round stats

### F-027 Difficulty presets (C.8)

**Priority:** Should
**Status:** Not started
**Acceptance:**

- Four presets (Veteran, Pilot, Recruit, Commander) chosen at game start
- Each preset scales flight assist, enemy aggression, shield recharge and pickup rates
- Base mode scales base and Lander counts (Easy 3 / Normal 5 / Hard 7 Landers)

**Notes:** The multiplier tables exist in `Config.hpp` (`AGGRESSION_MULT`, `RECHARGE_MULT`, `PICKUP_RATE`, `PRESET_ASSIST`) but nothing reads them. See [BUG-006](BUGS.md#bug-006-difficulty-presets-documented-as-shipped-but-not-implemented).

### HUD

### F-028 Tactical radar

**Priority:** Must
**Status:** Complete (2026-06-01, `6d3a339`; pulse sweep 2026-10-15, `50506fc`)
**Acceptance:**

- Heading-up disc with IFF-coloured blip shapes per type (radar towers use a radome-on-mast icon) and an altitude strip with altitude tint
- Old-style pulse: a ring expands every second, blips are painted when the ring reaches them, and they fade until the next pulse
- Radar towers pulse `RADAR_STATION_PULSE_OFFSET` (0.5 s) after the player. Each ring (`RADAR_STATION_RANGE`, 300 m) is drawn on the player's disc at true scale, clipped to the disc, and fades over its last `RADAR_STATION_PULSE_FADE` (50 m). It is visual only until C.4 lets tower pulses reveal contacts
- Real-time missile-warning arrows, Carrier jamming jitter, velocity arrows and ghost blips (32-slot pool)
- Range 300 m, or 500 m with a live Radar Booster

**Notes:** Known issues: [BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range), [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally), [BUG-008](BUGS.md#bug-008-landers-and-other-unmapped-types-show-as-fighter-blips), [BUG-009](BUGS.md#bug-009-infected-ships-still-show-as-hostile-on-radar).

### F-029 Flight, weapon and shield HUD

**Priority:** Must
**Status:** Complete (2026-06-22, `d0cc533`)
**Acceptance:**

- Toggleable amber wireframe flight HUD: horizon, flight-path vector, pitch ladder, heading/speed/AGL tapes
- Weapon block shows the active primary (RED/BLUE LASER), battery and charge
- Shield pie, thrust charge, wave/score/Lander counters

### Audio, settings and tools

### F-030 Procedural positional audio

**Priority:** Should
**Status:** In progress
**Acceptance:**

- Synthesised SFX (layered saw, sub and noise, bit-crush, soft clip, per-shot pitch variance), with no audio assets
- Camera-relative pan and distance falloff, using a 6-alias voice pool
- A master volume setting
- A continuous engine thrust loop

**Notes:** First cut 2026-06-07 (`cdaa52c`). The engine loop is missing. See [IMP-007](IMPROVEMENTS.md#imp-007-engine-thrust-loop-sfx).

### F-031 Menus and persistent settings

**Priority:** Must
**Status:** Complete (2026-05-06, `c041c64`)
**Acceptance:**

- Main, pause (Esc/P) and settings overlays
- Settings persist to `~/.config/terra-siege/settings.cfg`: invert pitch/yaw, default camera view, wireframe HUD, master volume, last game mode, loadout

### F-032 DEV_MODE developer tools

**Priority:** Should
**Status:** Complete (2026-05-07, `2bd918c`)
**Acceptance:**

- Compiled only with `-DDEV_MODE=ON`, with zero release overhead
- F1 free-roam camera, F2 assist cycle, F3 god mode, F4 flight recorder (120 Hz CSV), F5 reseed, F6 heightmap dump, F7 skip wave, `[` `]` `\` debug stepper

### Content pipeline

### F-033 OBJ mesh pipeline with 32-colour palette

**Priority:** Must
**Status:** Complete (2026-06-02, `e133893`)
**Acceptance:**

- Every entity mesh loads from `assets/meshes/*.obj` into a `MeshRegistry` keyed by `EntityType`
- Materials `c00`..`c31` map to a fixed 32-colour palette
- Face normals are recomputed at load
- Procedural fallback when an OBJ is missing

**Notes:** [D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth), [D-010](DECISIONS.md#d-010-32-colour-palette-via-obj-material-names). Spec in [project-status/3d_assets.md](project-status/3d_assets.md).

### F-034 Sidecar entity profiles

**Priority:** Should
**Status:** Complete (2026-06-07, `9f7e5c8`)
**Acceptance:**

- One `*.meta.json` per OBJ: identity, hull, shields, weapons, hardpoints, AI and FX
- The game reads profiles through `EntityProfileRegistry` on each spawn, so edits apply on the next spawn
- Missing or malformed sidecars fall back to `Config.hpp` values

**Notes:** [D-011](DECISIONS.md#d-011-sidecar-entity-profiles-by-default-registry-override-later). Retirement of the fallbacks is in [IMP-004](IMPROVEMENTS.md#imp-004-retire-confighpp-per-entity-fallbacks-once-sidecars-are-authoritative).

### Inspector (`terra-siege-inspect`)

### F-035 Inspector workspace and file operations (Phase A)

**Priority:** Must
**Status:** Complete (2026-06-04, `9ac375e`)
**Acceptance:**

- Starts without an OBJ. Open, Save, Save As and Close, drag-and-drop, recent files
- Unsaved-changes prompt, menu bar, Help → Controls overlay
- Blender-style orbit camera (RMB orbit, Shift+RMB pan, wheel zoom, F frame)

### F-036 Vertex editing depth (Phase B)

**Priority:** Should
**Status:** In progress
**Acceptance:**

- Multi-select, box select, undo/redo, snap to grid and to vertex, numeric input
- Mirror, vertex weld and vertex split

**Notes:** First cut 2026-06-07 (`92d0bbb`). Mirror, weld and split are deferred. See [IMP-009](IMPROVEMENTS.md#imp-009-inspector-phase-b-mirror-weld-split).

### F-037 Materials and palette painting (Phase C)

**Priority:** Should
**Status:** Complete (2026-06-11, `e157e0d`)
**Acceptance:**

- 32-colour swatch panel, face pick and paint, multi-select and eyedropper
- Saves material assignments without disturbing geometry

### F-038 Topology editing (Phase D)

**Priority:** Should
**Status:** Complete (2026-06-11, `48ed9f8`)
**Acceptance:**

- Add and delete vertices and faces, flip winding
- A full mesh rewrite on save, because topology changes break byte-exact round-trip

### F-039 Primitive insertion (Phase E)

**Priority:** Should
**Status:** Complete (2026-06-07, `771b6c1`)
**Acceptance:**

- Insert cube, plane, cylinder, cone and icosphere
- Undo is shared across tools (32 full-mesh snapshots)

### F-040 Sidecar profile tools (Phases F.1–F.5)

**Priority:** Should
**Status:** Complete (2026-06-07, `9f7e5c8`)
**Acceptance:**

- Profile, Identity, Hull, Shields, Weapons, Hardpoints, AI and FX tools edit `*.meta.json` with schema validation
- DOM-preserving save, so unknown fields survive round-trips

### F-041 Resources tool (Phase F.6)

**Priority:** Should
**Status:** Not started
**Acceptance:**

- Edit yield on death, base capacity and the production array per entity

**Notes:** Needed only for Slice C asset authoring.

### F-042 Inspector workflow polish (Phase G)

**Priority:** Could
**Status:** Not started
**Acceptance:**

- View modes, orthographic views, normals overlay, asset browser, live file watch, in-game hotswap, diff against HEAD

### Engine extras

### F-043 Post-processing effects

**Priority:** Could
**Status:** Not started
**Acceptance:**

- Vignette and mild bloom, consistent with the 1992-on-powerful-hardware aesthetic ([D-013](DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures))

**Notes:** GLSL stubs exist in `assets/shaders/` but are not wired in.

### F-044 Leaderboard persistence

**Priority:** Could
**Status:** Not started
**Acceptance:**

- Session scores are stored locally and shown on the result screen

### F-045 Cockpit camera

**Priority:** Could
**Status:** Not started
**Acceptance:**

- A first-person camera view from the cockpit

### F-046 Replay recording

**Priority:** Could
**Status:** Not started
**Acceptance:**

- Inputs recorded at physics-tick granularity replay a session deterministically

**Notes:** Relies on the fixed timestep ([D-003](DECISIONS.md#d-003-fixed-120-hz-physics-timestep)). Terrain seed and wave RNG would also need recording.

---

## Candidate features (uncommitted)

- Windows build (MinGW-w64 or MSVC). The codebase avoids platform `#ifdef`s in game logic.
- Launch pads: spawn parked on a pad rather than mid-air; landing legs that deploy automatically near a pad at low AGL; thrust charge refilled while landed on a pad. Gated on launch pads existing as a queryable terrain entity. There is a TODO hook at the landing branch of `Player::applyPhysics()`. Natural order: pad placement → spawn on pad → recharge on pad → landing-leg visuals.
- Helicopter mode, orbit drones and the resource system (see [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md) §§4–8)
- Recruiting infected ships at friendly bases (open design question, Slice B)
- Player-ship infection (open design question; probably undercuts the theme)
