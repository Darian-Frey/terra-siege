#pragma once

#include "entity/Player.hpp"
#include "raylib.h"
#include "world/Planet.hpp"
#include <cstdint>

// ====================================================================
// GameState — top-level state machine
// ====================================================================

enum class AppState { MainMenu, Playing, Paused, GameOver, Victory };
enum class CamMode { Follow, FreeRoam }; // F1 toggles in dev mode

// Five-view player camera system. Selected with keys 1–5 while in
// Follow CamMode. Do NOT rename to CameraMode (raylib typedef collision)
// nor to CamMode (already used for the dev Follow/FreeRoam toggle).
enum class CameraView {
  Chase = 0,      // Key 1 — follows ship nose, default combat view
  Velocity = 1,   // Key 2 — follows velocity vector
  Tactical = 2,   // Key 3 — fixed overhead, north-up
  ThreatLock = 3, // Key 4 — rotates to keep nearest enemy in frame
  Classic = 4,    // Key 5 — original Virus fixed diagonal-down
};

class GameState {
public:
  void init();
  void update(float dt);
  void render(float alpha);
  void shutdown();

  AppState state() const { return m_state; }

private:
  // Update helpers
  void updateFreeCamera(float dt);

  // Five-view player camera system (rebuild Step 3, see camera_system.md).
  // updateCamera() dispatches to the appropriate per-view update based on
  // m_cameraView. handleCameraViewKeys() reads keys 1–5 and rotates views.
  // handleCameraZoom() reads the wheel + bracket keys and writes the
  // per-view zoom slot. Each update*Camera reads its slot for FOV (or
  // altitude in the Tactical case).
  void updateCamera(float dt);
  void handleCameraViewKeys();
  void handleCameraZoom();
  void updateChaseCamera(float dt);
  void updateVelocityCamera(float dt);
  void updateTacticalCamera(float dt);
  void updateThreatLockCamera(float dt);
  void updateClassicCamera(float dt);
  // Reset per-view zoom slots to their spec defaults.
  void resetCameraZoom();

  void handleDevKeys();

  // World loading — called from init() and from F5 (DEV_MODE) to reroll seed
  void loadWorld(uint32_t seed);

  // Render helpers
  void drawHUD() const;
  void drawDebugPanel() const;
  void drawCameraViewLabel() const;     // 2-second fade label on view switch
  void drawTacticalShipArrow() const;   // ship heading indicator (Tactical view)
  void drawClassicCompassRose() const;  // world-north reference (Classic view)
  void drawFlightHUD() const;           // wireframe attitude/heading/FPV/speed

  // State
  AppState m_state = AppState::MainMenu;
  CamMode m_camMode = CamMode::Follow;
  uint32_t m_seed = 0;

  // Five-view camera state
  CameraView m_cameraView = CameraView::Chase;
  float m_viewLabelTimer = 0.0f; // counts down from CAM_VIEW_LABEL_DURATION
  float m_threatCamYaw = 0.0f;   // current threat-lock camera yaw (rad)
  // Per-view zoom memory. Slot interpretation depends on the view:
  //   Chase / Velocity / ThreatLock / Classic : FOV in degrees
  //   Tactical                                : altitude in world units
  // Defaults are loaded in init() from Config.
  float m_zoom[5] = {0};

  // World
  Planet m_planet;
  Player m_player;

  // Camera
  Camera3D m_camera = {};
  float m_camYaw = 0.0f;
  float m_camPitch = -0.35f;
  float m_camSpeed = 40.0f;
};