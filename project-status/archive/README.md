# project-status/archive — Historical design corpus

Documents that have been superseded, shipped, or otherwise rolled into the current active set. Kept for reference — they capture the *why* behind decisions and the design history that led to what's in the code now.

The active docs live at the project root ([`CLAUDE.md`](../../CLAUDE.md), [`ROADMAP.md`](../../ROADMAP.md), [`base_mode_v2.md`](../../base_mode_v2.md), [`terra_siege_inspect_roadmap.md`](../../terra_siege_inspect_roadmap.md)) plus the still-active [`game_modes_and_features.md`](../game_modes_and_features.md) one level up (parts §§4-9 still apply; Part 2 was superseded by `base_mode_v2.md`).

---

## Index

| File | Original path | Era | Status | Notes |
|--|--|--|--|--|
| [`CLAUDE_phase2.md`](CLAUDE_phase2.md) | `project-status/CLAUDE.md` | Phase 2 partial | Superseded | Earliest project context; predates Phase 3 combat |
| [`CLAUDE_rebuild.md`](CLAUDE_rebuild.md) | `terra_rebuild/CLAUDE.md` | May rebuild | Superseded | Project context paired with the rebuild spec set |
| [`VIRUS_REMAKE_ARCHITECTURE.md`](VIRUS_REMAKE_ARCHITECTURE.md) | `project-status/...` | Pre-code | Reference rationale | The original architectural proposal (raylib, flat pools, fixed timestep, directional shields). Still worth reading for *why* |
| [`project-roadmap.md`](project-roadmap.md) | `project-status/project-roadmap.md` | Phase 2 era | Superseded | Phase 2 flight-modes design (dual Classic+Arcade + five craft). Collapsed into single Newtonian by the rebuild |
| [`REBUILD_ROADMAP.md`](REBUILD_ROADMAP.md) | `terra_rebuild/REBUILD_ROADMAP.md` | May rebuild | 4/5 shipped | Execution plan for the rebuild spec set. Terrain rebuild not yet shipped |
| [`readme_rebuild.md`](readme_rebuild.md) | `terra_rebuild/readme_rebuild.md` | May rebuild | Superseded | Original index of the rebuild spec set; this README replaces it |
| [`flight_mode_rebuild.md`](flight_mode_rebuild.md) | `terra_rebuild/flight_mode_rebuild.md` | May rebuild | **SHIPPED** | Single Newtonian flight model. Replaced dual `ARCADE_*`/`CLASSIC_*` with `NEWTON_*` |
| [`terrain_rebuild.md`](terrain_rebuild.md) | `terra_rebuild/terrain_rebuild.md` | May rebuild | **NOT SHIPPED** | Sine-wave Fourier synthesis with toroidal wrap. Current code is still Diamond-Square. Revisit when there's appetite |
| [`camera_system.md`](camera_system.md) | `terra_rebuild/camera_system.md` | May rebuild | **SHIPPED** | Five-view camera (Chase/Velocity/Tactical/ThreatLock/Classic on 1–5). Tactical's `camera.up = {0,0,1}` invariant preserved in CLAUDE.md |
| [`combat_tuning.md`](combat_tuning.md) | `terra_rebuild/combat_tuning.md` | May rebuild | **SHIPPED** | TTK-derived HP framework, full weapon roster, missile system. Authoritative reference for combat numbers; TTK-not-HP rule preserved |
| [`radar_system.md`](radar_system.md) | `terra_rebuild/radar_system.md` | May rebuild | **SHIPPED** | Three-tier radar. Ghost-blip pool (32 pre-allocated slots) preserved |

---

## When to read which file

- **"Why is the architecture this way?"** → [`VIRUS_REMAKE_ARCHITECTURE.md`](VIRUS_REMAKE_ARCHITECTURE.md)
- **"How does combat tuning work?"** → [`combat_tuning.md`](combat_tuning.md)
- **"What was the flight rebuild's rationale?"** → [`flight_mode_rebuild.md`](flight_mode_rebuild.md)
- **"How does the radar work?"** → [`radar_system.md`](radar_system.md)
- **"What about sine-wave terrain?"** → [`terrain_rebuild.md`](terrain_rebuild.md) (pending — still Diamond-Square in code)
- **"What were the camera design decisions?"** → [`camera_system.md`](camera_system.md)
- **"What was the rebuild sequence?"** → [`REBUILD_ROADMAP.md`](REBUILD_ROADMAP.md)

For everything else, see [`/CLAUDE.md`](../../CLAUDE.md) which consolidates the architecture rules, invariants, and current state.
