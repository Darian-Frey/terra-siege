#pragma once

#include "core/Particles.hpp"
#include "core/Settings.hpp"
#include "entity/EntityManager.hpp"
#include "entity/Player.hpp"
#include "hud/Radar.hpp"
#include "mesh/EntityProfileRegistry.hpp"
#include "mesh/MeshRegistry.hpp"
#include "raylib.h"
#include "wave/WaveManager.hpp"
#include "world/Planet.hpp"
#include <array>
#include <cstdint>
#include <string>

// ====================================================================
// GameState — top-level state machine
// ====================================================================

enum class AppState {
  MainMenu,
  // Pre-flight loadout selection. Sits between MainMenu and Playing;
  // exited by clicking LAUNCH (applies the chosen weapons to the
  // player and transitions to Playing) or BACK (returns to MainMenu).
  LoadoutSelect,
  Playing,
  Paused,
  GameOver,
  Victory,
};
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

  // Menu system — drawn over the live world. Mouse cursor is enabled
  // while any of these are open; Player input is suspended in Paused
  // and MainMenu states.
  void drawMainMenu();
  void drawPauseMenu();
  void drawSettingsPanel();
  void drawLoadoutSelect();

  // State transitions
  void enterMainMenu();
  void enterLoadoutSelect(); // open pre-flight loadout picker
  void enterPlaying();       // apply loadout + start combat
  void enterPaused();
  void resumePlaying();  // unpause back to Playing without resetting world
  void resetCombat();    // clear entities, respawn fighters, reset player

  // Apply the currently-selected loadout to the player. Called by
  // enterPlaying() after resetCombat() has respawned the player.
  void applyLoadout();

  // Apply current settings to runtime state (player flags, etc.).
  // Live-applied settings call this on each toggle.
  void applyLiveSettings();

  // Cursor visibility — toggled on state changes. raylib's
  // DisableCursor locks for FPS-style mouse look while EnableCursor
  // returns it for menu interaction.
  void setCursorForGameplay(bool inGameplay);

  // HUD typography — load a system TTF (DejaVu Sans Mono Bold on
  // Mint/Ubuntu/Debian; falls back to raylib's bitmap default if no
  // candidate path is found). drawHudText/measureHudText use the
  // loaded font when available and add a 1-pixel black drop-shadow
  // for legibility against busy world backgrounds. Replaces all
  // direct DrawText / MeasureText calls in the HUD path.
  void initHudFont();
  void drawHudText(const char *text, int x, int y, int size, Color col) const;
  int measureHudText(const char *text, int size) const;

  // Menu / settings UI primitives — moved out of the anonymous
  // namespace so they can use drawHudText. Returns true on click.
  bool drawMenuButton(Rectangle r, const char *label, Vector2 mouse,
                      bool clickEdge) const;
  bool drawSettingsToggleRow(Rectangle row, const char *label,
                             const char *value, Vector2 mouse,
                             bool clickEdge) const;

  // Edge-triggered key check — true ONLY on the physics tick where
  // a key transitions from up to down. Required because raylib's
  // IsKeyPressed() returns true on every IsKeyPressed call within a
  // single render frame, and our fixed-timestep loop runs the physics
  // tick (and therefore handleDevKeys) multiple times per render —
  // so IsKeyPressed would fire repeatedly for one physical press,
  // causing F-key actions to cycle multiple times per tap.
  bool keyPressedEdge(int key);

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

  // Menu / settings state
  Settings m_settings;
  std::string m_settingsPath;
  bool m_settingsOpen = false;       // settings panel overlaid on Main/Pause
  // main.cpp calls DisableCursor() at startup, so the cursor IS hidden
  // when init() runs. The flag must match reality or setCursorForGameplay()
  // will skip the EnableCursor() call when entering the main menu.
  bool m_cursorHidden = true;
  // Cached mouse-click state for one-shot click detection
  bool m_lastClickState = false;

  // Player has died but the world keeps running until the wreck
  // hits the ground — then we fire the final explosion and switch
  // to GameOver. Reset on every transition into Playing.
  bool m_playerDeathHandled = false;

  // Per-key was-down state for keyPressedEdge(). Sized large enough
  // for raylib's KEY_KB_MENU (348) plus headroom.
  std::array<bool, 512> m_keyWasDown{};

  // Beam Laser this-tick render state. The beam damages and updates
  // each physics tick, but it's drawn from the 3D pass; we stash the
  // endpoints + hit id here so the render path can pull them.
  bool m_beamActive = false;
  Vector3 m_beamLineFrom{};
  Vector3 m_beamLineTo{};
  uint32_t m_beamHitId = 0;

  // Auto Turret cooldown lives on the gameplay-side (GameState owns
  // EntityManager and the firing logic), not on Player. The Player
  // only flags whether the subsystem is enabled.
  float m_autoTurretTimer = 0.0f;

  // Friendly-unit snapshot — count placed at round start. Compared
  // each tick to the live count to drive the "all friendlies dead"
  // game-over condition. Reset by spawnFriendliesForRound().
  int m_friendlyTotalAtStart = 0;
  bool m_friendliesLostHandled = false;

  // Pre-flight loadout (5f). Selected on the LoadoutSelect screen,
  // applied to the player by applyLoadout() right before Playing
  // begins. Persists across restarts so "Restart" from GameOver
  // can skip the picker and re-use the last loadout.
  struct Loadout {
    Player::PrimaryWeapon primary = Player::PrimaryWeapon::Cannon;
    Player::SecondaryWeapon secondary = Player::SecondaryWeapon::Missile;
    Player::SpecialWeapon special = Player::SpecialWeapon::EMP;
    bool autoTurret = false;
  };
  Loadout m_loadout{};

  // Spawn the round's friendly roster on terrain. Called from
  // loadWorld(); deterministic per-seed.
  void spawnFriendliesForRound(Vector3 playerStart);

  // HUD font handle. Loaded in init() via initHudFont(); falls back
  // to the raylib default when no system TTF is found.
  Font m_hudFont = {};
  bool m_hudFontLoaded = false;

  // World
  Planet m_planet;
  Player m_player;
  ParticleSystem m_particles;
  EntityManager m_em;
  Radar m_radar;
  WaveManager m_waves;
  // Mesh registry — startup-loaded raylib Models indexed by EntityType
  // (and a dedicated player slot). Entities not yet migrated to OBJ
  // keep their procedural render path; the registry returns false
  // from has() for those slots.
  tsmesh::MeshRegistry m_meshRegistry;
  // Parallel sidecar profile registry (F.2). Loaded at init() right
  // after MeshRegistry from the same assets/meshes/ directory; missing
  // sidecars are fine (the consumer falls back to Config defaults).
  tsmesh::EntityProfileRegistry m_profileRegistry;

  // Camera
  Camera3D m_camera = {};
  float m_camYaw = 0.0f;
  float m_camPitch = -0.35f;
  float m_camSpeed = 40.0f;
};