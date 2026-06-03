# terra-siege — Roadmap

Authoritative plan for what to build next. Three tracks run in parallel:

| Track | Scope | Authoritative doc |
|--|--|--|
| **Engine** | Phased build (Phase 1–6): terrain, flight, combat foundation, shields, HUD, polish, extras | This file + [README — Status](README.md#status) |
| **Features** | New game-modes design landing in three slices (A → B → C) | [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md) |
| **Tooling** | Inspector — OBJ editor + sidecar entity profiles for every mesh | [terra_siege_inspect_roadmap.md](terra_siege_inspect_roadmap.md) |

Status snapshots in any doc drift over time. `git log` is ground truth for what shipped.

---

## Current state

See the [README Status table](README.md#status) for the live phase rollup. Summary:

- **Phases 1, 1.5, 2** complete — terrain, flight model, ship + camera, ground shadow, exhaust.
- **Phase 3** complete through **5g** — enemy roster (Drone, Fighter, Seeder, Ground Turret, Bomber, Carrier), full weapon roster (Cannon, Plasma, Beam, Missile, Cluster, Depth Charge, Auto Turret, EMP), wave manager, pre-flight loadout, friendly units (Collector, Repair Station, Radar Booster), bomber strafe AI.
- **Phase 4** partial — player directional shields + bubble + pie HUD; radar tier 1 / 2 / 3.
- **5h** (post-Phase 3) — Collector economy loop, Base entity (player & enemy), ground tank, base auto-turret, friendly-fire filter.
- **OBJ mesh pipeline** + **terra-siege-inspect** binary with `Tool` registry — all 14 entity meshes migrated.

Not yet started: Phase 5 polish (audio, day/night, weather), Phase 6 extras (leaderboard, cockpit cam, replay), and the entirety of the Features track (Slices A / B / C).

---

## Engine track — remaining

| Phase | Remaining work | Notes |
|--|--|--|
| 3.5 | Atmospheric terrain objects (tree clusters, rock formations, antennas, crash sites). Functional bases + ground turrets already ship as terrain-anchored | Procedural placement table per [CLAUDE.md §3.5](CLAUDE.md) |
| 4 | Full weapon-slot HUD, top-centre wave / enemy counter, any remaining radar polish | Most of Phase 4 already shipped — finish-line work |
| 5 | Audio (positional miniaudio backend), day/night cycle, weather (wind drift, storm fog), post-FX (vignette, mild bloom) | Pre-allocated particle pool already shipped |
| 6 | Leaderboard persistence, cockpit camera, replay recording, additional weapon / enemy types | Open-ended; scheduled by demand |

---

## Feature track — A / B / C slicing

Full design lives in [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md). We're not landing 1100 lines as a single PR — instead three independent slices, each shippable on its own.

| Slice | Scope | New systems | Asset deps |
|--|--|--|--|
| **A — Visual + AI polish** *(immediate next)* | Smoke trail at <50% HP; retreat-to-base AI at <40% HP; heal-at-base | None — fits the existing enemy roster | None — pure code |
| **B — Infection + new weapons** | Shield Laser (4th primary), Shield Missile, Infectious Missile, 4-weapon cycle, shared energy pool (120 max, 15/s recharge), infection state machine on existing entities | Energy pool, infection state machine, missile selection cycle, new projectile types | New missile mesh + sidecar weapon profiles |
| **C — Base Mode** *(redesigned — see [base_mode_v2.md](base_mode_v2.md))* | Asymmetric defender vs invader. Friendly bases + radar network already in place; a few enemy landers already grounded at game start. Landers act as enemy bases (run their own production), expand by infecting captured friendly bases. Infection requires shields-stripped + hull ≤30%, applies symmetrically, bases can re-flip freely. Radar network degrades as nodes fall; player keeps a short-range personal radar throughout. Win: clear all landers + retake all infected bases. Lose: all friendly bases lost OR player destroyed | `GameMode` enum (Wave / Base), lander entity, network connectivity graph + help-call propagation, infection v2 (shields+hull gate, symmetric, re-flip allowed), session-result screen | Lander, friendly-base, radar station, builder, orbit-drone meshes + sidecar profiles |

**Slice ordering rationale:** the infection mechanic — the standout idea in the design doc — is shippable in Slice B without committing to the full RTS build in Slice C. Slice A goes first because it's small, code-only, and proves nothing breaks.

**Open design questions are resolved per-slice when that slice lands**, not upfront. Examples:

- *(Slice B)* Player ship can't be infected — undercuts the theme; revisit when implementing.
- *(Slice B)* Infected ships fight where they are until destroyed (disposable) vs being recruitable at friendly bases — revisit when implementing.
- *(Slice C)* On a toroidal world with two front lines, is the player an *operator* of an autonomous faction or a *combatant* in their own right? Revisit when the AI economy is live and we can feel it.

---

## Tooling track — inspector

Authoritative: [terra_siege_inspect_roadmap.md](terra_siege_inspect_roadmap.md). Sub-phases relevant to upcoming game work:

| Inspector phase | What it adds | Unblocks |
|--|--|--|
| **Phase A** — Workspace & file ops | Boot without OBJ, Open / Save / Save-As / Close, drag-and-drop, recent files, unsaved-changes prompt | All authoring work — should land early |
| **Phase B** — Vertex editing depth | Multi-select, undo / redo, mirror, snap, numeric input, weld | Authoring any non-trivial mesh |
| **Phase C** — Materials & palette | 32-colour swatch, face pick, eyedropper | Re-colouring without dropping to a text editor |
| **Phase D** — Topology | Add / delete verts and faces, flip winding, extrude | Building meshes from scratch in the inspector |
| **Phase E** — Primitives | Cube / cylinder / cone / sphere / plane | **Slice C base / radar / builder mesh authoring** |
| **Phase F.1** — Sidecar foundation | Load + save `*.meta.json`, schema validation, read-only viewer overlay, forward / scale / pivot mini-fields | Per-mesh entity profiles in general — **prerequisite for `EntityProfileRegistry` on the game side** |
| **Phase F.2** — Identity + hull + shields | Class dropdown, hull form, shields form (with 3D viz) | **Migration of existing `Config::FIGHTER_HP` etc. into sidecars** — pairs with Slice B |
| **Phase F.3** — Weapons + hardpoints | Named weapon stat blocks, hardpoint placement, fire-arc visualiser | **Slice B new weapon authoring** (Shield Laser, Shield/Infectious Missiles) without recompiling |
| **Phase F.4** — AI profile | Profile picker, range fields with wire circles, infection block | **Slice B infection** (per-entity `canBeInfected` etc.), **Slice C** (per-base AI) |
| **Phase F.5** — FX | Smoke threshold (slider), engine emitters, glow colour, death explosion scale | **Slice A** reads the smoke-threshold field even before this tool exists (edit JSON by hand); rest pairs with Slice C |
| **Phase F.6** — Resources | Yield on death, base capacity, production array | **Slice C only** |
| **Phase G** — Workflow polish | View modes, ortho views, normals overlay, asset browser, live file watch, in-game hotswap, diff vs HEAD | Quality-of-life — schedule by demand |

**Decision recorded:** sidecar by default, registry override later. One `*.meta.json` per OBJ, one-to-one. A registry file (single JSON pointing at multiple meshes with per-faction stat overrides) gets added only when we need to reuse one mesh across factions — not before.

---

## How the tracks interleave

Recommended sequencing — what to ship in what order:

1. **Slice A** — smoke + retreat. Pure code, no deps. Lands first.
2. **Inspector Phase A** — file ops, boot-without-OBJ. Unblocks asset authoring for everything after.
3. **Inspector Phase F.1** — sidecar foundation, read-only viewer. Lets us start writing `*.meta.json` files by hand.
4. **Inspector Phase F.2** + game-side **`EntityProfileRegistry`** — one-time migration of hull / shield Config constants into per-mesh sidecars. Needed for Slice B.
5. **Inspector Phase F.3** + **Slice B** — new weapon authoring lands alongside the infection / 4-weapon cycle code.
6. **Inspector Phases F.4 + F.5** — AI profile + FX tools, while Slice B settles.
7. **Inspector Phase E** + **F.6** — primitives + resources, for Slice C asset authoring.
8. **Slice C** — Base Mode, the biggest slice. Depends on every prior tooling step.
9. **Engine Phases 3.5 / 5 / 6** — run in spare cycles, not on the critical path for any slice.

Phases B, C, D, G of the inspector roadmap land whenever they're useful — they're not gated by any specific game slice.

---

## Related documents

- [README.md](README.md) — user-facing description, build/run, controls, current Status table.
- [CLAUDE.md](CLAUDE.md) — full project context for AI assistants (architecture decisions, coding standards, key constants).
- [project-status/game_modes_and_features.md](project-status/game_modes_and_features.md) — full design doc for the Features track.
- [project-status/archive/project-roadmap.md](project-status/archive/project-roadmap.md) — earlier Phase-2-era roadmap (flight modes, craft profiles). Most of it has shipped.
- [project-status/archive/VIRUS_REMAKE_ARCHITECTURE.md](project-status/archive/VIRUS_REMAKE_ARCHITECTURE.md) — original architectural rationale.
- [project-status/archive/README.md](project-status/archive/README.md) — full index of all archived design docs (rebuild specs, prior CLAUDE.md variants).
- [terra_siege_inspect_roadmap.md](terra_siege_inspect_roadmap.md) — Tooling track in detail.
