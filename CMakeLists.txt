cmake_minimum_required(VERSION 3.16)
project(terra-siege VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Dev mode flag — enables in-game debug overlay, free camera, god mode etc.
option(DEV_MODE "Enable developer/cheat mode" OFF)
if(DEV_MODE)
    add_compile_definitions(DEV_MODE)
    message(STATUS "terra-siege: DEV_MODE enabled")
endif()

# Fetch raylib
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Fetch doctest for the test harness. Header-only, single header — chosen
# over Catch2 for ~10× faster compile and a much smaller dependency
# surface. Tests are opt-in via the TERRA_SIEGE_BUILD_TESTS option.
option(TERRA_SIEGE_BUILD_TESTS "Build the test suite" ON)
if(TERRA_SIEGE_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
    )
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

# Fetch picojson for the entity-sidecar (inspector F.1). Header-only,
# single header (~1100 lines), no build of its own — same minimal-dep
# pattern as doctest. Used by the mesh static lib so both the game-
# side EntityProfileRegistry (later) and the inspector share one
# parser/writer.
FetchContent_Declare(
    picojson
    GIT_REPOSITORY https://github.com/kazuho/picojson.git
    GIT_TAG        v1.3.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(picojson)
add_library(picojson INTERFACE)
target_include_directories(picojson INTERFACE ${picojson_SOURCE_DIR})

# ---------------------------------------------------------------
# Shared mesh subsystem — used by the game, the inspector binary,
# and the test executables. Keeping this as a single static lib
# means changes to the loader / registry recompile once and all
# three consumers re-link.
# ---------------------------------------------------------------
add_library(terra_siege_mesh STATIC
    src/mesh/ObjLoader.cpp
    src/mesh/ObjLoader.hpp
    src/mesh/MeshRegistry.cpp
    src/mesh/MeshRegistry.hpp
    src/mesh/Palette.hpp
    src/mesh/SidecarProfile.cpp
    src/mesh/SidecarProfile.hpp
    src/mesh/EntityProfileRegistry.cpp
    src/mesh/EntityProfileRegistry.hpp
)
target_include_directories(terra_siege_mesh PUBLIC src)
target_link_libraries(terra_siege_mesh PUBLIC raylib picojson)

# ---------------------------------------------------------------
# Game source files (no mesh sources — those come from the lib;
# no inspector sources — those are a separate executable)
# ---------------------------------------------------------------
set(GAME_SOURCES
    src/main.cpp

    src/core/GameState.cpp
    src/core/GameState.hpp
    src/core/Clock.hpp
    src/core/Config.hpp
    src/core/Particles.cpp
    src/core/Particles.hpp
    src/core/Settings.cpp
    src/core/Settings.hpp

    src/world/Planet.cpp
    src/world/Planet.hpp
    src/world/Heightmap.cpp
    src/world/Heightmap.hpp
    src/world/TerrainChunk.cpp
    src/world/TerrainChunk.hpp
    src/world/SkyDome.cpp
    src/world/SkyDome.hpp
    src/world/Decorations.cpp
    src/world/Decorations.hpp

    src/entity/Entity.hpp
    src/entity/Player.cpp
    src/entity/Player.hpp
    src/entity/Enemy.cpp
    src/entity/Enemy.hpp
    src/entity/Friendly.cpp
    src/entity/Friendly.hpp
    src/entity/Projectile.cpp
    src/entity/Projectile.hpp
    src/entity/EntityManager.cpp
    src/entity/EntityManager.hpp

    src/weapon/WeaponSystem.cpp
    src/weapon/WeaponSystem.hpp
    src/weapon/WeaponTypes.hpp
    src/weapon/AutoTurret.cpp
    src/weapon/AutoTurret.hpp
    src/weapon/MissileGuidance.hpp
    src/weapon/UpgradeManager.cpp
    src/weapon/UpgradeManager.hpp

    src/shield/ShieldSystem.cpp
    src/shield/ShieldSystem.hpp
    src/shield/ShieldRenderer.cpp
    src/shield/ShieldRenderer.hpp

    src/ai/AIController.cpp
    src/ai/AIController.hpp
    src/ai/EnemyAI.cpp
    src/ai/EnemyAI.hpp
    src/ai/SpatialGrid.cpp
    src/ai/SpatialGrid.hpp

    src/renderer/SceneRenderer.cpp
    src/renderer/SceneRenderer.hpp
    src/renderer/ParticleSystem.cpp
    src/renderer/ParticleSystem.hpp
    src/renderer/PostFX.cpp
    src/renderer/PostFX.hpp

    src/hud/HUD.cpp
    src/hud/HUD.hpp
    src/hud/Radar.cpp
    src/hud/Radar.hpp
    src/hud/WeaponDisplay.cpp
    src/hud/WeaponDisplay.hpp
    src/hud/ShieldDisplay.cpp
    src/hud/ShieldDisplay.hpp

    src/audio/AudioManager.cpp
    src/audio/AudioManager.hpp

    src/wave/WaveManager.cpp
    src/wave/WaveManager.hpp
    src/wave/WaveDef.hpp
)

add_executable(terra-siege ${GAME_SOURCES})

target_include_directories(terra-siege PRIVATE src)

target_link_libraries(terra-siege PRIVATE terra_siege_mesh)

target_compile_definitions(terra-siege PRIVATE RAYMATH_IMPLEMENTATION)

# Bake project source root into the binary so DEV_MODE log files always
# land in tests/logs/ regardless of where the binary is launched from.
target_compile_definitions(terra-siege PRIVATE
    TERRA_SIEGE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# Compiler warnings
target_compile_options(terra-siege PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Copy assets to build directory so the binary can find them at runtime
add_custom_command(TARGET terra-siege POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:terra-siege>/assets
    COMMENT "Copying assets to build directory"
)

# Platform-specific links
if(UNIX AND NOT APPLE)
    target_link_libraries(terra-siege PRIVATE m dl pthread)
endif()

# ---------------------------------------------------------------
# Inspector — separate executable. Takes a path to an OBJ on the
# CLI and opens the in-engine editor. New tools (forward-axis,
# weapon mount points, …) live under src/inspector/ and just need
# to be appended to this source list.
# ---------------------------------------------------------------
add_executable(terra-siege-inspect
    src/inspector/main.cpp
    src/inspector/Inspector.cpp
    src/inspector/Inspector.hpp
    src/inspector/InspectorConfig.cpp
    src/inspector/InspectorConfig.hpp
    src/inspector/InspectorFont.cpp
    src/inspector/InspectorFont.hpp
    src/inspector/AITool.cpp
    src/inspector/AITool.hpp
    src/inspector/FXTool.cpp
    src/inspector/FXTool.hpp
    src/inspector/HardpointsTool.cpp
    src/inspector/HardpointsTool.hpp
    src/inspector/HullTool.cpp
    src/inspector/HullTool.hpp
    src/inspector/IdentityTool.cpp
    src/inspector/IdentityTool.hpp
    src/inspector/MenuBar.cpp
    src/inspector/MenuBar.hpp
    src/inspector/PrimitivesTool.cpp
    src/inspector/PrimitivesTool.hpp
    src/inspector/ProfileTool.cpp
    src/inspector/ProfileTool.hpp
    src/inspector/ShieldsTool.cpp
    src/inspector/ShieldsTool.hpp
    src/inspector/Tool.hpp
    src/inspector/VertexTool.cpp
    src/inspector/VertexTool.hpp
    src/inspector/WeaponsTool.cpp
    src/inspector/WeaponsTool.hpp
)
target_include_directories(terra-siege-inspect PRIVATE src)
target_link_libraries(terra-siege-inspect PRIVATE terra_siege_mesh)
target_compile_definitions(terra-siege-inspect PRIVATE RAYMATH_IMPLEMENTATION)
target_compile_options(terra-siege-inspect PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
if(UNIX AND NOT APPLE)
    target_link_libraries(terra-siege-inspect PRIVATE m dl pthread)
endif()

# ---------------------------------------------------------------
# Tests — doctest-based, opt-in via TERRA_SIEGE_BUILD_TESTS.
# Each test_*.cpp is its own executable so failures bisect easily.
# Tests link the shared mesh lib, NOT the whole game, so they stay
# fast to build.
# ---------------------------------------------------------------
if(TERRA_SIEGE_BUILD_TESTS)
    enable_testing()
    add_executable(test_obj_loader tests/test_obj_loader.cpp)
    target_link_libraries(test_obj_loader PRIVATE doctest terra_siege_mesh)
    target_compile_definitions(test_obj_loader PRIVATE
        TERRA_SIEGE_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME obj_loader COMMAND test_obj_loader)

    # Inspector save (T-06 / T-07 + edit-marker dedupe + count mismatch).
    add_executable(test_obj_save tests/test_obj_save.cpp)
    target_link_libraries(test_obj_save PRIVATE doctest terra_siege_mesh)
    add_test(NAME obj_save COMMAND test_obj_save)

    # Sidecar profile load/save (F.1) — defaults, partial files,
    # unknown-key preservation, forward/scale/pivot roundtrip.
    add_executable(test_sidecar_profile tests/test_sidecar_profile.cpp)
    target_link_libraries(test_sidecar_profile PRIVATE doctest terra_siege_mesh)
    add_test(NAME sidecar_profile COMMAND test_sidecar_profile)
endif()

message(STATUS "terra-siege ${PROJECT_VERSION} | Build: ${CMAKE_BUILD_TYPE} | C++${CMAKE_CXX_STANDARD}")
