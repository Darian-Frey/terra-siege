#include "GameState.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#ifdef DEV_MODE
#include <filesystem>
#include <fstream>

#ifndef TERRA_SIEGE_PROJECT_ROOT
#define TERRA_SIEGE_PROJECT_ROOT "."
#endif

static const std::string g_logDir =
    std::string(TERRA_SIEGE_PROJECT_ROOT) + "/tests/logs";

static void ensureLogDir() {
  std::error_code ec;
  std::filesystem::create_directories(g_logDir, ec);
}

static std::ofstream g_logFile;
static float g_logTimer = 0.0f;
static constexpr float LOG_INTERVAL = 0.1f; // log every 100ms

static void openLog() {
  ensureLogDir();
  g_logFile.open(g_logDir + "/terra-siege-debug.log",
                 std::ios::out | std::ios::trunc);
  if (g_logFile.is_open()) {
    g_logFile << "time,ship_x,ship_y,ship_z,yaw,roll,pitch_vis,speed,"
                 "screen_x,screen_y,keys\n";
  }
}

static void writeLog(float time, const Camera3D &cam, const Player &player) {
  if (!g_logFile.is_open())
    return;

  // Build key string
  char keys[64] = {};
  int k = 0;
  if (IsKeyDown(KEY_W))           k += snprintf(keys + k, sizeof(keys) - k, "W ");
  if (IsKeyDown(KEY_S))           k += snprintf(keys + k, sizeof(keys) - k, "S ");
  if (IsKeyDown(KEY_A))           k += snprintf(keys + k, sizeof(keys) - k, "A ");
  if (IsKeyDown(KEY_D))           k += snprintf(keys + k, sizeof(keys) - k, "D ");
  if (IsKeyDown(KEY_Q))           k += snprintf(keys + k, sizeof(keys) - k, "Q ");
  if (IsKeyDown(KEY_E))           k += snprintf(keys + k, sizeof(keys) - k, "E ");
  if (IsKeyDown(KEY_UP))          k += snprintf(keys + k, sizeof(keys) - k, "UP ");
  if (IsKeyDown(KEY_DOWN))        k += snprintf(keys + k, sizeof(keys) - k, "DN ");
  if (IsKeyDown(KEY_LEFT))        k += snprintf(keys + k, sizeof(keys) - k, "LT ");
  if (IsKeyDown(KEY_RIGHT))       k += snprintf(keys + k, sizeof(keys) - k, "RT ");
  if (IsKeyDown(KEY_LEFT_SHIFT))  k += snprintf(keys + k, sizeof(keys) - k, "SHIFT ");
  if (k == 0) snprintf(keys, sizeof(keys), "NONE");

  Vector3 p = player.position();
  Vector2 screenPos = GetWorldToScreen(p, cam);

  g_logFile << time << ","
            << p.x << "," << p.y << "," << p.z << ","
            << player.yaw() << "," << player.roll() << ","
            << player.pitchVis() << "," << player.speed() << ","
            << screenPos.x << "," << screenPos.y << ","
            << keys << "\n";
}

static void closeLog() {
  if (g_logFile.is_open()) {
    g_logFile.flush();
    g_logFile.close();
  }
}

// ================================================================
// Flight recorder — F4 toggled burst capture of flight data
// Records every physics tick (120Hz) to flight-recording.csv for
// precision analysis of flight behavior.
// ================================================================
static std::ofstream g_recordFile;
static bool g_recording = false;
static float g_recordStartTime = 0.0f;

static void startRecording() {
  ensureLogDir();
  g_recordFile.open(g_logDir + "/flight-recording.csv",
                    std::ios::out | std::ios::trunc);
  if (g_recordFile.is_open()) {
    g_recordFile
        << "t,ship_x,ship_y,ship_z,vel_x,vel_y,vel_z,speed,thrust_charge,"
           "yaw_rad,pitch_rad,roll_rad,yaw_deg,pitch_deg,roll_deg,"
           "fwd_x,fwd_y,fwd_z,agl,thrusting,"
           "cam_x,cam_y,cam_z,cam_off_x,cam_off_y,cam_off_z,cam_dist,cam_fov,"
           "keys,mouse_dx,mouse_dy\n";
    g_recordStartTime = static_cast<float>(GetTime());
    g_recording = true;
    TraceLog(LOG_INFO, "Flight recorder: STARTED");
  }
}

static void stopRecording() {
  if (g_recording) {
    g_recordFile.flush();
    g_recordFile.close();
    g_recording = false;
    TraceLog(LOG_INFO, "Flight recorder: STOPPED (saved flight-recording.csv)");
  }
}

static bool isRecording() { return g_recording; }

static void recordSample(const Camera3D &cam, const Player &player,
                         const Planet &planet) {
  if (!g_recording || !g_recordFile.is_open())
    return;

  // Keys currently held
  char keys[64] = {};
  int k = 0;
  if (IsKeyDown(KEY_W))          k += snprintf(keys + k, sizeof(keys) - k, "W ");
  if (IsKeyDown(KEY_S))          k += snprintf(keys + k, sizeof(keys) - k, "S ");
  if (IsKeyDown(KEY_A))          k += snprintf(keys + k, sizeof(keys) - k, "A ");
  if (IsKeyDown(KEY_D))          k += snprintf(keys + k, sizeof(keys) - k, "D ");
  if (IsKeyDown(KEY_UP))         k += snprintf(keys + k, sizeof(keys) - k, "UP ");
  if (IsKeyDown(KEY_DOWN))       k += snprintf(keys + k, sizeof(keys) - k, "DN ");
  if (IsKeyDown(KEY_LEFT_SHIFT)) k += snprintf(keys + k, sizeof(keys) - k, "SHIFT ");
  if (k == 0) snprintf(keys, sizeof(keys), "-");

  float t = static_cast<float>(GetTime()) - g_recordStartTime;
  Vector3 p = player.position();
  Vector3 v = player.velocity();
  Vector3 f = player.forward();
  float agl = p.y - planet.heightAt(p.x, p.z);
  const float rad2deg = 180.0f / 3.14159265f;
  Vector2 md = GetMouseDelta();

  // Camera offset relative to ship — what we ultimately care about for
  // the chase-cam feel. cam_dist = scalar distance from ship to camera.
  Vector3 camOff = {cam.position.x - p.x, cam.position.y - p.y,
                    cam.position.z - p.z};
  float camDist =
      sqrtf(camOff.x * camOff.x + camOff.y * camOff.y + camOff.z * camOff.z);

  g_recordFile << t << ","
               << p.x << "," << p.y << "," << p.z << ","
               << v.x << "," << v.y << "," << v.z << ","
               << player.speed() << "," << player.thrustCharge() << ","
               << player.yaw() << "," << player.pitch() << ","
               << player.roll() << ","
               << player.yaw() * rad2deg << ","
               << player.pitch() * rad2deg << ","
               << player.roll() * rad2deg << ","
               << f.x << "," << f.y << "," << f.z << ","
               << agl << ","
               << (player.isThrusting() ? 1 : 0) << ","
               << cam.position.x << "," << cam.position.y << "," << cam.position.z << ","
               << camOff.x << "," << camOff.y << "," << camOff.z << ","
               << camDist << "," << cam.fovy << ","
               << keys << ","
               << md.x << "," << md.y << "\n";
}
#endif

// ================================================================
// init
// ================================================================
void GameState::init() {
  double t0 = GetTime();
  m_particles.init();
  initHudFont();
  loadWorld(12345u); // stable default seed — DEV_MODE F5 rerolls
  double t1 = GetTime();
  TraceLog(LOG_INFO, "Terrain generation: %.2f seconds (%d x %d heightmap)",
           t1 - t0, Config::HEIGHTMAP_SIZE, Config::HEIGHTMAP_SIZE);

  // Camera setup
  m_camera.fovy = Config::CAM_FOV;
  m_camera.projection = CAMERA_PERSPECTIVE;
  m_camera.up = {0.0f, 1.0f, 0.0f};

  // Initialise follow camera at sensible position
  Vector3 startPos = m_player.position();
  Vector3 fwd = m_player.forward();
  m_camera.position =
      Vector3Add(startPos, Vector3Add(Vector3Scale(fwd, -Config::CAM_DISTANCE),
                                      {0, Config::CAM_HEIGHT, 0}));
  m_camera.target = Vector3Add(startPos, {0, 1.5f, 0});

  m_camMode = CamMode::Follow;
  resetCameraZoom();

  // Load settings (creates the file on first save). Apply to player +
  // camera before entering MainMenu so the menu reflects current state.
  m_settingsPath = Settings::defaultPath();
  m_settings.load(m_settingsPath);
  applyLiveSettings();
  m_cameraView = static_cast<CameraView>(m_settings.defaultView);

  // Land in MainMenu. The world is already generated and visible
  // behind the menu overlay so the user sees what they'll be flying.
  enterMainMenu();

#ifdef DEV_MODE
  openLog();
#endif
}

// ================================================================
// loadWorld — generate terrain for the given seed and place the player
// above the world centre. Used by init() and the DEV_MODE seed-reroll
// hotkey. Progress callback drives the loading screen.
// ================================================================
void GameState::loadWorld(uint32_t seed) {
  m_seed = seed;

  // Throttle the loading-screen render to ~30 fps so we don't spend
  // meaningful time on UI during chunk meshing.
  double lastDraw = 0.0;
  auto progressCb = [&lastDraw, this](const char *step, float progress) {
    double now = GetTime();
    if (progress < 1.0f && (now - lastDraw) < 0.033) return;
    lastDraw = now;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    BeginDrawing();
    ClearBackground({18, 28, 48, 255});

    const char *title = "TERRA-SIEGE";
    int tw = measureHudText(title, 48);
    drawHudText(title, sw / 2 - tw / 2, sh / 2 - 110, 48, {230, 235, 245, 255});

    const char *sub = "a modern reimagining of Virus (1988)";
    int sbw = measureHudText(sub, 16);
    drawHudText(sub, sw / 2 - sbw / 2, sh / 2 - 60, 16, {150, 165, 185, 220});

    int stepW = measureHudText(step, 20);
    drawHudText(step, sw / 2 - stepW / 2, sh / 2 + 5, 20, {200, 215, 235, 255});

    const int barW = 440, barH = 18;
    const int barX = sw / 2 - barW / 2, barY = sh / 2 + 50;
    DrawRectangle(barX - 2, barY - 2, barW + 4, barH + 4,
                  {80, 100, 130, 255});
    DrawRectangle(barX, barY, barW, barH, {30, 40, 60, 255});
    int fillW = static_cast<int>(barW * progress);
    if (fillW > 0)
      DrawRectangle(barX, barY, fillW, barH, {90, 200, 110, 255});

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(progress * 100.0f));
    int pw = measureHudText(pct, 18);
    drawHudText(pct, sw / 2 - pw / 2, barY + 30, 18, {180, 200, 225, 255});

    EndDrawing();
  };

  m_planet.generate(seed, progressCb);
  m_particles.clear();
  m_em.clear();

  // Spawn above world centre facing +Z (north).
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);
  Vector3 startPos = {mid, ground + 12.0f, mid};
  m_player.init(startPos, Config::FLIGHT_ASSIST_DEFAULT);

  // WaveManager drives all enemy spawning from here on — first wave
  // begins after WAVE_FIRST_DELAY seconds so the player has time to
  // orient before the first contact.
  m_waves.reset();
}

// ================================================================
// Dev keys (only compiled/active in DEV_MODE)
// ================================================================
// ----------------------------------------------------------------
// HUD typography — load a Linux system TTF (DejaVu Sans Mono Bold is
// standard on Mint/Ubuntu/Debian). Anti-aliased glyphs at every size
// instead of the blocky raylib bitmap default. drawHudText/measureHudText
// are the one-stop replacements for DrawText / MeasureText across the
// entire HUD path; they add a 1-pixel black drop-shadow for legibility.
// ----------------------------------------------------------------
void GameState::initHudFont() {
  static const char *candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
      "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf",
      "/Library/Fonts/Menlo.ttc",
  };
  for (const char *path : candidates) {
    if (!FileExists(path)) continue;
    // 36-px atlas — supports HUD sizes 10-22 cleanly via bilinear
    // filtering. Larger atlas would help the menu's 28pt + main-menu
    // 64pt, but those few sites can request bigger sizes and accept
    // mild blur.
    Font f = LoadFontEx(path, 36, nullptr, 0);
    if (f.texture.id == 0) continue;
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    m_hudFont = f;
    m_hudFontLoaded = true;
    TraceLog(LOG_INFO, "HUD font loaded: %s", path);
    return;
  }
  TraceLog(LOG_INFO,
           "HUD font: no system TTF found, falling back to raylib default");
}

void GameState::drawHudText(const char *text, int x, int y, int size,
                            Color col) const {
  Color shadow = {0, 0, 0, 200};
  if (m_hudFontLoaded) {
    Vector2 sp = {static_cast<float>(x + 1), static_cast<float>(y + 1)};
    Vector2 p = {static_cast<float>(x), static_cast<float>(y)};
    DrawTextEx(m_hudFont, text, sp, static_cast<float>(size), 1.0f, shadow);
    DrawTextEx(m_hudFont, text, p, static_cast<float>(size), 1.0f, col);
  } else {
    drawHudText(text, x + 1, y + 1, size, shadow);
    drawHudText(text, x, y, size, col);
  }
}

int GameState::measureHudText(const char *text, int size) const {
  if (m_hudFontLoaded) {
    return static_cast<int>(
        MeasureTextEx(m_hudFont, text, static_cast<float>(size), 1.0f).x);
  }
  return measureHudText(text, size);
}

// Menu UI primitives — moved from anonymous namespace so they can use
// the loaded HUD font via drawHudText / measureHudText.
bool GameState::drawMenuButton(Rectangle r, const char *label, Vector2 mouse,
                               bool clickEdge) const {
  bool hover = CheckCollisionPointRec(mouse, r);
  Color bg = hover ? Color{60, 80, 120, 240} : Color{30, 40, 60, 220};
  Color border = hover ? Color{220, 230, 255, 255}
                       : Color{120, 140, 180, 200};
  Color fg = hover ? Color{255, 255, 255, 255} : Color{200, 210, 230, 255};
  DrawRectangleRec(r, bg);
  DrawRectangleLinesEx(r, 2.0f, border);
  int tw = measureHudText(label, 22);
  drawHudText(label, static_cast<int>(r.x + r.width / 2 - tw / 2),
              static_cast<int>(r.y + r.height / 2 - 11), 22, fg);
  return hover && clickEdge;
}

bool GameState::drawSettingsToggleRow(Rectangle row, const char *label,
                                      const char *valueText, Vector2 mouse,
                                      bool clickEdge) const {
  drawHudText(label, static_cast<int>(row.x + 14),
              static_cast<int>(row.y + row.height / 2 - 9), 18,
              {220, 225, 240, 255});

  Rectangle btn = {row.x + row.width - 130.0f, row.y + 6.0f, 116.0f,
                   row.height - 12.0f};
  bool hover = CheckCollisionPointRec(mouse, btn);
  Color bg = hover ? Color{80, 110, 160, 240} : Color{40, 60, 90, 220};
  Color border = hover ? Color{220, 230, 255, 255}
                       : Color{100, 130, 170, 200};
  Color fg = hover ? Color{255, 255, 255, 255} : Color{220, 230, 250, 255};
  DrawRectangleRec(btn, bg);
  DrawRectangleLinesEx(btn, 1.5f, border);
  int tw = measureHudText(valueText, 16);
  drawHudText(valueText, static_cast<int>(btn.x + btn.width / 2 - tw / 2),
              static_cast<int>(btn.y + btn.height / 2 - 8), 16, fg);
  return hover && clickEdge;
}

// Edge-triggered key check — see header for rationale.
bool GameState::keyPressedEdge(int key) {
  if (key < 0 || static_cast<size_t>(key) >= m_keyWasDown.size())
    return false;
  bool isDown = IsKeyDown(key);
  bool edge = isDown && !m_keyWasDown[static_cast<size_t>(key)];
  m_keyWasDown[static_cast<size_t>(key)] = isDown;
  return edge;
}

void GameState::handleDevKeys() {
#ifdef DEV_MODE
  // F1 — toggle camera mode
  if (keyPressedEdge(KEY_F1)) {
    if (m_camMode == CamMode::Follow) {
      m_camMode = CamMode::FreeRoam;
      // Initialise free-roam from current follow camera position
      Vector3 dir =
          Vector3Normalize(Vector3Subtract(m_camera.target, m_camera.position));
      m_camYaw = atan2f(dir.x, dir.z);
      m_camPitch = asinf(dir.y);
    } else {
      m_camMode = CamMode::Follow;
    }
  }

  // F2 — next flight assist level
  if (keyPressedEdge(KEY_F2)) {
    int next = (m_player.flightAssist() + 1) % 4;
    m_player.setFlightAssist(next);
  }

  // F3 — toggle infinite thrust charge (testing aid; full god mode in Phase 3+)
  if (keyPressedEdge(KEY_F3)) {
    bool on = !m_player.godMode();
    m_player.setGodMode(on);
    TraceLog(LOG_INFO, "God mode: %s (infinite thrust + invincible + "
                       "infinite weapons)",
             on ? "ON" : "OFF");
  }

  // F4 — toggle flight recorder
  if (keyPressedEdge(KEY_F4)) {
    if (isRecording())
      stopRecording();
    else
      startRecording();
  }

  // F7 — skip wave (dev). Silently kills every alive enemy and flushes
  // any pending spawns so the wave-clear check fires next tick and the
  // intermission timer kicks straight in. Lets us walk through the
  // wave table while tuning new enemy types without grinding kills.
  if (keyPressedEdge(KEY_F7)) {
    m_em.killAllEnemies();
    m_waves.skipRemainingSpawns();
    TraceLog(LOG_INFO, "[dev] Skip wave — wave %d cleared",
             m_waves.currentWave());
  }

  // F6 — dump current heightmap to disk for offline inspection.
  // Writes a greyscale PNG and a text dump (stats + ASCII preview) to
  // tests/logs/heightmap-<unixtime>.{png,txt}. The ASCII preview is
  // human-readable so it can be inspected directly without an image
  // viewer.
  if (keyPressedEdge(KEY_F6)) {
    ensureLogDir();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s/heightmap-%lld",
                  g_logDir.c_str(),
                  static_cast<long long>(std::time(nullptr)));
    std::string stem = buf;
    m_planet.exportHeightmap(stem);
    TraceLog(LOG_INFO, "Heightmap dumped: %s.{png,txt}", stem.c_str());
  }

  // F5 — reroll terrain seed (regenerate world). Useful for confirming
  // that two seeds produce visibly distinct landscapes.
  if (keyPressedEdge(KEY_F5)) {
    uint32_t newSeed =
        static_cast<uint32_t>(std::time(nullptr)) ^ (m_seed * 2654435761u);
    if (newSeed == 0) newSeed = 1;
    TraceLog(LOG_INFO, "Reseeding terrain: 0x%08x -> 0x%08x", m_seed, newSeed);
    loadWorld(newSeed);
    // Re-anchor the camera so it doesn't lerp through the old world.
    Vector3 startPos = m_player.position();
    Vector3 fwd = m_player.forward();
    m_camera.position = Vector3Add(
        startPos, Vector3Add(Vector3Scale(fwd, -Config::CAM_DISTANCE),
                             {0, Config::CAM_HEIGHT, 0}));
    m_camera.target = Vector3Add(startPos, {0, 1.5f, 0});
  }

#endif
}

// ================================================================
// Five-view camera system — see terra_rebuild/camera_system.md
//
// Shared properties:
//  - All views terrain-clamp the camera (heightAt + 3.5) before lerping.
//  - All views position-lerp at CAM_LERP for the position-only smoothing
//    that prevents pop on view switch; orientation updates instantly.
//  - The dispatcher updateCamera() decrements the view-label timer once
//    per tick so the on-switch fade label drains uniformly.
// ================================================================

namespace {
// Apply the standard terrain clamp: camera position is pushed up to be
// at least 3.5 above the ground beneath it AND at least 2.0 above the
// player. Used by every view that follows the player from above.
inline void applyTerrainClamp(Vector3 &pos, const Planet &planet,
                              const Vector3 &playerPos, float groundMargin) {
  float groundH = planet.heightAt(pos.x, pos.z);
  if (pos.y < groundH + groundMargin) pos.y = groundH + groundMargin;
  if (pos.y < playerPos.y + 2.0f) pos.y = playerPos.y + 2.0f;
}
} // namespace

// ----------------------------------------------------------------
// View 1 — Chase (default). Follows ship nose direction (yaw only),
// horizon stays level regardless of ship pitch/roll.
// ----------------------------------------------------------------
void GameState::updateChaseCamera(float dt) {
  Vector3 playerPos = m_player.position();
  float playerYaw = m_player.yaw();
  Vector3 horizFwd = {sinf(playerYaw), 0.0f, cosf(playerYaw)};

  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, -Config::CAM_DISTANCE),
                            {0.0f, Config::CAM_HEIGHT, 0.0f}));
  applyTerrainClamp(desiredPos, m_planet, playerPos, 3.5f);

  float lerpSpeed = Config::CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  m_camera.target = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, 8.0f), {0.0f, 1.5f, 0.0f}));
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = m_zoom[static_cast<int>(CameraView::Chase)];
}

// ----------------------------------------------------------------
// View 2 — Velocity. Same as Chase but oriented along the velocity
// vector instead of the nose direction. Below VELOCITY_CAM_MIN_SPD
// the velocity direction is unstable, so blend smoothly to nose.
// ----------------------------------------------------------------
void GameState::updateVelocityCamera(float dt) {
  Vector3 playerPos = m_player.position();
  Vector3 vel = m_player.velocity();
  float spd = Vector3Length(vel);
  float playerYaw = m_player.yaw();
  Vector3 noseFwd = {sinf(playerYaw), 0.0f, cosf(playerYaw)};

  Vector3 velDir = noseFwd;
  if (spd > 0.01f) {
    Vector3 flatVel = {vel.x, 0.0f, vel.z};
    float flatSpd = Vector3Length(flatVel);
    if (flatSpd > 0.01f)
      velDir = Vector3Scale(flatVel, 1.0f / flatSpd);
  }

  // Linear blend toward nose when nearly stationary — avoids flicker.
  float blendT = spd / Config::VELOCITY_CAM_MIN_SPD;
  if (blendT > 1.0f) blendT = 1.0f;
  float blendToNose = 1.0f - blendT;
  velDir = Vector3Normalize(
      Vector3Add(Vector3Scale(velDir, 1.0f - blendToNose),
                 Vector3Scale(noseFwd, blendToNose)));

  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(velDir, -Config::CAM_DISTANCE),
                            {0.0f, Config::CAM_HEIGHT, 0.0f}));
  applyTerrainClamp(desiredPos, m_planet, playerPos, 3.5f);

  float lerpSpeed = Config::CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  m_camera.target = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(velDir, 8.0f), {0.0f, 1.5f, 0.0f}));
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = m_zoom[static_cast<int>(CameraView::Velocity)];
}

// ----------------------------------------------------------------
// View 3 — Tactical overhead. Fixed altitude looking straight down,
// world-north (+Z) at top of screen. CRITICAL: camera.up = {0,0,1}.
// {0,1,0} produces a degenerate cross product when the look direction
// is also world-up, and the view spins erratically.
// ----------------------------------------------------------------
void GameState::updateTacticalCamera(float dt) {
  Vector3 playerPos = m_player.position();
  // Tactical zoom = altitude (per-view zoom slot)
  float altitude = m_zoom[static_cast<int>(CameraView::Tactical)];
  Vector3 desiredPos = {playerPos.x, playerPos.y + altitude, playerPos.z};

  float lerpSpeed = Config::CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  m_camera.target = playerPos;
  m_camera.up = {0.0f, 0.0f, 1.0f}; // world-+Z = north on screen
  m_camera.fovy = Config::TACTICAL_CAM_FOV;
}

// ----------------------------------------------------------------
// View 4 — Threat-lock. Same chase geometry but yaw rotates to keep
// the highest-priority enemy in frame. With no entities yet (Phase 3
// hasn't shipped), findHighestThreat() returns nullptr and the view
// is identical to Chase — that's the documented stub behaviour.
// Hysteresis logic, rotation cap, and target tracking are all wired
// up so Phase 3 can drop in EntityManager + threat scoring without
// touching the camera code.
// ----------------------------------------------------------------
void GameState::updateThreatLockCamera(float dt) {
  Vector3 playerPos = m_player.position();
  float playerYaw = m_player.yaw();

  // Phase 3 will replace this with EntityManager threat lookup.
  // Until then, no target → fall back to chase orientation.
  void *target = nullptr;
  float desiredCamYaw = playerYaw;

  if (target) {
    // Reserved: enemy-direction logic per spec — populated when
    // EntityManager exists. (Unreachable while target stays nullptr.)
  }

  // Slew current threat-yaw toward desired with rotation cap.
  float maxRot = Config::THREAT_CAM_MAX_ROT * dt;
  float yawError = desiredCamYaw - m_threatCamYaw;
  while (yawError > 3.14159f) yawError -= 6.28318f;
  while (yawError < -3.14159f) yawError += 6.28318f;
  if (yawError > maxRot) yawError = maxRot;
  else if (yawError < -maxRot) yawError = -maxRot;
  m_threatCamYaw += yawError;

  Vector3 camDir = {sinf(m_threatCamYaw), 0.0f, cosf(m_threatCamYaw)};
  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(camDir, -Config::CAM_DISTANCE),
                            {0.0f, Config::CAM_HEIGHT, 0.0f}));
  applyTerrainClamp(desiredPos, m_planet, playerPos, 3.5f);

  float lerpSpeed = Config::CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  m_camera.target = Vector3Add(playerPos, {0.0f, 1.5f, 0.0f});
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = m_zoom[static_cast<int>(CameraView::ThreatLock)];
}

// ----------------------------------------------------------------
// View 5 — Classic. Fixed world-space offset: camera never rotates,
// world-north stays at top of screen. The original Virus / Zarch
// feel — hardest view to learn but full 360° terrain awareness.
// ----------------------------------------------------------------
void GameState::updateClassicCamera(float dt) {
  Vector3 playerPos = m_player.position();
  Vector3 desiredPos = {playerPos.x + Config::CLASSIC_CAM_OFFSET_X,
                        playerPos.y + Config::CLASSIC_CAM_ALTITUDE,
                        playerPos.z + Config::CLASSIC_CAM_OFFSET_Z};
  applyTerrainClamp(desiredPos, m_planet, playerPos, 5.0f);

  float lerpSpeed = Config::CLASSIC_CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  m_camera.target = playerPos;
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = m_zoom[static_cast<int>(CameraView::Classic)];
}

// ----------------------------------------------------------------
// Dispatcher + view-switch keys
// ----------------------------------------------------------------
void GameState::updateCamera(float dt) {
  switch (m_cameraView) {
  case CameraView::Chase:      updateChaseCamera(dt); break;
  case CameraView::Velocity:   updateVelocityCamera(dt); break;
  case CameraView::Tactical:   updateTacticalCamera(dt); break;
  case CameraView::ThreatLock: updateThreatLockCamera(dt); break;
  case CameraView::Classic:    updateClassicCamera(dt); break;
  }
  m_viewLabelTimer -= dt;
  if (m_viewLabelTimer < 0.0f) m_viewLabelTimer = 0.0f;
}

void GameState::resetCameraZoom() {
  m_zoom[static_cast<int>(CameraView::Chase)] = Config::CAM_FOV;
  m_zoom[static_cast<int>(CameraView::Velocity)] = Config::CAM_FOV;
  m_zoom[static_cast<int>(CameraView::Tactical)] = Config::TACTICAL_CAM_ALTITUDE;
  m_zoom[static_cast<int>(CameraView::ThreatLock)] = Config::CAM_FOV;
  m_zoom[static_cast<int>(CameraView::Classic)] = Config::CLASSIC_CAM_FOV;
}

// Mouse wheel + [ / ] step + \ reset, applied to the active view's
// zoom slot. Tactical zoom is altitude (60–250); all other views are
// FOV (50–90). Wheel-up = zoom in (lower FOV / lower altitude).
void GameState::handleCameraZoom() {
  float wheel = GetMouseWheelMove();
  bool stepIn = keyPressedEdge(KEY_LEFT_BRACKET);
  bool stepOut = keyPressedEdge(KEY_RIGHT_BRACKET);
  bool reset = keyPressedEdge(KEY_BACKSLASH);
  if (wheel == 0.0f && !stepIn && !stepOut && !reset) return;

  const int idx = static_cast<int>(m_cameraView);

  if (m_cameraView == CameraView::Tactical) {
    constexpr float STEP = 15.0f;
    constexpr float MIN = 60.0f;
    constexpr float MAX = 250.0f;
    if (reset) m_zoom[idx] = Config::TACTICAL_CAM_ALTITUDE;
    m_zoom[idx] -= wheel * STEP;
    if (stepIn) m_zoom[idx] -= STEP;
    if (stepOut) m_zoom[idx] += STEP;
    if (m_zoom[idx] < MIN) m_zoom[idx] = MIN;
    else if (m_zoom[idx] > MAX) m_zoom[idx] = MAX;
  } else {
    constexpr float STEP = 5.0f;
    constexpr float MIN = 50.0f;
    constexpr float MAX = 90.0f;
    float defaultFov = (m_cameraView == CameraView::Classic)
                           ? Config::CLASSIC_CAM_FOV
                           : Config::CAM_FOV;
    if (reset) m_zoom[idx] = defaultFov;
    m_zoom[idx] -= wheel * STEP;
    if (stepIn) m_zoom[idx] -= STEP;
    if (stepOut) m_zoom[idx] += STEP;
    if (m_zoom[idx] < MIN) m_zoom[idx] = MIN;
    else if (m_zoom[idx] > MAX) m_zoom[idx] = MAX;
  }

  // Re-show the view label so the player gets visual feedback that
  // something changed in this view.
  m_viewLabelTimer = Config::CAM_VIEW_LABEL_DURATION;
}

void GameState::handleCameraViewKeys() {
  CameraView newView = m_cameraView;
  if (keyPressedEdge(KEY_ONE))   newView = CameraView::Chase;
  if (keyPressedEdge(KEY_TWO))   newView = CameraView::Velocity;
  if (keyPressedEdge(KEY_THREE)) newView = CameraView::Tactical;
  if (keyPressedEdge(KEY_FOUR))  newView = CameraView::ThreatLock;
  if (keyPressedEdge(KEY_FIVE))  newView = CameraView::Classic;

  if (newView != m_cameraView) {
    m_cameraView = newView;
    m_viewLabelTimer = Config::CAM_VIEW_LABEL_DURATION;
    // Reset threat yaw when entering ThreatLock so it starts aligned
    // with the player's nose instead of swinging from a stale value.
    if (newView == CameraView::ThreatLock)
      m_threatCamYaw = m_player.yaw();
  }
}

// ================================================================
// Free-roam camera (dev mode)
// ================================================================
void GameState::updateFreeCamera(float dt) {
  Vector2 delta = GetMouseDelta();
  m_camYaw += delta.x * 0.003f;
  m_camPitch -= delta.y * 0.003f;
  if (m_camPitch > 1.4f)
    m_camPitch = 1.4f;
  if (m_camPitch < -1.4f)
    m_camPitch = -1.4f;

  Vector3 fwd = {cosf(m_camPitch) * sinf(m_camYaw), sinf(m_camPitch),
                 cosf(m_camPitch) * cosf(m_camYaw)};
  Vector3 rgt = {sinf(m_camYaw - 1.5708f), 0.0f, cosf(m_camYaw - 1.5708f)};

  float speed = m_camSpeed;
  if (IsKeyDown(KEY_LEFT_SHIFT))
    speed *= 4.0f;

  if (IsKeyDown(KEY_W))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(fwd, speed * dt));
  if (IsKeyDown(KEY_S))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(fwd, -speed * dt));
  if (IsKeyDown(KEY_D))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(rgt, speed * dt));
  if (IsKeyDown(KEY_A))
    m_camera.position =
        Vector3Add(m_camera.position, Vector3Scale(rgt, -speed * dt));
  if (IsKeyDown(KEY_E))
    m_camera.position.y += speed * dt;
  if (IsKeyDown(KEY_Q))
    m_camera.position.y -= speed * dt;

  m_camera.target = Vector3Add(m_camera.position, fwd);
  m_camera.up = {0.0f, 1.0f, 0.0f};
}

// ================================================================
// update
// ================================================================
void GameState::update(float dt) {
  switch (m_state) {
  case AppState::Playing: {
    handleDevKeys();

    // Pause toggle — Esc or P. Only fires from Follow mode; FreeRoam
    // dev cam keeps existing F1 behaviour for symmetry.
    if (m_camMode == CamMode::Follow &&
        (keyPressedEdge(KEY_ESCAPE) || keyPressedEdge(KEY_P))) {
      enterPaused();
      break;
    }

    if (m_camMode == CamMode::Follow) {
      handleCameraViewKeys();
      handleCameraZoom();
      m_player.update(dt, m_planet);
      updateCamera(dt);

      // Engine exhaust — emit while thrust is on. Thruster sits below
      // the ship's belly along its local-down axis; exhaust direction
      // is local-down (= -up()). Particles inherit ship velocity.
      if (m_player.isThrusting()) {
        Vector3 down = Vector3Negate(m_player.up());
        Vector3 thrusterPos =
            Vector3Add(m_player.position(), Vector3Scale(down, 0.6f));
        m_particles.emitExhaust(thrusterPos, down, m_player.velocity(), dt);
      }
      m_particles.update(dt, m_planet);

      // Spawn cannon projectiles armed by Player::update (LMB / Space).
      Vector3 spos, svel;
      if (m_player.consumePendingShot(spos, svel)) {
        m_em.spawnProjectile(spos, svel, Config::CANNON_DAMAGE,
                             Config::CANNON_RANGE, Config::CANNON_SPEED,
                             ProjectileOwner::Player);
      }

      m_em.update(dt, m_planet, m_player, m_particles);
      m_radar.update(dt, m_em, m_player.position(), m_player.yaw(),
                     static_cast<float>(GetTime()));
      // WaveManager spawns enemies on a stagger and drives the
      // wave-clear / intermission pacing.
      m_waves.update(dt, m_em, m_player, m_planet);

      // Death flow — when the player loses all hull they keep falling
      // (input + assist gated off in Player) until the wreck comes to
      // rest on the ground, at which point we fire the final
      // explosion and switch to GameOver. The scene keeps updating
      // through the fall — the original Virus did the same and it
      // reads as "lost power" rather than a hard cut.
      if (!m_player.isAlive() && !m_playerDeathHandled) {
        Vector3 ppos = m_player.position();
        float spd = m_player.speed();
        // Crash logic in Player::applyPhysics zeroes velocity on
        // impact, so spd≈0 reliably signals the wreck has stopped.
        if (spd < 1.0f) {
          m_em.emitKillExplosion(ppos, m_particles);
          m_playerDeathHandled = true;
          m_state = AppState::GameOver;
          setCursorForGameplay(false);
          break;
        }
      }
    } else {
      // Free-roam: player suspended; keys 1–5 are ignored.
      updateFreeCamera(dt);
    }

#ifdef DEV_MODE
    g_logTimer += dt;
    if (g_logTimer >= LOG_INTERVAL) {
      g_logTimer -= LOG_INTERVAL;
      writeLog(static_cast<float>(GetTime()), m_camera, m_player);
    }
    // Flight recorder samples every physics tick (120Hz) for precision
    if (m_camMode == CamMode::Follow) {
      recordSample(m_camera, m_player, m_planet);
    }
#endif
    break;
  }
  default:
    break;
  }
}

// ================================================================
// render
// ================================================================
// ----------------------------------------------------------------
// Camera-view label helper (kept at file scope; trivial pure function).
// ----------------------------------------------------------------
namespace {
const char *cameraViewLabel(int v) {
  switch (v) {
  case 0: return "CHASE";
  case 1: return "VELOCITY";
  case 2: return "TACTICAL";
  case 3: return "THREAT";
  case 4: return "CLASSIC";
  default: return "CHASE";
  }
}
} // namespace

void GameState::render(float alpha) {
  (void)alpha;

  switch (m_state) {
  case AppState::Playing: {
    ClearBackground({55, 80, 140, 255});

    BeginMode3D(m_camera);
    // Override projection matrix to push the far clip plane out from
    // raylib's default 1000 to 3000 so the whole 8km world is visible.
    {
      rlMatrixMode(RL_PROJECTION);
      rlLoadIdentity();
      double aspect = static_cast<double>(GetScreenWidth()) /
                      static_cast<double>(GetScreenHeight());
      double top = 0.05 * tan(m_camera.fovy * 0.5 * DEG2RAD);
      double right = top * aspect;
      rlFrustum(-right, right, -top, top, 0.05, 3000.0);
      rlMatrixMode(RL_MODELVIEW);
    }
    m_planet.draw(m_camera.position);
    m_player.renderGroundShadow(m_planet);
    m_player.render();
    m_em.render();
    m_particles.render(m_camera);
    // Shield bubble drawn AFTER everything else in 3D so its alpha
    // blends over the ship + enemies + particles correctly.
    m_player.renderShieldBubble();
    EndMode3D();

    drawHUD();
    break;
  }
  case AppState::MainMenu:
  case AppState::Paused: {
    // Render the live (or frozen) world as a backdrop so the menu
    // floats over the actual game surface — the player sees what
    // they're about to fly into. Same render path as Playing minus
    // the HUD; the menu draws its own dimming overlay on top.
    ClearBackground({55, 80, 140, 255});
    BeginMode3D(m_camera);
    {
      rlMatrixMode(RL_PROJECTION);
      rlLoadIdentity();
      double aspect = static_cast<double>(GetScreenWidth()) /
                      static_cast<double>(GetScreenHeight());
      double top = 0.05 * tan(m_camera.fovy * 0.5 * DEG2RAD);
      double right = top * aspect;
      rlFrustum(-right, right, -top, top, 0.05, 3000.0);
      rlMatrixMode(RL_MODELVIEW);
    }
    m_planet.draw(m_camera.position);
    m_player.renderGroundShadow(m_planet);
    m_player.render();
    m_em.render();
    m_particles.render(m_camera);
    // Shield bubble drawn AFTER everything else in 3D so its alpha
    // blends over the ship + enemies + particles correctly.
    m_player.renderShieldBubble();
    EndMode3D();

    if (m_state == AppState::MainMenu)
      drawMainMenu();
    else
      drawPauseMenu();
    break;
  }
  case AppState::GameOver: {
    // Render the wreckage scene as a frozen backdrop, then a tinted
    // overlay + restart / main-menu buttons. Same camera path as
    // Playing so the player sees their final position.
    ClearBackground({55, 80, 140, 255});
    BeginMode3D(m_camera);
    {
      rlMatrixMode(RL_PROJECTION);
      rlLoadIdentity();
      double aspect = static_cast<double>(GetScreenWidth()) /
                      static_cast<double>(GetScreenHeight());
      double top = 0.05 * tan(m_camera.fovy * 0.5 * DEG2RAD);
      double right = top * aspect;
      rlFrustum(-right, right, -top, top, 0.05, 3000.0);
      rlMatrixMode(RL_MODELVIEW);
    }
    m_planet.draw(m_camera.position);
    m_player.renderGroundShadow(m_planet);
    m_player.render(); // wreck stays visible at the crash site
    m_em.render();
    m_particles.render(m_camera);
    EndMode3D();

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Vector2 mouse = GetMousePosition();
    bool clickNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool clickEdge = clickNow && !m_lastClickState;

    DrawRectangle(0, 0, sw, sh, {30, 0, 0, 180});
    const char *title = "DESTROYED";
    int tw = measureHudText(title, 64);
    drawHudText(title, sw / 2 - tw / 2, sh / 4, 64, {255, 100, 80, 255});

    const float bw = 280.0f, bh = 50.0f;
    float bx = sw / 2 - bw / 2;
    float by = sh / 2 - 30;
    if (drawMenuButton({bx, by, bw, bh}, "RESTART", mouse, clickEdge))
      enterPlaying();
    by += bh + 14;
    if (drawMenuButton({bx, by, bw, bh}, "MAIN MENU", mouse, clickEdge))
      enterMainMenu();
    m_lastClickState = clickNow;
    break;
  }
  case AppState::Victory:
    ClearBackground(BLACK);
    drawHudText("VICTORY", 40, 40, 28, GREEN);
    break;
  }
}

// ================================================================
// HUD
// ================================================================
void GameState::drawHUD() const {
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();

  DrawFPS(12, 12);

  // ---- Debug info panel (top right) ----
  const Vector3 &pos = m_player.position();
  float terrainH = m_planet.heightAt(pos.x, pos.z);
  float agl = pos.y - terrainH;
  float spd = m_player.speed();

  float yawDeg = m_player.yaw() * (180.0f / 3.14159f);
  while (yawDeg < 0.0f)
    yawDeg += 360.0f;
  while (yawDeg >= 360.0f)
    yawDeg -= 360.0f;

  const char *compass = "N";
  if (yawDeg >= 22.5f && yawDeg < 67.5f)
    compass = "NE";
  else if (yawDeg >= 67.5f && yawDeg < 112.5f)
    compass = "E";
  else if (yawDeg >= 112.5f && yawDeg < 157.5f)
    compass = "SE";
  else if (yawDeg >= 157.5f && yawDeg < 202.5f)
    compass = "S";
  else if (yawDeg >= 202.5f && yawDeg < 247.5f)
    compass = "SW";
  else if (yawDeg >= 247.5f && yawDeg < 292.5f)
    compass = "W";
  else if (yawDeg >= 292.5f && yawDeg < 337.5f)
    compass = "NW";

  // Assist level label
  const char *assistLabels[] = {"RAW", "MINIMAL", "STANDARD", "FULL"};
  const char *assistStr = assistLabels[m_player.flightAssist()];

  char buf[64];
  const int px = sw - 220;
  int py = 12;
  const int lh = 18;
  const Color col = {200, 220, 255, 220};

  float pitchDeg = m_player.pitch() * (180.0f / 3.14159f);
  float rollDeg = m_player.roll() * (180.0f / 3.14159f);
  float chargePct = m_player.thrustCharge();
  bool thrusting = m_player.isThrusting();

  int panelLines = m_camMode == CamMode::FreeRoam ? 11 : 10;
  DrawRectangle(px - 8, py - 4, 216, lh * panelLines + 8, {0, 0, 0, 120});

  snprintf(buf, sizeof(buf), "X: %7.1f", pos.x);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Z: %7.1f", pos.z);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Alt:  %6.1f m", pos.y);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "AGL:  %6.1f m", agl);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Spd:  %5.1f%s", spd, thrusting ? " ^" : "");
  drawHudText(buf, px, py, 14, thrusting ? Color{255, 200, 80, 240} : col);
  py += lh;
  snprintf(buf, sizeof(buf), "Hdg:  %5.1f  %s", yawDeg, compass);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Pch:  %+6.1f°", pitchDeg);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Roll: %+6.1f°", rollDeg);
  drawHudText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Chg:  %5.1f", chargePct);
  drawHudText(buf, px, py, 14, chargePct < 20.0f ? Color{255, 100, 80, 240} : col);
  py += lh;
  snprintf(buf, sizeof(buf), "Asst: %s", assistStr);
  drawHudText(buf, px, py, 14, col);
  py += lh;

#ifdef DEV_MODE
  if (m_camMode == CamMode::FreeRoam) {
    drawHudText("[ FREE CAM ]", px, py, 14, YELLOW);
  }
#endif

  // ---- Health bar (HULL) and thrust charge bar (THRUST), bottom-left ----
  const int bx = 12, bw = 160, bh = 14;
  // Hull
  {
    int by = sh - 80;
    DrawRectangle(bx - 1, by - 1, bw + 2, bh + 2, {0, 0, 0, 160});
    DrawRectangle(bx, by, bw, bh, {50, 50, 50, 255});
    int healthW = static_cast<int>(bw * m_player.health() / 100.0f);
    Color hpCol = healthW > bw / 2   ? Color{40, 200, 60, 255}
                  : healthW > bw / 4 ? Color{220, 180, 0, 255}
                                     : Color{210, 40, 40, 255};
    DrawRectangle(bx, by, healthW, bh, hpCol);
    drawHudText("HULL", bx, by - 14, 12, col);
  }
  // Thrust charge
  {
    int by = sh - 56;
    DrawRectangle(bx - 1, by - 1, bw + 2, bh + 2, {0, 0, 0, 160});
    DrawRectangle(bx, by, bw, bh, {50, 50, 50, 255});
    int chgW = static_cast<int>(bw * m_player.thrustCharge() /
                                Config::NEWTON_THRUST_CHARGE_MAX);
    Color cCol = m_player.thrustCharge() < 20.0f
                     ? Color{255, 100, 80, 255}
                     : Color{80, 180, 255, 255};
    DrawRectangle(bx, by, chgW, bh, cCol);
    drawHudText("THRUST", bx, by - 14, 12, col);
    if (m_player.godMode())
      drawHudText("GOD", bx + bw + 8, by, 14, {255, 200, 80, 240});
  }
  // Landed indicator
  if (m_player.isLanded()) {
    drawHudText("LANDED", bx + bw + 12, sh - 80, 14, {120, 220, 140, 255});
  }

  // ---- Directional shield pie ----
  // Persistent state readout for the 4 shield sectors (the in-world
  // bubble flash is impact feedback only — the pie tells you what's
  // left to absorb the next hit). Front=top, Rear=bottom, Right of
  // pie = Right sector — same heading-up convention as the radar.
  // Wedge colour shifts cyan → red as HP drops; alpha fades so a
  // depleted sector visibly drops out.
  {
    const int piX = bx + bw + 60;
    const int piY = sh - 60; // centre between hull (sh-80) and thrust (sh-56)
    const float radius = 28.0f;
    Vector2 centre = {static_cast<float>(piX), static_cast<float>(piY)};

    // Background ring + dark fill so empty sectors still have a
    // visual "hole" instead of disappearing into the gameplay view.
    DrawCircle(piX, piY, radius + 2.0f, {0, 0, 0, 170});
    DrawCircle(piX, piY, radius, {30, 35, 45, 200});

    // Raylib screen angles: 0° = east, increasing clockwise. We want
    // Front at the top, which is 270° in screen coords. Each wedge
    // spans 90° centred on its cardinal direction.
    struct Wedge {
      float startDeg, endDeg;
      int sector;
    };
    const Wedge wedges[4] = {
        {225.0f, 315.0f, 0}, // Front  (top)
        {315.0f, 405.0f, 2}, // Right
        {45.0f,  135.0f, 1}, // Rear   (bottom)
        {135.0f, 225.0f, 3}, // Left
    };
    for (const Wedge &w : wedges) {
      float frac = m_player.sectorHPFrac(w.sector);
      if (frac <= 0.01f) continue;
      unsigned char r = static_cast<unsigned char>(220.0f - 140.0f * frac);
      unsigned char g = static_cast<unsigned char>(60.0f + 140.0f * frac);
      unsigned char b = static_cast<unsigned char>(40.0f + 200.0f * frac);
      unsigned char a = static_cast<unsigned char>(80.0f + 160.0f * frac);
      Color c = {r, g, b, a};
      DrawCircleSector(centre, radius * 0.95f, w.startDeg, w.endDeg, 20, c);
    }

    // Ring outline so the pie reads as a discrete instrument even
    // when all sectors are depleted.
    DrawRing(centre, radius - 1.0f, radius, 0.0f, 360.0f, 32,
             {180, 200, 220, 200});

    // Cardinal tick — small white dot at 12 o'clock reinforces the
    // heading-up orientation (front sector = top of pie).
    DrawCircle(piX, piY - static_cast<int>(radius) - 4, 1.5f,
               {220, 230, 240, 220});

    drawHudText("SHLD", piX - 14, piY + static_cast<int>(radius) + 4, 12,
                col);
  }

  // ---- AGL tape ----
  // Vertical bar showing the craft's height above the terrain directly
  // beneath it (NOT absolute Y). The ceiling tick marks where thrust
  // cuts out — pilots learn the "stay below the line" rule visually.
  // Bar scaled so the ceiling sits ~80% of the way up, leaving headroom
  // to show the over-ceiling state.
  {
    Vector3 ppos = m_player.position();
    float ground = m_planet.heightAt(ppos.x, ppos.z);
    float agl = ppos.y - ground;
    if (agl < 0.0f) agl = 0.0f;

    const float maxAGL = Config::NEWTON_FLIGHT_CEILING * 1.25f;
    float t = agl / maxAGL;
    if (t > 1.0f) t = 1.0f;

    // Sits to the right of the SPD tape (which is at x=14, width 18).
    // Vertical placement matches the SPD tape (drawFlightHUD) so the
    // two read as a paired left-edge instrument cluster: both centred
    // on screen midline with 220px height.
    const int abx = 50;
    const int abw = 16;
    const int abh = 220;
    const int aby = sh / 2 - abh / 2;

    // Frame
    DrawRectangle(abx - 1, aby - 1, abw + 2, abh + 2, {0, 0, 0, 160});
    DrawRectangle(abx, aby, abw, abh, {40, 40, 50, 230});

    // Fill — colour codes the safety zone
    int fillH = static_cast<int>(abh * t);
    Color fillCol;
    if (agl > Config::NEWTON_FLIGHT_CEILING)
      fillCol = {220, 60, 60, 240}; // red — thrust cuts out
    else if (agl > Config::NEWTON_FLIGHT_CEILING * 0.85f)
      fillCol = {220, 180, 0, 240}; // yellow — approaching ceiling
    else
      fillCol = {80, 200, 100, 240}; // green — normal flight
    DrawRectangle(abx, aby + abh - fillH, abw, fillH, fillCol);

    // Ceiling tick mark
    float ceilT = Config::NEWTON_FLIGHT_CEILING / maxAGL;
    int ceilY = aby + abh - static_cast<int>(abh * ceilT);
    DrawLine(abx - 4, ceilY, abx + abw + 4, ceilY, {255, 220, 80, 220});
    drawHudText("CLG", abx + abw + 6, ceilY - 5, 10, {255, 220, 80, 200});

    // Label + numeric AGL
    drawHudText("AGL", abx - 6, aby - 14, 12, col);
    char abuf[16];
    snprintf(abuf, sizeof(abuf), "%dm", static_cast<int>(agl));
    drawHudText(abuf, abx + abw + 6, aby + abh - 12, 12, col);
  }

  // ---- Controls bar ----
  DrawRectangle(0, sh - 28, sw, 28, {0, 0, 0, 140});

  const char *controls =
      m_camMode == CamMode::Follow
          ? "Mouse: pitch+yaw  |  Wheel/[]: zoom  |  W/LMB: thrust  |  "
            "A/D: yaw  |  Q/E: roll  |  1-5: view  |  \\: zoom reset"
          : "WASD: fly  |  Q/E: up/down  |  Shift: fast  (F1: back to ship)";
  drawHudText(controls, 12, sh - 20, 14, {180, 180, 180, 220});

#ifdef DEV_MODE
  // Render dev key labels right-aligned with a fixed gap between tokens so
  // it stays legible no matter what each label's character count is.
  {
    const Color devCol = YELLOW;
    const char *tokens[] = {"[DEV]",     "F1:cam",   "F2:assist",
                            "F3:godmode", "F4:rec", "F5:reseed",
                            "F6:dump"};
    const int tokenCount = sizeof(tokens) / sizeof(tokens[0]);
    const int gap = 18;
    const int rightMargin = 12;
    int totalW = 0;
    for (int i = 0; i < tokenCount; ++i) {
      totalW += measureHudText(tokens[i], 14);
      if (i + 1 < tokenCount) totalW += gap;
    }
    int x = sw - rightMargin - totalW;
    for (int i = 0; i < tokenCount; ++i) {
      drawHudText(tokens[i], x, sh - 20, 14, devCol);
      x += measureHudText(tokens[i], 14) + gap;
    }
  }

  // Flight recorder indicator — large pulsing badge at top-center
  if (isRecording()) {
    float pulse = 0.5f + 0.5f * sinf(static_cast<float>(GetTime()) * 5.0f);
    unsigned char alpha = static_cast<unsigned char>(160 + 95 * pulse);
    Color bg = {0, 0, 0, alpha};
    Color fg = {255, 50, 50, 255};

    const int bw = 160, bh = 36;
    const int bx = sw / 2 - bw / 2;
    const int by = 10;
    DrawRectangle(bx, by, bw, bh, bg);
    DrawRectangleLines(bx, by, bw, bh, fg);
    DrawCircle(bx + 22, by + bh / 2, 8, fg);
    drawHudText("REC", bx + 42, by + 8, 22, fg);

    // Elapsed time
    float elapsed = static_cast<float>(GetTime()) - g_recordStartTime;
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%.1fs", elapsed);
    drawHudText(tbuf, bx + 100, by + 10, 18, {255, 220, 220, 255});
  }
#endif

  // Wave status — small thin bar at top-centre showing the current
  // wave number plus either intermission countdown or remaining-
  // enemies-this-wave. Sits clear of the heading-tape (which is
  // ~50px wide centered, this bar is wider). Intermission is
  // highlighted brighter so a "next wave coming" alert is obvious.
  {
    char buf[64];
    Color barCol = {0, 0, 0, 140};
    Color textCol = {220, 230, 250, 230};

    switch (m_waves.state()) {
    case WaveManager::State::FirstDelay:
      snprintf(buf, sizeof(buf), "WAVE %d  STARTING IN %.0fs",
               m_waves.currentWave(),
               ceilf(m_waves.firstDelayTimer()));
      textCol = {255, 220, 120, 240};
      break;
    case WaveManager::State::Spawning: {
      int alive = m_waves.aliveEnemyCount(m_em);
      int pending = m_waves.remainingToSpawn();
      snprintf(buf, sizeof(buf), "WAVE %d  %d ENEMIES",
               m_waves.currentWave(), alive + pending);
      break;
    }
    case WaveManager::State::WaveActive: {
      int alive = m_waves.aliveEnemyCount(m_em);
      snprintf(buf, sizeof(buf), "WAVE %d  %d ENEMIES",
               m_waves.currentWave(), alive);
      break;
    }
    case WaveManager::State::Intermission:
      snprintf(buf, sizeof(buf), "WAVE %d CLEAR  NEXT IN %.0fs",
               m_waves.currentWave(),
               ceilf(m_waves.intermissionTimer()));
      textCol = {120, 240, 160, 240};
      barCol = {20, 60, 30, 180};
      break;
    }

    // Stack below the heading tape (which occupies ~y=18-58).
    int tw = measureHudText(buf, 16);
    int bx = sw / 2 - (tw + 24) / 2;
    int by = 64;
    DrawRectangle(bx, by, tw + 24, 22, barCol);
    DrawRectangleLines(bx, by, tw + 24, 22, {120, 150, 180, 180});
    drawHudText(buf, bx + 12, by + 4, 16, textCol);
  }

  // Camera-view overlays — fade label on switch, plus per-view UI.
  drawCameraViewLabel();
  drawTacticalShipArrow();
  drawClassicCompassRose();

  // Wireframe flight HUD — visible in every camera view. Provides the
  // attitude/heading/speed/FPV data a Newtonian pilot needs regardless
  // of which camera is active. Toggled via Settings (m_settings.wireframeHUD).
  if (m_settings.wireframeHUD) drawFlightHUD();

  // Radar Tier 1 — bottom-right disc + altitude strip. Tactical view
  // hides the disc since the screen IS effectively a radar in that
  // mode; only the altitude strip stays for vertical-cue parity.
  // Classic view scales the disc up 20% (camera blindspots are wider
  // there so the player needs more radar info).
  {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    if (m_cameraView == CameraView::Tactical) {
      // Strip-only mode top-right corner.
      const float stripW = 16.0f;
      const float stripH = 120.0f;
      Vector2 stripTL = {sw - stripW - 28.0f, 12.0f};
      m_radar.drawAltitudeStripOnly(stripTL, stripH, stripW);
    } else {
      float scale = (m_cameraView == CameraView::Classic)
                        ? Config::RADAR_CLASSIC_VIEW_SCALE
                        : 1.0f;
      float r = Config::RADAR_DISC_RADIUS_PX * scale;
      // Leave room for the altitude strip + margin to the right.
      Vector2 discCentre = {static_cast<float>(sw) - r - 36.0f,
                            static_cast<float>(sh) - r - 38.0f};
      m_radar.draw(discCentre, r);
    }
  }
}

// ================================================================
// Camera view overlays — see camera_system.md
// ================================================================
void GameState::drawCameraViewLabel() const {
  if (m_viewLabelTimer <= 0.0f) return;

  static const char *labels[] = {
      "CHASE VIEW", "VELOCITY VIEW", "TACTICAL VIEW",
      "THREAT-LOCK VIEW", "CLASSIC VIEW",
  };
  const char *label = labels[static_cast<int>(m_cameraView)];

  // Linear fade over the last CAM_VIEW_LABEL_FADE seconds.
  float a = m_viewLabelTimer / Config::CAM_VIEW_LABEL_FADE;
  if (a > 1.0f) a = 1.0f;
  unsigned char alpha = static_cast<unsigned char>(220.0f * a);
  unsigned char bgAlpha = static_cast<unsigned char>(120.0f * a);

  int sw = GetScreenWidth();
  int tw = measureHudText(label, 22);
  int px = sw / 2 - tw / 2;
  int py = 48;
  DrawRectangle(px - 12, py - 6, tw + 24, 34, {0, 0, 0, bgAlpha});
  drawHudText(label, px, py, 22, {200, 220, 255, alpha});
}

void GameState::drawTacticalShipArrow() const {
  if (m_cameraView != CameraView::Tactical) return;

  Vector3 shipPos = m_player.position();
  Vector2 screenPos = GetWorldToScreen(shipPos, m_camera);
  float yaw = m_player.yaw();

  // In tactical view, +Z (north) is up on screen, so yaw maps directly:
  // X = sin(yaw), screen-Y = -cos(yaw) (screen Y inverted).
  const float arrowLen = 22.0f;
  Vector2 tip = {screenPos.x + sinf(yaw) * arrowLen,
                 screenPos.y - cosf(yaw) * arrowLen};
  DrawLineEx(screenPos, tip, 2.5f, {255, 230, 100, 220});
  DrawCircle(static_cast<int>(tip.x), static_cast<int>(tip.y), 3.0f,
             {255, 230, 100, 220});
}

void GameState::drawClassicCompassRose() const {
  if (m_cameraView != CameraView::Classic) return;

  // Bottom-right corner, above the dev controls bar.
  const int cx = GetScreenWidth() - 50;
  const int cy = GetScreenHeight() - 95;
  const int r = 24;
  DrawCircleLines(cx, cy, static_cast<float>(r), {150, 160, 180, 160});
  drawHudText("N", cx - 4, cy - r - 14, 12, {220, 230, 255, 220});
  drawHudText("S", cx - 4, cy + r + 2, 12, {180, 190, 210, 160});
  drawHudText("E", cx + r + 3, cy - 5, 12, {180, 190, 210, 160});
  drawHudText("W", cx - r - 14, cy - 5, 12, {180, 190, 210, 160});
  // Tick at north
  DrawLine(cx, cy - r + 4, cx, cy - r + 10, {255, 230, 100, 220});
}

// ================================================================
// Wireframe flight HUD — boresight, FPV, pitch ladder, heading tape,
// speed tape. Drawn in every camera view per the design.
// Style: amber lines (Virus / F-16 phosphor convention), no fills,
// translucent so the world reads through.
// ================================================================
void GameState::drawFlightHUD() const {
  const Color amber = {255, 176, 0, 220};
  const Color amberDim = {255, 176, 0, 140};
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();
  const float cx = sw * 0.5f;
  const float cy = sh * 0.5f;

  Vector3 ppos = m_player.position();
  Vector3 vel = m_player.velocity();
  float speed = m_player.speed();
  float yaw = m_player.yaw();
  float pitch = m_player.pitch();
  float roll = m_player.roll();

  const float deg = 180.0f / 3.14159265f;
  float yawDeg = yaw * deg;
  while (yawDeg < 0.0f) yawDeg += 360.0f;
  while (yawDeg >= 360.0f) yawDeg -= 360.0f;
  float pitchDeg = pitch * deg;

  // ----------------------------------------------------------------
  // Horizon bar (waterline) — fixed at screen centre, an airplane-rear
  // silhouette: two short wings flanking a centre fuselage square,
  // wing tips drop downward. The pitch ladder (and the world) moves
  // RELATIVE to this bar — it's the static "you are here" reference.
  // ----------------------------------------------------------------
  {
    const float wingLen = 22.0f;
    const float gap = 9.0f;
    const float dropLen = 5.0f;
    // Wings
    DrawLineEx({cx - gap - wingLen, cy}, {cx - gap, cy}, 2.0f, amber);
    DrawLineEx({cx + gap, cy}, {cx + gap + wingLen, cy}, 2.0f, amber);
    // Wing-tip drops (downward strokes at the outboard ends)
    DrawLineEx({cx - gap - wingLen, cy},
               {cx - gap - wingLen, cy + dropLen}, 2.0f, amber);
    DrawLineEx({cx + gap + wingLen, cy},
               {cx + gap + wingLen, cy + dropLen}, 2.0f, amber);
    // Centre fuselage marker
    DrawRectangle(static_cast<int>(cx) - 2, static_cast<int>(cy) - 2, 4, 4,
                  amber);
  }

  // ----------------------------------------------------------------
  // Nose / weapons crosshair — projects a point in front of the ship
  // along its actual forward vector (yaw + pitch + roll applied) onto
  // the screen. This is where future projectile weapons will fire.
  // Distinct from the FPV: this is a circle with a 4-arm SEALED cross
  // (not the FPV's 3-wing open pattern).
  // ----------------------------------------------------------------
  {
    Vector3 noseFwd = m_player.forward();
    Vector3 noseWorld = Vector3Add(ppos, Vector3Scale(noseFwd, 30.0f));
    Vector2 noseScreen = GetWorldToScreen(noseWorld, m_camera);

    // Cull when nose points away from camera (GetWorldToScreen returns
    // mirror-projected coords for points behind the camera plane).
    Vector3 camFwd = Vector3Normalize(
        Vector3Subtract(m_camera.target, m_camera.position));
    bool inFront = Vector3DotProduct(noseFwd, camFwd) > 0.0f;

    if (inFront && noseScreen.x > -50.0f && noseScreen.x < sw + 50.0f &&
        noseScreen.y > -50.0f && noseScreen.y < sh + 50.0f) {
      const float r = 5.0f;
      const float arm = 7.0f;
      DrawCircleLines(static_cast<int>(noseScreen.x),
                      static_cast<int>(noseScreen.y), r, amber);
      DrawLineEx({noseScreen.x - r - arm, noseScreen.y},
                 {noseScreen.x - r, noseScreen.y}, 1.5f, amber);
      DrawLineEx({noseScreen.x + r, noseScreen.y},
                 {noseScreen.x + r + arm, noseScreen.y}, 1.5f, amber);
      DrawLineEx({noseScreen.x, noseScreen.y - r - arm},
                 {noseScreen.x, noseScreen.y - r}, 1.5f, amber);
      DrawLineEx({noseScreen.x, noseScreen.y + r},
                 {noseScreen.x, noseScreen.y + r + arm}, 1.5f, amber);
    }
  }

  // ----------------------------------------------------------------
  // Pitch ladder + artificial horizon. Translates vertically with
  // pitch (positive pitch = nose up = horizon below screen centre)
  // and rotates about screen centre with roll. Rungs every 15° from
  // -45 to +45. The horizon (0°) is bold; negative-pitch rungs are
  // dashed, F-16 convention.
  // ----------------------------------------------------------------
  {
    constexpr float PPD = 6.0f;          // pixels per degree
    const float rungHalfLen = 60.0f;     // half-width of a rung
    const float rungGap = 14.0f;         // gap in middle of each rung
    const float ca = cosf(roll);
    const float sa = sinf(roll);
    auto worldFromLocal = [&](float lx, float ly) -> Vector2 {
      // 2D rotation about origin then translate to (cx, cy)
      return {lx * ca - ly * sa + cx, lx * sa + ly * ca + cy};
    };

    const int LADDER[] = {-45, -30, -15, 0, 15, 30, 45};
    for (int d : LADDER) {
      // Local Y for rung at world-pitch d, given player pitch:
      // rung at d=pitchDeg passes through screen centre (localY=0).
      // Rungs above the horizon (d > pitchDeg) draw above centre
      // (negative localY), rungs below draw below.
      float localY = (pitchDeg - static_cast<float>(d)) * PPD;
      if (localY < -static_cast<float>(sh) || localY > static_cast<float>(sh))
        continue;

      Vector2 lA = worldFromLocal(-rungHalfLen, localY);
      Vector2 lB = worldFromLocal(-rungGap, localY);
      Vector2 rA = worldFromLocal(rungGap, localY);
      Vector2 rB = worldFromLocal(rungHalfLen, localY);

      if (d == 0) {
        DrawLineEx(lA, lB, 2.0f, amber);
        DrawLineEx(rA, rB, 2.0f, amber);
      } else if (d > 0) {
        DrawLineEx(lA, lB, 1.4f, amber);
        DrawLineEx(rA, rB, 1.4f, amber);
      } else {
        // Dashed for negative pitch (below horizon)
        Vector2 lMid = worldFromLocal(-(rungHalfLen + rungGap) * 0.5f, localY);
        Vector2 rMid = worldFromLocal((rungHalfLen + rungGap) * 0.5f, localY);
        DrawLineEx(lA, lMid, 1.2f, amberDim);
        DrawLineEx(rMid, rB, 1.2f, amberDim);
      }

      // Tail tick — short downward stroke at the inner ends, suggesting
      // "this is the side of the line below the horizon" (F-16 detail).
      Vector2 lTickA = worldFromLocal(-rungGap, localY);
      Vector2 lTickB = worldFromLocal(-rungGap, localY + (d >= 0 ? 5.0f : -5.0f));
      Vector2 rTickA = worldFromLocal(rungGap, localY);
      Vector2 rTickB = worldFromLocal(rungGap, localY + (d >= 0 ? 5.0f : -5.0f));
      DrawLineEx(lTickA, lTickB, 1.0f, d == 0 ? amber : amberDim);
      DrawLineEx(rTickA, rTickB, 1.0f, d == 0 ? amber : amberDim);

      // Degree labels at both ends
      char buf[8];
      snprintf(buf, sizeof(buf), "%+d", d);
      Vector2 lblL = worldFromLocal(-rungHalfLen - 26.0f, localY - 6.0f);
      Vector2 lblR = worldFromLocal(rungHalfLen + 4.0f, localY - 6.0f);
      drawHudText(buf, static_cast<int>(lblL.x), static_cast<int>(lblL.y), 11,
               amber);
      drawHudText(buf, static_cast<int>(lblR.x), static_cast<int>(lblR.y), 11,
               amber);
    }
  }

  // ----------------------------------------------------------------
  // Flight Path Marker (FPV) / velocity vector indicator. Projects a
  // point in front of the ship along the velocity direction onto the
  // screen. When FPV diverges from boresight, the pilot is drifting.
  // Drawn as a small circle with three "wings": KSP/Elite/F-16 style.
  // Hidden when nearly stationary (direction is meaningless then).
  // ----------------------------------------------------------------
  if (speed > Config::VELOCITY_CAM_MIN_SPD) {
    Vector3 velDir = Vector3Scale(vel, 1.0f / speed);
    // Sample point along velocity direction. Use a moderate distance
    // so the marker stays in screen even at extreme speeds.
    Vector3 fpvWorld =
        Vector3Add(ppos, Vector3Scale(velDir, 30.0f));
    Vector2 fpvScreen = GetWorldToScreen(fpvWorld, m_camera);

    // Cull if projection is wildly off-screen (behind camera, etc.)
    if (fpvScreen.x > -100.0f && fpvScreen.x < sw + 100.0f &&
        fpvScreen.y > -100.0f && fpvScreen.y < sh + 100.0f) {
      // Determine if velocity points generally behind the camera; if so,
      // GetWorldToScreen still returns valid coords but they refer to
      // the screen as a flipped projection. Check via dot product
      // against the camera forward vector to detect retrograde.
      Vector3 camFwd =
          Vector3Normalize(Vector3Subtract(m_camera.target, m_camera.position));
      bool retrograde = Vector3DotProduct(velDir, camFwd) < 0.0f;

      const float r = 6.0f;
      const float wing = 6.0f;
      DrawCircleLines(static_cast<int>(fpvScreen.x),
                      static_cast<int>(fpvScreen.y), r, amber);
      // Three "wings" — left, right, top
      DrawLineEx({fpvScreen.x - r, fpvScreen.y},
                 {fpvScreen.x - r - wing, fpvScreen.y}, 1.5f, amber);
      DrawLineEx({fpvScreen.x + r, fpvScreen.y},
                 {fpvScreen.x + r + wing, fpvScreen.y}, 1.5f, amber);
      DrawLineEx({fpvScreen.x, fpvScreen.y - r},
                 {fpvScreen.x, fpvScreen.y - r - wing}, 1.5f, amber);

      // Retrograde: cross through the FPV (KSP/Elite convention)
      if (retrograde) {
        const float d = r * 0.7f;
        DrawLineEx({fpvScreen.x - d, fpvScreen.y - d},
                   {fpvScreen.x + d, fpvScreen.y + d}, 1.5f, amber);
        DrawLineEx({fpvScreen.x - d, fpvScreen.y + d},
                   {fpvScreen.x + d, fpvScreen.y - d}, 1.5f, amber);
      }
    }
  }

  // ----------------------------------------------------------------
  // Heading tape — top-centre. ~50° of arc shown. Three-digit format
  // (000 = north) plus N/E/S/W cardinal letters at the four points.
  // Caret at screen centre marks the current heading.
  // ----------------------------------------------------------------
  {
    const float tapeW = 320.0f;
    const float tapeH = 26.0f;
    const float tx = cx - tapeW * 0.5f;
    const float ty = 18.0f;
    const float visibleArc = 50.0f; // degrees displayed
    const float ppd = tapeW / visibleArc; // pixels per degree

    // Frame
    DrawRectangleLinesEx({tx, ty, tapeW, tapeH}, 1.0f, amberDim);

    // Tick marks every 5°. Major (with label) every 10°.
    int startDeg = static_cast<int>(yawDeg) - static_cast<int>(visibleArc * 0.5f);
    int endDeg = startDeg + static_cast<int>(visibleArc) + 1;
    for (int d = startDeg; d <= endDeg; ++d) {
      int dn = ((d % 360) + 360) % 360;
      if (d % 5 != 0) continue;
      float xpos = tx + (d - (yawDeg - visibleArc * 0.5f)) * ppd;
      if (xpos < tx || xpos > tx + tapeW) continue;
      bool major = (d % 10 == 0);
      float h = major ? 8.0f : 4.0f;
      DrawLineEx({xpos, ty + tapeH - h}, {xpos, ty + tapeH}, 1.0f, amber);
      if (major) {
        char buf[8];
        if (dn == 0)        snprintf(buf, sizeof(buf), "N");
        else if (dn == 90)  snprintf(buf, sizeof(buf), "E");
        else if (dn == 180) snprintf(buf, sizeof(buf), "S");
        else if (dn == 270) snprintf(buf, sizeof(buf), "W");
        else                snprintf(buf, sizeof(buf), "%03d", dn);
        int tw = measureHudText(buf, 11);
        drawHudText(buf, static_cast<int>(xpos) - tw / 2,
                 static_cast<int>(ty) + 2, 11, amber);
      }
    }

    // Centre caret pointing up at the active heading
    Vector2 cTip = {cx, ty + tapeH + 2.0f};
    Vector2 cL = {cx - 5.0f, ty + tapeH + 8.0f};
    Vector2 cR = {cx + 5.0f, ty + tapeH + 8.0f};
    DrawLineEx(cTip, cL, 1.5f, amber);
    DrawLineEx(cTip, cR, 1.5f, amber);
    DrawLineEx(cL, cR, 1.5f, amber);
  }

  // ----------------------------------------------------------------
  // Speed tape — left edge. Mirrors the AGL tape on the right of the
  // bottom-left HUD cluster. Shows current ground speed in m/s with
  // a moving tick scale and a centre caret.
  // ----------------------------------------------------------------
  {
    const float tapeH = 220.0f;
    const float tapeW = 18.0f;
    const float tx = 14.0f;
    const float ty = cy - tapeH * 0.5f;

    // Frame
    DrawRectangle(static_cast<int>(tx) - 1, static_cast<int>(ty) - 1,
                  static_cast<int>(tapeW) + 2, static_cast<int>(tapeH) + 2,
                  {0, 0, 0, 160});
    DrawRectangleLinesEx({tx, ty, tapeW, tapeH}, 1.0f, amberDim);

    // Scrolling scale: 5 m/s per minor tick, 10 per major.
    // 100 m/s spans roughly the tape height; scale = 2.2 px per m/s.
    const float pxPerUnit = tapeH / 100.0f; // 100 m/s window
    const float minSpd = speed - 50.0f;
    const float maxSpd = speed + 50.0f;
    int s0 = static_cast<int>(floorf(minSpd / 5.0f)) * 5;
    int s1 = static_cast<int>(ceilf(maxSpd / 5.0f)) * 5;
    for (int s = s0; s <= s1; s += 5) {
      if (s < 0) continue;
      float yPx = ty + tapeH * 0.5f - (s - speed) * pxPerUnit;
      if (yPx < ty || yPx > ty + tapeH) continue;
      bool major = (s % 10 == 0);
      float w = major ? 7.0f : 4.0f;
      DrawLineEx({tx + tapeW - w, yPx}, {tx + tapeW, yPx}, 1.0f,
                 major ? amber : amberDim);
      if (major && s > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s);
        drawHudText(buf, static_cast<int>(tx) - 22, static_cast<int>(yPx) - 5,
                 10, amberDim);
      }
    }

    // Centre caret + speed readout
    Vector2 carT = {tx + tapeW + 2.0f, ty + tapeH * 0.5f - 4.0f};
    Vector2 carM = {tx + tapeW + 8.0f, ty + tapeH * 0.5f};
    Vector2 carB = {tx + tapeW + 2.0f, ty + tapeH * 0.5f + 4.0f};
    DrawLineEx(carT, carM, 1.5f, amber);
    DrawLineEx(carM, carB, 1.5f, amber);
    DrawLineEx(carT, carB, 1.5f, amber);

    drawHudText("SPD", static_cast<int>(tx) - 4, static_cast<int>(ty) - 14, 11,
             amber);
    char sbuf[12];
    snprintf(sbuf, sizeof(sbuf), "%.0f", speed);
    drawHudText(sbuf, static_cast<int>(tx) - 4,
             static_cast<int>(ty + tapeH) + 4, 14, amber);
  }
}

// ================================================================
// shutdown
// ================================================================
// ================================================================
// Menu system — Main / Pause / Settings overlays.
//
// All three are drawn over the live (or frozen) world so the player
// sees the world they'll be flying. Mouse cursor is enabled while
// any menu is open. Settings is a panel overlaid on top of Main or
// Pause, dismissed by clicking BACK.
//
// UI primitives are hand-rolled — raylib has no widget kit. Hover
// state via CheckCollisionPointRec, click via the m_lastClickState
// edge-trigger so a single press doesn't repeat across frames.
// ================================================================


// ----------------------------------------------------------------
// Cursor management — gameplay locks the cursor for FPS-style mouse
// look (Player::handleInput uses GetMouseDelta). Menus need it free.
// Only call the raylib functions on transitions; calling them every
// frame causes a flicker on some Linux WMs.
// ----------------------------------------------------------------
void GameState::setCursorForGameplay(bool inGameplay) {
  if (inGameplay && !m_cursorHidden) {
    DisableCursor();
    m_cursorHidden = true;
  } else if (!inGameplay && m_cursorHidden) {
    EnableCursor();
    m_cursorHidden = false;
  }
}

void GameState::applyLiveSettings() {
  m_player.setInvertYaw(m_settings.invertYaw);
  m_player.setInvertPitch(m_settings.invertPitch);
  m_player.setGodMode(m_settings.godMode);
  // wireframeHUD is checked per-frame in drawHUD() — no apply needed.
  // defaultView is only consumed at game-start; live changes don't
  // override the player's current view.
}

void GameState::resetCombat() {
  m_em.clear();
  m_particles.clear();

  // Respawn player above world centre
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);
  Vector3 startPos = {mid, ground + 12.0f, mid};
  m_player.init(startPos, Config::FLIGHT_ASSIST_DEFAULT);

  // Re-anchor camera so it doesn't lerp through the old position
  Vector3 fwd = m_player.forward();
  m_camera.position = Vector3Add(
      startPos, Vector3Add(Vector3Scale(fwd, -Config::CAM_DISTANCE),
                           {0, Config::CAM_HEIGHT, 0}));
  m_camera.target = Vector3Add(startPos, {0, 1.5f, 0});

  // Reset the wave manager — wave 1 begins after WAVE_FIRST_DELAY.
  m_waves.reset();

  applyLiveSettings();
  m_cameraView = static_cast<CameraView>(m_settings.defaultView);
  resetCameraZoom();

  // Fresh life — re-arm the death pipeline.
  m_playerDeathHandled = false;
}

void GameState::enterMainMenu() {
  m_state = AppState::MainMenu;
  m_settingsOpen = false;
  setCursorForGameplay(false);
}

void GameState::enterPlaying() {
  resetCombat();
  m_state = AppState::Playing;
  m_settingsOpen = false;
  setCursorForGameplay(true);
}

void GameState::enterPaused() {
  m_state = AppState::Paused;
  setCursorForGameplay(false);
}

void GameState::resumePlaying() {
  m_state = AppState::Playing;
  m_settingsOpen = false;
  setCursorForGameplay(true);
}

// ----------------------------------------------------------------
// drawMainMenu / drawPauseMenu / drawSettingsPanel
//
// Each method also handles its own input (mouse hover + click). The
// click edge is computed once per render via m_lastClickState so a
// long-held mouse button only triggers one action per click.
// ----------------------------------------------------------------
void GameState::drawMainMenu() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  Vector2 mouse = GetMousePosition();
  bool clickNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  bool clickEdge = clickNow && !m_lastClickState;

  // Dimming overlay so the menu reads against any terrain background
  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 140});

  // Title
  const char *title = "TERRA-SIEGE";
  int tw = measureHudText(title, 64);
  drawHudText(title, sw / 2 - tw / 2, sh / 4 - 32, 64, {220, 240, 255, 255});

  const char *sub = "a modern reimagining of Virus (1988)";
  int sbw = measureHudText(sub, 16);
  drawHudText(sub, sw / 2 - sbw / 2, sh / 4 + 40, 16, {180, 200, 220, 200});

  if (m_settingsOpen) {
    drawSettingsPanel();
    m_lastClickState = clickNow;
    return;
  }

  // Buttons
  const float bw = 280.0f, bh = 50.0f;
  float bx = sw / 2 - bw / 2;
  float by = sh / 2 - 30;

  if (drawMenuButton({bx, by, bw, bh}, "START GAME", mouse, clickEdge))
    enterPlaying();
  by += bh + 14;
  if (drawMenuButton({bx, by, bw, bh}, "SETTINGS", mouse, clickEdge))
    m_settingsOpen = true;
  by += bh + 14;
  if (drawMenuButton({bx, by, bw, bh}, "QUIT", mouse, clickEdge)) {
    // raylib WindowShouldClose reads this on next frame
    CloseWindow();
  }

  m_lastClickState = clickNow;
}

void GameState::drawPauseMenu() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  Vector2 mouse = GetMousePosition();
  bool clickNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  bool clickEdge = clickNow && !m_lastClickState;

  DrawRectangle(0, 0, sw, sh, {0, 0, 0, 140});

  const char *title = "PAUSED";
  int tw = measureHudText(title, 48);
  drawHudText(title, sw / 2 - tw / 2, sh / 4, 48, {230, 235, 255, 255});

  if (m_settingsOpen) {
    drawSettingsPanel();
    m_lastClickState = clickNow;
    return;
  }

  const float bw = 280.0f, bh = 50.0f;
  float bx = sw / 2 - bw / 2;
  float by = sh / 2 - 30;

  if (drawMenuButton({bx, by, bw, bh}, "RESUME", mouse, clickEdge))
    resumePlaying();
  by += bh + 14;
  if (drawMenuButton({bx, by, bw, bh}, "SETTINGS", mouse, clickEdge))
    m_settingsOpen = true;
  by += bh + 14;
  if (drawMenuButton({bx, by, bw, bh}, "MAIN MENU", mouse, clickEdge))
    enterMainMenu();

  m_lastClickState = clickNow;
}

void GameState::drawSettingsPanel() {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  Vector2 mouse = GetMousePosition();
  bool clickNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  bool clickEdge = clickNow && !m_lastClickState;

  // Panel background
  const float pw = 480.0f, ph = 380.0f;
  float px = sw / 2 - pw / 2;
  float py = sh / 2 - ph / 2;
  DrawRectangle(static_cast<int>(px), static_cast<int>(py),
                static_cast<int>(pw), static_cast<int>(ph),
                {18, 24, 36, 240});
  DrawRectangleLinesEx({px, py, pw, ph}, 2.0f, {120, 150, 200, 220});

  const char *title = "SETTINGS";
  int tw = measureHudText(title, 28);
  drawHudText(title, static_cast<int>(px + pw / 2 - tw / 2),
           static_cast<int>(py + 20), 28, {220, 235, 255, 255});

  // Rows
  const float rowH = 44.0f;
  float ry = py + 70;
  Rectangle rowRect{px + 20, ry, pw - 40, rowH};

  // Invert Yaw
  if (drawSettingsToggleRow(rowRect, "Invert Yaw (Mouse-X)",
                            m_settings.invertYaw ? "ON" : "OFF", mouse,
                            clickEdge)) {
    m_settings.invertYaw = !m_settings.invertYaw;
    applyLiveSettings();
    m_settings.save(m_settingsPath);
  }
  rowRect.y += rowH + 6;

  // Invert Pitch
  if (drawSettingsToggleRow(rowRect, "Invert Pitch (Mouse-Y)",
                            m_settings.invertPitch ? "ON" : "OFF", mouse,
                            clickEdge)) {
    m_settings.invertPitch = !m_settings.invertPitch;
    applyLiveSettings();
    m_settings.save(m_settingsPath);
  }
  rowRect.y += rowH + 6;

  // God Mode
  if (drawSettingsToggleRow(rowRect, "God Mode (F3)",
                            m_settings.godMode ? "ON" : "OFF", mouse,
                            clickEdge)) {
    m_settings.godMode = !m_settings.godMode;
    applyLiveSettings();
    m_settings.save(m_settingsPath);
  }
  rowRect.y += rowH + 6;

  // Default View — clicking cycles 0→1→…→4→0
  if (drawSettingsToggleRow(rowRect, "Default Camera View",
                            cameraViewLabel(m_settings.defaultView), mouse,
                            clickEdge)) {
    m_settings.defaultView = (m_settings.defaultView + 1) % 5;
    m_settings.save(m_settingsPath);
  }
  rowRect.y += rowH + 6;

  // Wireframe HUD
  if (drawSettingsToggleRow(rowRect, "Wireframe Flight HUD",
                            m_settings.wireframeHUD ? "ON" : "OFF", mouse,
                            clickEdge)) {
    m_settings.wireframeHUD = !m_settings.wireframeHUD;
    m_settings.save(m_settingsPath);
  }
  rowRect.y += rowH + 6;

  // Back button
  Rectangle back{px + pw / 2 - 80, py + ph - 60, 160, 40};
  if (drawMenuButton(back, "BACK", mouse, clickEdge)) {
    m_settingsOpen = false;
  }

  m_lastClickState = clickNow;
}

void GameState::shutdown() {
#ifdef DEV_MODE
  closeLog();
  stopRecording();
#endif
  m_particles.unload();
  m_player.unload();
  m_planet.unload();
  if (m_hudFontLoaded) {
    UnloadFont(m_hudFont);
    m_hudFontLoaded = false;
  }
}