# terra-siege

A modern reimagining of **Virus** (Argonaut Software / Firebird, 1988) built in C++17 with [raylib](https://www.raylib.com/).

Defend your planet against waves of alien attackers from the cockpit of a hovercraft. The planet is yours to protect — its cities, radar stations, and launch pads depend on you.

---

## Status

> **Active development — Phase 3 in progress (5d.3 enemy roster)**

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | CMake scaffold, raylib, core loop, terrain, free-roam camera | ✅ Complete |
| 1.5 | 1025² heightmap, Perlin fBM + ridged mountains, rivers, lakes | ✅ Complete |
| 2 | Newtonian flight model, five-view camera, ground shadow, exhaust | ✅ Complete |
| 3 | Combat foundation, wave manager, radar tier 1, settings menu | 🔧 In progress |
| 3.5 | Terrain objects — bases, towers, radar dishes, trees | ⏳ Planned |
| 4 | Directional shields, HUD, radar tier 2/3, weapon display | ⏳ Planned |
| 5 | Polish — audio, day/night, weather, post-FX | ⏳ Planned |
| 6 | Extended features — leaderboard, cockpit cam, replay | ⏳ Planned |

### Phase 3 progress

| Step | Description | Status |
|------|-------------|--------|
| 3a | Entity pools, Fighter AI, Cannon, projectile collision | ✅ |
| 3b | Settings menu (start/pause), god mode, invert axes, lost-power death | ✅ |
| 3c | Radar tier 1, wave manager, drop-shadow HUD font | ✅ |
| 5d.1 | Drone (boids swarm) + Fighter EVADE engine damage | ✅ |
| 5d.2 | Seeder (drone-dispenser) + Ground Turret + F7 skip-wave | ✅ |
| 5d.3 | Bomber (heavy, slow, punishing fire) | ✅ |
| 5d.4 | Carrier (4-sector shield, drone-spawning boss) | ⏳ Next |
| 5e   | Full weapon roster (Plasma, Beam, Missiles, Auto Turret, EMP) | ⏳ |
| 5f   | Round-start missile selection UI | ⏳ |
| 5g   | Friendly units + Bomber STRAFE_FRIENDLY state | ⏳ |

---

## Features (current)

### World

- **Tileable Perlin fBM terrain** — gradient noise with 7 octaves, domain warp, and shape exponent for proper continent/ocean balance
- **Ridged mountains** — Musgrave-style ridged-multifractal masked to high-elevation regions
- **Rivers** — downhill flow simulation carving channels from mountain sources to the ocean
- **Lakes** — flood-fill inland water bodies at natural terrain depressions
- **Flat-shaded rendering** — per-face colour and directional lighting for a clean low-poly aesthetic faithful to the original
- **Height colour bands** — snow, high rock, rock, grassland, sand, shallow water, ocean, river, lake all distinctly coloured

### Flight

- **Newtonian flight model** — Virus/Zarch-style "tilt and burn" along the local up axis with constant world gravity
- **Thrust charge** — drains while burning, recharges while coasting; flight ceiling cuts thrust above 250m AGL
- **Five-view camera** — Chase / Velocity / Tactical (overhead) / ThreatLock / Classic (fixed world-offset, 1988-style); per-view FOV and lerp speeds
- **Wireframe HUD overlay** — toggleable artificial-horizon style instrument layer
- **Ground shadow + drop-shadow HUD font** — DejaVu Sans Mono Bold loaded from system fonts with 1px black drop-shadow for legibility against any terrain colour
- **Particle system** — pre-allocated 2000-slot pool, gravity + bounce flags, used for engine exhaust, hit bursts, kill explosions, damage smoke trails

### Combat (Phase 3 in progress)

- **Cannon** — 100 DPS hitscan-feel projectile (8 damage / 0.08s fire rate), 200m range
- **Enemy roster:**
  - **Drone** — kamikaze swarm with boids flocking (separation/alignment/cohesion + pursuit), 1-shot kill, contact damage
  - **Fighter** — PURSUE/ATTACK/EVADE state machine, shielded, return-fire 25 DPS, engine-damage limp at <25% hull (smoke trail + reduced thrust/turn/top-speed)
  - **Bomber** — heavy bruiser, slow turn, punishing 31 DPS in chunky 25-damage shots, same engine-damage behaviour
  - **Seeder** — slow drone-dispenser, drifts in orbit and drops a fresh drone every 4s; fragile force-multiplier
  - **Ground Turret** — stationary terrain-anchored, rotating barrel tracks player, 18 DPS in a 6° fire cone
- **Wave manager** — staggered spawn cadence, 5s intermissions, ground-anchored placement for turrets, escalating wave table that introduces each type in isolation before mixing
- **Radar tier 1** — 120px ego-centric disc bottom-right, IFF blip colours, proximity pulse blink, altitude strip, ghost-blip pool reserved for tier 3, camera-aware sizing
- **Damage feedback** — 0.12s white flash per hit, hit-burst sparks (terrain + enemy contacts), kill-explosion fireballs, lost-power death (no more frozen scene — wreck falls and explodes on impact)
- **Settings menu** — start + pause overlays, invert pitch/yaw, god mode (infinite thrust + invincible + infinite weapons), wireframe HUD toggle; persisted to `$HOME/.config/terra-siege/settings.cfg`

---

## Planned Features

- Carrier (4-sector shield boss, drone-spawning)
- Friendly units — Collector, Repair Station, Radar Booster (lose them all = game over)
- Directional shield system (front / rear / left / right quadrants)
- Full weapon slot loadout — Plasma, Beam Laser, Missiles (proportional navigation homing), Cluster, Depth Charge, Auto Turret, Shield Booster, EMP
- Round-start missile selection UI
- Procedural terrain objects — military bases, launch pads, radar dishes, radio towers, trees
- Radar tier 2 (missile warning ring, jamming, ghost blips) and tier 3 (full target vector arrows)
- Day/night cycle and weather effects
- Positional audio, leaderboard, replay system, cockpit camera mode

---

## Building

### Requirements

- CMake 3.16+
- GCC or Clang with C++17 support
- Git (raylib fetched automatically via CMake FetchContent)
- Linux: `libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev`

### Linux

```bash
git clone https://github.com/Darian-Frey/terra-siege.git
cd terra-siege
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/terra-siege
```

### Developer / Cheat Mode

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDEV_MODE=ON
cmake --build build -j$(nproc)
./build/terra-siege
```

Dev mode enables the debug info overlay, and will enable free-camera toggle, god mode, and the entity inspector as those features are implemented.

### Windows (coming later)

The codebase is cross-platform. MinGW-w64 or MSVC builds planned once Linux development reaches Phase 4.

---

## Controls (current)

### Gameplay

| Key | Action |
|-----|--------|
| Mouse | Pitch / yaw / roll the ship (Newtonian — "tilt and burn") |
| W (hold) | Thrust along the ship's local up axis |
| Mouse 1 | Fire cannon |
| 1 / 2 / 3 / 4 / 5 | Switch camera view (Chase / Velocity / Tactical / ThreatLock / Classic) |
| Escape / P | Pause (opens settings menu) |

### Dev mode (`-DDEV_MODE=ON`)

| Key | Action |
|-----|--------|
| F1 | Toggle camera mode (Follow / FreeRoam) |
| F2 | Cycle flight-assist level (0–3) |
| F3 | Toggle god mode (infinite thrust + invincible + infinite weapons) |
| F4 | Toggle flight recorder |
| F5 | Reroll terrain seed |
| F6 | Dump heightmap to `tests/logs/heightmap-<unixtime>.{png,txt}` |
| F7 | Skip wave (silently kill all enemies + flush pending spawns) |
| `[` / `]` / `\` | Step in / out / reset (debug stepper) |

---

## Project Structure

```text
terra-siege/
├── CMakeLists.txt
├── assets/
│   └── shaders/                  # GLSL shaders (terrain, shield, exhaust — stubs)
└── src/
    ├── main.cpp
    ├── core/
    │   ├── Clock.hpp             # Fixed-timestep accumulator (120 Hz)
    │   ├── Config.hpp            # All tuning constants — single source of truth
    │   ├── GameState.hpp/cpp     # Top-level state machine + menu overlays
    │   ├── Particles.hpp/cpp     # 2000-slot pool, gravity + bounce flags
    │   └── Settings.hpp/cpp      # Persistent settings (~/.config/terra-siege)
    ├── world/
    │   ├── Heightmap.hpp/cpp     # Perlin fBM + ridged mountains + rivers + lakes
    │   ├── TerrainChunk.hpp/cpp  # Flat-shaded mesh builder
    │   └── Planet.hpp/cpp        # Chunk orchestration, heightAt() query
    ├── entity/
    │   ├── Entity.hpp            # Type-tagged struct (single pool layout)
    │   ├── Player.hpp/cpp        # Hovercraft mesh, Newtonian physics, input
    │   └── EntityManager.hpp/cpp # Flat enemy + projectile pools, AI dispatch
    ├── hud/
    │   └── Radar.hpp/cpp         # Tier 1 ego-centric disc + altitude strip
    ├── wave/
    │   ├── WaveDef.hpp           # Declarative wave loadout
    │   └── WaveManager.hpp/cpp   # Wave state machine + spawn placement
    ├── weapon/                   # Plasma, Beam, Missiles, Auto Turret (Phase 3+, stubs)
    ├── shield/                   # Directional shield system (Phase 4+, stubs)
    ├── ai/                       # Bomber STRAFE_FRIENDLY, Carrier (5d.4+, stubs)
    └── audio/                    # Positional audio manager (Phase 5+, stubs)
```

---

## Architecture Notes

- **Fixed timestep** — physics runs at 120 Hz decoupled from render rate; smooth motion on any display
- **No heap allocation in hot path** — entity, particle, and projectile pools pre-allocated at startup
- **Data-oriented entity update** — flat arrays per entity type, not pointer-chasing polymorphism
- **Procedural geometry** — all 3D assets (ship, terrain objects, UI elements) built in code; no model files required
- **Platform-agnostic** — `std::filesystem::path` throughout, no platform `#ifdef` in game logic

---

## Inspiration

[Virus](https://en.wikipedia.org/wiki/Virus_(video_game)) was developed by Jez San at Argonaut Software and published by Firebird in 1988. It was one of the first games to render a real-time filled 3D polygon landscape on home computers. The techniques Argonaut pioneered in Virus directly led to the Super FX chip and Star Fox on the SNES.

terra-siege is an independent fan reimagining — not affiliated with Argonaut Software or any rights holders.

---

## Licence

MIT — see `LICENSE` for details.