# Changelog

All notable changes to this project will be documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The project has no tagged releases yet. `[0.1.0]` is a retroactive baseline: the state at the last commit before the documentation standard was adopted ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). It matches the version in `CMakeLists.txt` and summarises earlier history by phase. Commit hashes refer to `git log`.

## [Unreleased]

### Added
- Guided-missile model (`missile.obj`): grey rear body with a black band, white nose section, black seeker, four swept fins. Used for the homing Missile, the Cluster carrier (orange) and cluster submunitions (orange, smaller), oriented along their flight path
- Slice C C.4 (F-023), the radar network:
  - Bases, radar towers and boosters link within 400 m. Clusters containing a base are online.
  - Relay chains of towers every 350 m join each base to its two nearest bases, in both modes.
  - Contacts show only inside your 300 m or an online tower's 300 m. Beyond 300 m they appear as rim markers, painted by tower pulses.
  - Offline towers go dark ([D-032](DECISIONS.md#d-032-radar-network-relay-chains-base-anchored-clusters-pulse-painted-rim-markers))
- Base control tower and turret doubled in size (`BASE_TOWER_SCALE`) so they hold their own next to the radar tower
- Every base now has its own radar tower on the platform's compass-NW corner, opposite the control tower (NE). Base layout (towers and east ramp) now follows the compass via `BASE_EAST_SIGN`, working around the mirrored 3D view ([BUG-028](BUGS.md#bug-028-3d-view-is-mirrored-eastwest-relative-to-the-compass-and-heading))
- Radar tower model (`radar_station.obj` + sidecar): a red/white lattice tower with a deck and white radome. It has its own radar blip, a green radome-on-mast icon. Radar towers pulse 500 ms after the player's pulse; each 300 m ring is drawn on the player's disc at true scale and fades out over its last 50 m. Wave mode now also places 3 extra bases with landing platforms, and 4 radar towers, across the map
- Slice C C.2 (F-021): bases under attack launch allied Fighter defenders (up to 3 each), which are leashed to their base and land when idle. Bases stand on raised 36 m landing platforms, with the control tower in the NE corner and ramps on the south and east sides that Collectors use to drive on and off. Landing or hovering low anywhere on the platform repairs and rearms the player (HUD: ON PAD). Both work in every mode. Fighters, Drones and Tanks now attack nearby friendlies (about a third of them are raiders that prefer friendlies), and Landers besiege nearby bases. New radar station entity as the network node. Base mode now places 8 bases and 6 radar stations ([D-030](DECISIONS.md#d-030-base-defenders-reuse-the-allied-fighter-path-pads-replace-repair-stations-in-base-mode))
- Old-style pulse radar: a ring sweeps out every second and paints contacts, which fade until the next pulse (F-028, [D-026](DECISIONS.md#d-026-old-style-pulse-radar), `50506fc`)
- `Planet::nearestCopy()` and `Planet::isGrassland()`; `BIOME_*` band thresholds centralised in `Config.hpp` (`7e846c3`)
- README screenshot gallery in `docs/screenshots/`
- Project documentation to the development documentation standard: README status header, FEATURES, DECISIONS, ARCHITECTURE, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, MIT LICENSE ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard))

### Changed
- Base Defence mode is lost when every base falls, not every friendly unit
- `base.obj` now holds only the control tower, centred on its own origin. The raised platform, ramps and corner lights are drawn procedurally
- Friendly-structure checks consolidated into `isFriendlyStructure()` (Entity.hpp)
- Friendlies other than the home Base and its collectors now spawn on grassland across the whole world, not within 220 m of the player (F-018, [D-028](DECISIONS.md#d-028-friendlies-scattered-world-wide-on-grassland), `7e846c3`)
- ROADMAP and CLAUDE.md rewritten to the standard's format. Design references moved to `project-status/` ([D-002](DECISIONS.md#d-002-exempt-buildmd-and-specmd-keep-design-references-in-project-status))

### Removed
- Distance-based radar blink (`RADAR_BLINK_*`), replaced by pulse painting (`50506fc`)
- Stale duplicate of `game_modes_and_features.md` at the repo root

### Fixed
- The compass read mirrored against the view: the heading tape and debug heading now increase when turning right, and E is on the right when facing north ([BUG-028](BUGS.md#bug-028-3d-view-is-mirrored-eastwest-relative-to-the-compass-and-heading), [D-033](DECISIONS.md#d-033-world-x-points-west-compass-heading-is-yaw))
- The radar showed every entity on the map and one booster anywhere boosted range globally; both now follow network coverage ([BUG-001](BUGS.md#bug-001-radar-shows-every-entity-on-the-map-regardless-of-range), [BUG-002](BUGS.md#bug-002-one-radar-booster-anywhere-boosts-radar-range-globally))
- Base defenders never fired at drones circling them, because they couldn't turn tightly enough for the 16° fire cone. Allied ships now fire within about 45°. Bases also scramble defenders when a hostile is within 220 m, not only when hit, and the base turret range rises from 110 m to 160 m
- Gameplay and radar ignored the world wrap. After crossing a world edge, nearby radar towers had no blip or pulse, and back at base enemies, the base turret and defenders stopped engaging. The simulation is now re-centred on the player every tick ([BUG-007](BUGS.md#bug-007-ai-collision-and-radar-distances-ignore-the-toroidal-wrap))
- Rebuilding a terrain chunk (base aprons, DEV F5 reseed) crashed because of stale mesh ids after unload ([BUG-027](BUGS.md#bug-027-rebuilding-a-terrain-chunk-crashes-stale-mesh-ids-after-unload))
- Enemy fire never damaged friendly structures or allied ships, so friendlies were invulnerable and the "friendlies lost" defeat could never trigger ([BUG-026](BUGS.md#bug-026-enemy-fire-never-damages-friendly-structures-or-allied-ships))
- Infected ships showed as hostile (red) on radar ([BUG-009](BUGS.md#bug-009-infected-ships-still-show-as-hostile-on-radar))
- Fighters and Bombers opened fire from well beyond their weapons' reach, and could stay latched in ATTACK at any distance. They now fire only within 90% of their projectile range ([BUG-025](BUGS.md#bug-025-fighters-and-bombers-open-fire-beyond-their-weapons-reach))
- Remaining HUD overlaps: nose crosshair and FPV over the top banners, DEV hotkey labels over the controls hints, Classic-view compass rose over the radar ([BUG-022](BUGS.md#bug-022-nose-crosshair-can-draw-over-the-heading-tape-and-wave-banners), [BUG-023](BUGS.md#bug-023-dev-hotkey-labels-overlap-the-controls-hint-text), [BUG-024](BUGS.md#bug-024-classic-view-compass-rose-overlaps-the-radar))
- Overlapping HUD elements: weapon battery bar over the weapon name, SHLD caption and weapon panel under the controls bar, pitch ladder through the top banners, speed-tape labels off-screen ([BUG-021](BUGS.md#bug-021-overlapping-hud-elements-in-the-bottom-left-cluster-pitch-ladder-and-speed-tape))
- In-game controls bar showed stale bindings; FPS counter and telemetry panel appeared in release builds ([BUG-019](BUGS.md#bug-019-in-game-controls-bar-shows-stale-bindings), [BUG-020](BUGS.md#bug-020-debug-info-panel-and-fps-counter-appear-in-release-builds))
- Trees, decorations and entities vanished after crossing a world edge; they now render at the toroidal copy nearest the camera ([AV-005](ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap), `7e846c3`)
- Documentation drift: rivers, difficulty presets, spatial grid, constants and archive terrain status ([BUG-003](BUGS.md#bug-003-documentation-claims-rivers-ship-but-river-carving-is-disabled), [BUG-006](BUGS.md#bug-006-difficulty-presets-documented-as-shipped-but-not-implemented), [BUG-013](BUGS.md#bug-013-claudemd-key-constants-table-drifted-from-confighpp), [BUG-017](BUGS.md#bug-017-documentation-describes-a-spatial-grid-and-module-layout-that-dont-exist), [BUG-018](BUGS.md#bug-018-archived-design-docs-claim-the-terrain-rebuild-never-shipped))

---

## [0.1.0] — 2026-06-22

Retroactive baseline. Development history 2026-03-31 to 2026-06-22, by phase.

### Added
- **Phase 1 / 1.5** (2026-03-31 to 2026-04-02): CMake + raylib scaffold, 120 Hz fixed-timestep loop, flat-shaded chunked terrain, 1025² heightmap with lakes ([D-003](DECISIONS.md#d-003-fixed-120-hz-physics-timestep), [D-004](DECISIONS.md#d-004-c17-raylib-50-and-a-single-cmake-build-with-fetchcontent))
- **Phase 2** (to 2026-05-03):
  - Single Newtonian "tilt and burn" flight with regenerating thrust charge and four flight-assist levels (F-006, F-007)
  - Tileable Perlin fBM toroidal terrain (F-001)
  - Five-view camera with wireframe flight HUD (F-009)
  - Ground shadow, 2000-slot particle pool and exhaust (F-008)
- **Phase 3** (2026-05-06 to 2026-06-01):
  - Entity pools with TTK-derived HP ([D-014](DECISIONS.md#d-014-ttk-derived-enemy-hp))
  - Enemy roster: Fighter, Drone, Seeder, Ground Turret/Tank, Bomber, Carrier (F-016)
  - Weapon roster: Plasma, Beam, Missile, Cluster, Depth Charge, EMP, Shield Booster, Auto Turret (F-011, F-012)
  - Wave manager (F-019) and pre-flight loadout (F-013)
  - Friendly units and collector economy with the Base (F-018)
  - Menus and persistent settings (F-031); DEV_MODE tools (F-032)
- **Phase 3.5** (2026-06-08): biome-aware forests, rocks, antennas and crash sites (F-002)
- **Phase 4, partial:** player directional shields (2026-05-15) and impact-cap visuals (2026-06-05) (F-014); radar tiers 1–3 (2026-05-06 to 2026-06-01) (F-028)
- **Phase 5, first cut** (2026-06-07): procedural positional SFX (F-030, [D-022](DECISIONS.md#d-022-procedural-in-engine-sfx-synthesis))
- **OBJ pipeline** (2026-06-01 to 2026-06-02): every entity mesh as OBJ with a 32-colour palette, and a doctest harness (F-033, [D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth))
- **Inspector** (2026-06-02 to 2026-06-11): `terra-siege-inspect` with phases A, B (first cut), C, D, E and F.1–F.5; game-side `EntityProfileRegistry` (F-035..F-040, [D-016](DECISIONS.md#d-016-inspector-as-a-separate-binary-with-a-tool-registry))
- **Slice A** (2026-06-03): damage smoke and retreat AI (F-017)
- **Slice B** (2026-06-04): shared energy pool, Shield Missile, Infectious Missile and ship infection (F-015)
- **Slice C, partial** (2026-06-11): game mode picker and Base Defence shell, Lander entity (first cut), win/loss session-result screen (F-020, F-022, F-026)

### Changed
- Flight rebuilt as a single Newtonian model; finite fuel replaced by thrust charge (2026-05-03, [D-005](DECISIONS.md#d-005-single-newtonian-flight-model-one-craft), [D-008](DECISIONS.md#d-008-regenerating-thrust-charge-instead-of-fuel))
- Terrain: Diamond-Square → Fourier sine synthesis → tileable Perlin fBM, all on 2026-05-03 ([D-006](DECISIONS.md#d-006-tileable-perlin-gradient-fbm-terrain))
- Drones reworked from a kamikaze swarm to stand-off gunships with a global cap of 16 (2026-06-06, [D-025](DECISIONS.md#d-025-drones-as-a-stand-off-gunship-swarm-with-a-global-cap))
- Ground Turret became a moving tank (2026-06-01)
- Default primary weapon became the red/blue typed laser pair with charged shot; Q/E now select laser colour instead of rolling (2026-06-22, F-010, [D-021](DECISIONS.md#d-021-typed-redblue-laser-pair-replaces-cannon-and-shield-laser))

### Removed
- Arcade/Classic dual flight modes and five craft (`FlightMode`, `CraftType`) (2026-05-03)
- `diamondSquare()`, `smooth()`, `applyRadialFalloff()` (2026-05-03)
- River carving disabled; code retained (2026-05-03, F-003)
- Cannon and Shield Laser primaries (2026-06-22)

### Fixed
- Radar left/right mirroring; disc doubled in size (2026-06-01, `e912515`)
- HP bars billboard; missile proximity fuse stops orbiting near misses (2026-06-04, `ec6d66a`)
- Fighter EVADE state transitions (2026-06-04, `0b7d64c`)
