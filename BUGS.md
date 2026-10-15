# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8 of the documentation standard, bugs are **logged here when found, not silently fixed**. The author decides whether to fix immediately, defer, or leave alone.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

The register was created on 2026-10-15 ([D-001](DECISIONS.md#d-001-adopt-the-development-documentation-standard)). Entries found before that date were backfilled from commit messages, and their `Found` date is the commit date.

## Open

### BUG-004: ThreatLock camera view is a stub

**Status:** open
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `GameState::updateThreatLockCamera()`
**Severity:** medium
**Description.** Camera view 4 (ThreatLock) should rotate to keep the nearest enemy in frame. The target lookup was left as `void *target = nullptr` with a comment saying "Phase 3 will replace this". Phase 3 shipped but the lookup never did, so ThreatLock behaves like Chase with a slewed yaw. The old README and CLAUDE.md described it as working.
**Reproduction.** Press 4 during combat. The camera follows the ship's heading and never turns towards enemies.
**Notes.** `EntityManager::nearestEnemyDistanceInCone()` (added in `d0cc533`) or a similar nearest-hostile query would supply the target. Tracks F-009.

### BUG-005: Holding Q or E to select a laser colour suppresses auto-level roll

**Status:** open
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/entity/Player.cpp](src/entity/Player.cpp) `Player::applyFlightAssist()` (`bool rollInput = IsKeyDown(KEY_Q) || IsKeyDown(KEY_E);`)
**Severity:** low
**Description.** `d0cc533` retired Q/E as roll keys and reused them for laser-colour selection, but flight assist still treats Q/E as manual roll input. While either key is held, assist levels 1–3 stop auto-levelling roll.
**Reproduction.** With flight assist ≥1, bank the craft with the mouse, then hold Q. Roll stays put until Q is released.
**Notes.** Low impact because colour selection is normally a tap. Fix: base `rollInput` on mouse roll only, or remove it.

### BUG-008: Landers and other unmapped types show as Fighter blips

**Status:** open
**Found:** 2026-10-15 (radar review session)
**Location:** [src/hud/Radar.cpp](src/hud/Radar.cpp) `blipTypeFromEntity()`
**Severity:** low
**Description.** `blipTypeFromEntity()` has no case for `EntityType::Lander`, so it falls through to the default `BlipType::Fighter`. Landers, the stationary boss-tier enemy bases in Base mode, appear as small red triangles that look like fighters.
**Reproduction.** Start a Base Defence round and look for Lander contacts on the radar.
**Notes.** Add a Lander blip type, perhaps a larger hollow square.

### BUG-010: In-flight auto-aimed projectiles can still hit newly infected ships

**Status:** open
**Found:** 2026-06-04 (`0b7d64c`)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) projectile hit path
**Severity:** low
**Description.** Auto-aim weapons skip infected targets when acquiring, but a projectile already in flight towards a non-infected target can still hit an infected ship that crosses its path.
**Reproduction.** Not yet isolated. The `0b7d64c` commit message describes it as a "known small gap… flag if it shows up in playtest."
**Notes.** Could filter infected ships at hit time for player-owned auto-aim projectile kinds.

### BUG-012: Sidecar `deathExplosionScale` is read but never used

**Status:** open
**Found:** 2026-06-07 (`9f7e5c8`)
**Location:** [src/entity/Entity.hpp](src/entity/Entity.hpp) `deathExplosionScale`; kill-explosion call sites in [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp)
**Severity:** low
**Description.** The FX tool (F.5) lets authors set a per-entity death explosion scale, and the game caches it on the entity, but `emitKillExplosion` call sites ignore it. Edits in the inspector have no visible effect.
**Reproduction.** Set `fx.deathExplosionScale` to 3.0 in `fighter.meta.json` and kill a Fighter. The explosion is the default size.
**Notes.** Explicitly deferred in `9f7e5c8` ("not yet wired into emitKillExplosion call sites").

### BUG-014: Lander damage budget is smaller than the Carrier's despite design intent

**Status:** open
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/core/Config.hpp](src/core/Config.hpp) `LANDER_HULL_TOTAL`
**Severity:** low
**Description.** The comment says the Lander should be "significantly larger than Carrier", and the `49f005f` commit message calls it 60% larger. In fact it totals 2,400 (1,000 hull + 4 × 350 shield) against the Carrier's 2,500 (1,500 hull + 4 × 250 shield), and its raw hull is two-thirds of the Carrier's.
**Reproduction.** Arithmetic on `LANDER_*` and `CARRIER_*` / `TTK_CARRIER` constants.
**Notes.** Tuning decision for C.8 (F-027). Either raise `LANDER_HULL_TOTAL` or correct the stated intent.

### BUG-015: Radar range rings are labelled as weapon ranges they don't match

**Status:** open
**Found:** 2026-10-15 (radar review session)
**Location:** [src/hud/Radar.cpp](src/hud/Radar.cpp) `drawDisc()`; [src/core/Config.hpp](src/core/Config.hpp) `RADAR_INNER_RING_FRAC`, `RADAR_OUTER_RING_FRAC`
**Severity:** low
**Description.** The rings at 35% and 75% of the disc are commented as cannon range and missile range. The cannon no longer exists, laser and missile range are 600 m, and at the 300 m base range the rings sit at about 105 m and 225 m. The rings imply ranges that mean nothing in game.
**Reproduction.** Compare ring radius × active range with `LASER_RANGE` / `MISSILE_RANGE`.
**Notes.** Either relabel them as plain distance markers, or derive them from the active weapon's range.

### BUG-016: Cluster submunition 120° search cone documented but not implemented

**Status:** open
**Found:** 2026-10-15 (documentation migration audit)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) cluster child reacquire (around lines 2302–2316)
**Severity:** medium
**Description.** The combat design (`combat_tuning.md`) and the old CLAUDE.md "Traps" section both say cluster submunitions search a 120° forward cone and must never search a full sphere, because that is what spreads the weapon across a swarm. In the code, children reacquire any live enemy within `MISSILE_REACQUIRE_RANGE` (300 m) in every direction, and no cone constant exists. The spread comes only from the split distance (`CLUSTER_SPLIT_DISTANCE`).
**Reproduction.** Fire a Cluster Missile at a tight group. Check how child targets are picked in the reacquire loop; there is no angular gate.
**Notes.** Either implement the cone (`CLUSTER_SUB_SEARCH_CONE`), or accept split-distance spreading and update [AV-007](ATTACK_VECTORS.md#av-007-cluster-submunitions-converge-on-one-target).

### BUG-028: 3D view is mirrored east–west relative to the compass and heading

**Status:** open
**Found:** 2026-10-15 (pad radar tower placement review)
**Location:** cross-cutting: camera setup in [src/core/GameState.cpp](src/core/GameState.cpp); world convention +X = east (heading 090 faces +X)
**Severity:** medium
**Description.** With the player facing north (heading about 000), world +X objects appear on the left of the screen and −X objects on the right. By the compass and heading tape, east should be on the right. raylib's camera right vector when looking down +Z is −X, so a right-handed view of a world authored with +X = east shows east on the left. Anything placed by compass direction therefore appears mirrored. Examples: the base control tower (+X+Z, "NE") shows as NW, and the "east" ramp shows on the west. The radar's long-standing "empirical fix" that negates screen X (`Radar::worldToRadar`) is very likely compensating for this.
**Reproduction.** Face north on a base platform: the control tower (+X+Z) is on the left and the pad radar tower (−X+Z) is on the right, towards the heading tape's NE marks.
**Notes.** A root fix (flip the world X convention, or mirror the projection) touches flight input, camera, radar and compass, and needs a dedicated session. Until then, base layout uses `BASE_EAST_SIGN` so placement matches what the player sees on the compass.

## Fixed

### BUG-003: Documentation claims rivers ship, but river carving is disabled

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (documentation migration audit)
**Location:** README.md, CLAUDE.md; [src/world/Heightmap.cpp](src/world/Heightmap.cpp)
**Severity:** low
**Description.** `26495c8` (2026-05-03) disabled river carving ("Rivers disabled; lakes only"). README and CLAUDE.md kept listing rivers as a shipped feature, and CLAUDE.md quoted `RIVER_COUNT = 6` when Config has 8.
**Reproduction.** Generate any terrain. No rivers appear.
**Notes.** Docs corrected in the 2026-10-15 documentation migration. Rivers are now tracked as F-003 (Not started).

### BUG-006: Difficulty presets documented as shipped but not implemented

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (documentation migration audit)
**Location:** CLAUDE.md; [src/core/Config.hpp](src/core/Config.hpp) difficulty modifier tables
**Severity:** low
**Description.** CLAUDE.md described four presets "selected at game start". The tables exist in Config, but there's no `DifficultyPreset` enum, selection UI or any reader of the tables.
**Reproduction.** `grep -rn 'AGGRESSION_MULT\|PRESET_ASSIST' src` finds them only in Config.hpp.
**Notes.** Docs corrected in the 2026-10-15 documentation migration. The feature is tracked as F-027 (Not started).

### BUG-013: CLAUDE.md key-constants table drifted from Config.hpp

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (documentation migration audit)
**Location:** CLAUDE.md "Key Config Constants"
**Severity:** low
**Description.** Six documented values were stale: `TERRAIN_SCALE` 4.0 (actual 8.0), `TERRAIN_HEIGHT_MAX` 55 (220), `SEA_LEVEL` 0.20 (0.30), `RIVER_COUNT` 6 (8), `LAKE_COUNT` 12 (14), `NEWTON_DRAG` 0.02 (0.15). It also listed `NEWTON_FUEL_*` constants from the finite-fuel era, which was replaced by the thrust charge in `be7724e`.
**Reproduction.** Compare the table with [src/core/Config.hpp](src/core/Config.hpp).
**Notes.** The rewritten CLAUDE.md no longer duplicates constant values; it points to Config.hpp, following the standard's "one source of truth per fact" rule.

### BUG-017: Documentation describes a spatial grid and module layout that don't exist

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (documentation migration audit)
**Location:** CLAUDE.md, README.md; `src/ai/`, `src/weapon/`, `src/renderer/`, `src/hud/`, `src/entity/{Enemy,Friendly,Projectile}`
**Severity:** low
**Description.** CLAUDE.md said "Spatial grid cell size = `SPATIAL_CELL_SIZE` (60 units), rebuilt each tick" and listed AI, weapon, renderer and HUD modules as if they held logic. In fact `SpatialGrid`, `AIController`, `EnemyAI`, every `weapon/` file, `SceneRenderer`, `PostFX`, `renderer/ParticleSystem`, `HUD`, `WeaponDisplay`, `ShieldDisplay`, `ShieldSystem`, `SkyDome`, `Enemy`, `Friendly` and `Projectile` are empty files. AI, weapons and HUD live in `EntityManager.cpp` and `GameState.cpp`, and all entity queries are linear scans. CLAUDE.md also described per-type `std::vector` pools; the real layout is one type-tagged `Entity` in two fixed `std::array` pools.
**Reproduction.** `find src -size 0`
**Notes.** Docs corrected in the 2026-10-15 migration (see [ARCHITECTURE.md](ARCHITECTURE.md)). The cleanup is [IMP-014](IMPROVEMENTS.md#imp-014-delete-or-populate-the-empty-stub-modules); the performance risk is [AV-011](ATTACK_VECTORS.md#av-011-quadratic-entity-queries-without-a-spatial-grid).

### BUG-018: Archived design docs claim the terrain rebuild never shipped

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (documentation migration audit)
**Location:** `project-status/archive/README.md`; banners of `terrain_rebuild.md`, `REBUILD_ROADMAP.md`, `CLAUDE_rebuild.md`
**Severity:** low
**Description.** The archive marked the terrain rebuild "NOT SHIPPED (still Diamond-Square)". In fact Fourier sine synthesis shipped in `50fa26e` and was replaced the same day (2026-05-03) by tileable Perlin fBM in `26495c8`. Anyone reading the archive could be led to re-implement an obsolete spec.
**Reproduction.** Read the archive README index row for `terrain_rebuild.md`.
**Notes.** Banners and index corrected in the 2026-10-15 migration. [D-006](DECISIONS.md#d-006-tileable-perlin-gradient-fbm-terrain) records the actual history.

### BUG-019: In-game controls bar shows stale bindings

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (README screenshot session)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawHUD()` controls bar (around line 2104)
**Severity:** low
**Description.** The bottom controls bar reads "W/LMB: thrust" and "Q/E: roll". Since `d0cc533`, LMB fires the primary and Q/E select the laser colour; roll has no key. The bar also omits the weapon keys (Tab, RMB/Z, F/X, T, Shift+LMB charge). New players are told the wrong controls.
**Reproduction.** Start any round and read the bar along the bottom of the screen.
**Notes.** Fixed 2026-10-15: the bar now lists Mouse aim, W thrust, LMB fire, Shift+LMB charge, Q/E red/blue, Tab, RMB/Z, F/X, T and 1–5, sized to fit 1280 px.


### BUG-020: Debug info panel and FPS counter appear in release builds

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (README screenshot session)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawHUD()` (`DrawFPS` and the "Debug info panel (top right)" block, lines 1716–1795)
**Severity:** low
**Description.** `DrawFPS` and the top-right panel (X/Z, altitude, AGL, speed, heading, pitch, roll, charge, assist level) are drawn unconditionally. Only part of the HUD is behind `#ifdef DEV_MODE`. A release build therefore shows developer telemetry over gameplay, which also spoils screenshots.
**Reproduction.** Build without `-DDEV_MODE=ON` and start a round. The FPS counter and debug panel are visible.
**Notes.** Fixed 2026-10-15: `DrawFPS` and the telemetry panel are now inside `#ifdef DEV_MODE`. A player-facing FPS toggle can be added to settings later if wanted.


### BUG-021: Overlapping HUD elements in the bottom-left cluster, pitch ladder and speed tape

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (README screenshot review)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawHUD()` bottom-left cluster; `drawFlightHUD()` pitch ladder and speed tape
**Severity:** low
**Description.** Six overlaps:
- The HULL/THRUST bars, shield pie and weapon panel were placed at fixed offsets from the screen bottom, so the SHLD caption and the weapon panel's TRT row slid under the 28 px controls bar.
- The laser battery bar started at a fixed 90 px, on top of "RED LASER" / "BLUE LASER".
- The GOD tag was wedged between the THRUST bar and the shield pie.
- The pitch ladder drew through the heading tape and the wave/friendlies banners.
- The speed tape's tick labels were drawn at a negative x, off the left edge.
**Reproduction.** Visible in any in-flight frame at 1280×720 (see the pre-fix `docs/screenshots/combat.png`).
**Notes.** Fixed 2026-10-15:
- The cluster now stacks up from the top of the controls bar.
- Meters start after the widest primary name, and the weapon panel is 20 px wider.
- GOD and LANDED are right-aligned on the HULL/THRUST label lines.
- The pitch ladder is scissor-clipped below y=116.
- The speed and altitude tapes moved 24 px right.

### BUG-022: Nose crosshair can draw over the heading tape and wave banners

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (HUD layout check after BUG-021)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawFlightHUD()` nose / weapons crosshair
**Severity:** low
**Description.** The aim crosshair is projected from a point 30 m ahead of the nose and drawn wherever that lands on screen, without clipping. In chase view it often lands near the top of the screen, on top of the heading tape and the WAVE / FRIENDLIES banners.
**Reproduction.** Chase view (1), level flight. The circle-and-cross marker appears at about y = 40–70 at 1280×720.
**Notes.** Fixed 2026-10-15: the flight HUD now draws before the status banners, and the crosshair and FPV are scissor-clipped to the play area below `kHudTopStackBottom`, matching the pitch ladder. Whether the 30 m projection is itself right is still unverified. Original options considered: clip it like the pitch ladder; draw the banners after the flight HUD so they sit on top; or check whether the 30 m projection distance is what's intended. The crosshair should mark where shots go, and a projection that sits that high may itself be off.


### BUG-023: DEV hotkey labels overlap the controls hint text

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (HUD layout check after BUG-021)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawHUD()` controls bar, `#ifdef DEV_MODE` key labels
**Severity:** low
**Description.** In DEV_MODE builds, the right-aligned `[DEV] F1:cam … F6:dump` labels are drawn on the same 28 px bar as the left-aligned player controls text. Since BUG-019 lengthened that text, the two overlap across the right half of the bar. Release builds are unaffected.
**Reproduction.** Build with `-DDEV_MODE=ON` and start a round.
**Notes.** Fixed 2026-10-15: in DEV builds the controls bar is two rows (`kControlsBarH` = 46): player hints on top, dev labels below. The bottom-left cluster and the radar stack from the bar's top edge. Original options: in DEV builds, give the dev labels their own strip; or hide the player hint text while the dev labels are shown.


### BUG-024: Classic-view compass rose overlaps the radar

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (HUD layout check after BUG-021)
**Location:** [src/core/GameState.cpp](src/core/GameState.cpp) `drawClassicCompassRose()`
**Severity:** low
**Description.** In Classic view (5), the compass rose was drawn at a fixed bottom-right position (`sw − 50`, `sh − 95`). That is inside the radar disc, which Classic view also enlarges by 20%, and on top of the altitude strip.
**Reproduction.** Press 5 during a round. The N/S/E/W rose sits on the radar's lower-right edge.
**Notes.** Fixed 2026-10-15: moved to the top-left (64, 150), between the FPS counter and the SPD tape, which is free in both builds.

### BUG-025: Fighters and Bombers open fire beyond their weapons' reach

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (playtest report: "they start firing as soon as they see me")
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) `updateFighter()` / `updateBomber()` state transitions and `willFire`
**Severity:** medium
**Description.** Two faults:
- Fighters and Bombers fired whenever they were in ATTACK state and facing the target, and ATTACK began at the sidecar `ai.attackRange`. For the Fighter that is 180 m, while its shots travel only 80 m (`FIGHTER_PROJ_RANGE`). Fighters fired from more than twice their weapon's reach, and the tracers fizzled out mid-air.
- Nothing cleared ATTACK once the target moved beyond `aiDetectionRange`, so a ship could stay latched in ATTACK and keep firing at any distance.
**Reproduction.** Approach a Fighter head-on from about 200 m. It opens fire at about 180 m, and the shots expire around 80 m out.
**Notes.** Fixed 2026-10-15:
- Engaging (ATTACK: ease throttle, line up) is now separate from firing.
- Shots are only fired once the target is inside `ENEMY_FIRE_RANGE_FRAC` (0.9) × the projectile range: about 72 m for Fighters, about 99 m for Bombers. This matches drones (45 m fire / 50 m shots).
- Outside `aiAttackRange`, ships drop back to PURSUE.
- Drones, tanks and turrets were already range-gated correctly.
- Fighter `ai.attackRange` (180 m) is still well beyond its weapon reach, so Fighters ease throttle and line up from 180 m and close for about 100 m before firing. Lowering it in `fighter.meta.json` is a tuning call.

### BUG-009: Infected ships still show as hostile on radar

**Status:** fixed (2026-10-15)
**Found:** 2026-06-04 (`44b5d99`)
**Location:** [src/hud/Radar.cpp](src/hud/Radar.cpp) `baseColourForBlip()`
**Severity:** low
**Description.** Infected Fighters and Bombers fight for the player, but the radar colours blips by entity type only. Converted ships still appear red. The `44b5d99` commit message notes "Radar.cpp not touched in this slice; can be wired in later."
**Reproduction.** Infect a Fighter with the Infectious Missile. Its blip stays red.
**Notes.** Fixed 2026-10-15 (C.2): `Radar::update` colours Infected/Infecting contacts with the friendly IFF colour. This also covers base defenders, which fly the same allied path.


### BUG-026: Enemy fire never damages friendly structures or allied ships

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (Slice C C.2 implementation)
**Location:** [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) `updateProjectile()` collision routing (the "enemy projectile → player" branch)
**Severity:** high
**Description.** Enemy-owned projectiles are only collision-tested against the player. Nothing checks them against Bases, Collectors, Repair Stations, Radar Boosters or Radar Stations, or against allied ships (infected Fighters and Bombers, base defenders). As a result:
- Friendly structures can't be destroyed, so Bomber STRAFE_FRIENDLY runs (5g, `f22f08e`) do nothing.
- The "all friendlies lost" loss condition can never trigger, in Wave or Base mode.
- Allied ships die only from the infection bleed or the player's own shots.
- In C.2, bases can never come "under attack", so they never launch defenders.
The 5g commit says strafe runs "fire at the friendly", but the friendly-hit check was never written.
**Reproduction.** Let a Bomber strafe a Collector or Base for a while. Its hull bar never drops.
**Notes.** Fixed 2026-10-15 in both modes, as part of C.2: the enemy-projectile branch now tests friendly structures and allied ships (`isFriendlyStructure` or Infected/Infecting) before the player. This makes Wave mode harder, because Collectors and Bases can now be lost. Enemy AI still aims only at the player (and Bombers at friendlies), so allied ships are mostly hit by stray fire. The original note said the fix is an extra pass in the enemy-projectile branch: test friendly structures and allied ships (Infected/Infecting state) and `applyDamage` them, before the player test.


### BUG-027: Rebuilding a terrain chunk crashes (stale mesh ids after unload)

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (C.2 playtest: segfault, exit 139, after adding base aprons)
**Location:** [src/world/TerrainChunk.cpp](src/world/TerrainChunk.cpp) `unload()`
**Severity:** high
**Description.** `unload()` called `UnloadModel(m_model)` but left the chunk's separate `Mesh m_mesh` copy holding the freed GPU ids and vertex pointers. On the next `build()`, raylib's `UploadMesh` saw a non-zero VAO id and refused ("Trying to re-load an already loaded mesh"), so the new model referenced freed memory and the game crashed when drawing it. Any chunk rebuild was affected, including the DEV F5 terrain reseed (`Planet::generate` unloads and rebuilds every chunk).
**Reproduction.** Before the fix: start a round (base aprons rebuild nearby chunks), or press F5 in a DEV build. The log shows the "already loaded" warning, followed by a segfault.
**Notes.** Fixed 2026-10-15: `unload()` now resets `m_mesh` and `m_model` to `{}`. A startup smoke test confirms clean re-uploads with no warnings.

### BUG-007: AI, collision and radar distances ignore the toroidal wrap

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (`7e846c3`)
**Location:** cross-cutting: [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp) AI and collision, [src/hud/Radar.cpp](src/hud/Radar.cpp)
**Severity:** medium
**Description.** Rendering is wrap-aware (`Planet::nearestCopy()`), but gameplay distances use raw coordinates. Once the unwrapped player crosses a world edge, or friendlies spread world-wide, three things go wrong. Enemies pursue targets the long way round the map. Radar bearings point the long way. Entities close across the seam don't collide or detect each other.
**Reproduction.** Fly more than about 4 km from spawn in one direction. Radar blips for friendlies near spawn point backwards instead of towards the nearer copy across the seam.
**Notes.** Fixed 2026-10-15 in two steps. Gameplay: `EntityManager::rebaseAround()` runs at the start of every update and shifts each entity and projectile (with its `spawnPos`, `routePoint` and Collector `targetPos`) onto the world copy nearest the player, so all distances near the player are correct. Found when, after flying back to the base across an edge, enemies, the base turret and defenders stopped engaging. Known limitation: two entities about half a world from the player can end up on opposite sides of the rebase line; that only affects fights roughly 4 km away. Radar: `Radar` now measures every contact, painted echo, ghost, tower pulse and missile warning to the nearest toroidal copy (`Config::WORLD_SIZE`). Found when a tower in front of a player past the southern edge had no blip or pulse. Related: [AV-005](ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap), [D-007](DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world).


### BUG-001: Radar shows every entity on the map regardless of range

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (radar review session)
**Location:** [src/hud/Radar.cpp](src/hud/Radar.cpp) `Radar::update()` and `Radar::draw()`
**Severity:** medium
**Description.** `Radar::update()` adds every live entity in the pool as a contact, and `draw()` pins out-of-range contacts to the edge of the disc. `RADAR_BASE_RANGE` and `RADAR_BOOST_RANGE` therefore only change the disc scale, not what can be detected. The player has a full-map direction finder, which undercuts the Slice C radar network design (F-023), where coverage is meant to be limited and to degrade as nodes are lost.
**Reproduction.** Start a Wave or Base round. Every enemy on the map appears as a blip on the edge of the disc, however far away it is.
**Notes.** Fixed 2026-10-15 by C.4 (F-023): contacts show only inside the player's 300 m or an online network node's coverage. The original note said to settle this before C.4 (F-023). Options: drop contacts beyond range, or keep edge markers only for contacts reported by the radar network. Related: BUG-002.


### BUG-002: One Radar Booster anywhere boosts radar range globally

**Status:** fixed (2026-10-15)
**Found:** 2026-10-15 (radar review session)
**Location:** [src/hud/Radar.cpp](src/hud/Radar.cpp) `Radar::update()`; `EntityManager::anyRadarBoosterAlive()`
**Severity:** low
**Description.** The boosted range applies if any Radar Booster is alive anywhere. Since `7e846c3`, boosters spawn across the whole world, so one booster thousands of metres away still gives the player the full 500 m range.
**Reproduction.** Start a Wave round. The radar border is bright green (boosted) even when the only booster is far out of sight.
**Notes.** Fixed 2026-10-15 by C.4: the booster is now an ordinary network node that covers 300 m around itself while its cluster is online. The global 500 m boost (`RADAR_BOOST_RANGE`) is gone. Related: BUG-001.


## Won't Fix

_None._

## Deferred

### BUG-011: OBJ-rendered entities lose the white damage flash

**Status:** deferred
**Found:** 2026-06-02 (`e133893`)
**Location:** [src/mesh/MeshRegistry.cpp](src/mesh/MeshRegistry.cpp) `draw()`; entity render paths in [src/entity/EntityManager.cpp](src/entity/EntityManager.cpp)
**Severity:** low
**Description.** Tinting a model `WHITE` doesn't override per-vertex baked palette colours, so OBJ-rendered entities don't flash white when hit. Procedural fallbacks still do.
**Reproduction.** Shoot any OBJ-rendered enemy. There is no white flash.
**Notes.** Accepted trade-off of the OBJ pipeline ([D-009](DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth)). Fix path is a shader-based flash: [IMP-010](IMPROVEMENTS.md#imp-010-shader-based-damage-flash-for-obj-entities).
