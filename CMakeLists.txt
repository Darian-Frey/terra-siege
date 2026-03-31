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

# ---------------------------------------------------------------
# Source files
# ---------------------------------------------------------------
set(SOURCES
    src/main.cpp

    src/core/GameState.cpp
    src/core/GameState.hpp
    src/core/Clock.hpp
    src/core/Config.hpp

    src/world/Planet.cpp
    src/world/Planet.hpp
    src/world/Heightmap.cpp
    src/world/Heightmap.hpp
    src/world/TerrainChunk.cpp
    src/world/TerrainChunk.hpp
    src/world/SkyDome.cpp
    src/world/SkyDome.hpp

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

add_executable(terra-siege ${SOURCES})

target_include_directories(terra-siege PRIVATE src)

target_link_libraries(terra-siege PRIVATE raylib)

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

message(STATUS "terra-siege ${PROJECT_VERSION} | Build: ${CMAKE_BUILD_TYPE} | C++${CMAKE_CXX_STANDARD}")