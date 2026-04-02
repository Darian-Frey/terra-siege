# terra-siege

A modern reimagining of **Virus** (Argonaut Software / Firebird, 1988) built in C++17 with [raylib](https://www.raylib.com/).

Defend your planet against waves of alien attackers from the cockpit of a hovercraft. The planet is yours to protect — its cities, radar stations, and launch pads depend on you.

---

## Status

> **Active development — Phase 1.5 complete**

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | CMake scaffold, raylib, core loop, terrain, free-roam camera | ✅ Complete |
| 1.5 | 1025² heightmap, river carving, lake flooding, debug HUD | ✅ Complete |
| 2 | Player craft, physics, flight assist, follow camera | 🔧 Next |
| 3 | Enemies, combat, wave manager, friendly units | ⏳ Planned |
| 3.5 | Terrain objects — bases, towers, radar dishes, trees | ⏳ Planned |
| 4 | Directional shields, HUD, radar, weapon display | ⏳ Planned |
| 5 | Polish — particles, audio, day/night, weather | ⏳ Planned |
| 6 | Extended features — leaderboard, cockpit cam, replay | ⏳ Planned |

---

## Features (current)

- **Fractal terrain** — Diamond-Square generation with 12-pass smoothing and radial falloff producing a natural continent shape
- **Rivers** — downhill flow simulation carving channels from mountain sources to the ocean
- **Lakes** — flood-fill inland water bodies at natural terrain depressions
- **Flat-shaded rendering** — per-face colour and directional lighting for a clean low-poly aesthetic faithful to the original
- **Height colour bands** — snow, high rock, rock, grassland, sand, shallow water, ocean, river, lake all distinctly coloured
- **Debug HUD** — real-time position, altitude, AGL, heading and pitch overlay
- **Dev mode** — compile-time flag enabling debug overlay and future cheat features

---

## Planned Features

- Hovercraft physics with configurable flight assist (4 difficulty levels)
- Directional shield system (front / rear / left / right quadrants)
- Weapon slots — primary, secondary, special — with upgrade pickups
- Auto-turret subsystem with independent targeting AI
- Proportional navigation homing missiles
- Ego-centric radar with altitude strip and friend/foe differentiation
- Enemy variants — fighters, bombers, swarm drones, carriers, ground turrets
- Procedural terrain objects — military bases, launch pads, radar dishes, radio towers, trees
- Day/night cycle and weather effects
- EMP, cluster missiles, depth charges, beam laser

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

## Controls (current — free-roam camera)

| Key | Action |
|-----|--------|
| W / S | Move forward / back |
| A / D | Strafe left / right |
| Q / E | Altitude down / up |
| Left Shift | Speed boost (4×) |
| Mouse | Look around |
| Escape | Quit |

---

## Project Structure

```
terra-siege/
├── CMakeLists.txt
├── assets/
│   └── shaders/          # GLSL shaders (terrain, shield, exhaust)
└── src/
    ├── main.cpp
    ├── core/
    │   ├── Clock.hpp         # Fixed-timestep accumulator
    │   ├── Config.hpp        # All tuning constants
    │   ├── GameState.hpp/cpp # Top-level state machine
    ├── world/
    │   ├── Heightmap.hpp/cpp     # Diamond-Square + rivers + lakes
    │   ├── TerrainChunk.hpp/cpp  # Flat-shaded mesh builder
    │   └── Planet.hpp/cpp        # Chunk orchestration
    ├── entity/               # Player, enemies, projectiles (Phase 2+)
    ├── weapon/               # Weapon slots, missiles, turret (Phase 3+)
    ├── shield/               # Directional shield system (Phase 4+)
    ├── ai/                   # Enemy AI state machines (Phase 3+)
    ├── hud/                  # Radar, weapon display, shield pie (Phase 4+)
    ├── audio/                # Positional audio manager (Phase 5+)
    └── wave/                 # Wave definitions and spawning (Phase 3+)
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