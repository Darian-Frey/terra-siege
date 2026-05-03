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
  m_state = AppState::Playing;

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
  auto progressCb = [&lastDraw](const char *step, float progress) {
    double now = GetTime();
    if (progress < 1.0f && (now - lastDraw) < 0.033) return;
    lastDraw = now;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    BeginDrawing();
    ClearBackground({18, 28, 48, 255});

    const char *title = "TERRA-SIEGE";
    int tw = MeasureText(title, 48);
    DrawText(title, sw / 2 - tw / 2, sh / 2 - 110, 48, {230, 235, 245, 255});

    const char *sub = "a modern reimagining of Virus (1988)";
    int sbw = MeasureText(sub, 16);
    DrawText(sub, sw / 2 - sbw / 2, sh / 2 - 60, 16, {150, 165, 185, 220});

    int stepW = MeasureText(step, 20);
    DrawText(step, sw / 2 - stepW / 2, sh / 2 + 5, 20, {200, 215, 235, 255});

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
    int pw = MeasureText(pct, 18);
    DrawText(pct, sw / 2 - pw / 2, barY + 30, 18, {180, 200, 225, 255});

    EndDrawing();
  };

  m_planet.generate(seed, progressCb);

  // Spawn above world centre facing +Z (north).
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);
  Vector3 startPos = {mid, ground + 12.0f, mid};
  m_player.init(startPos, Config::FLIGHT_ASSIST_DEFAULT);
}

// ================================================================
// Dev keys (only compiled/active in DEV_MODE)
// ================================================================
void GameState::handleDevKeys() {
#ifdef DEV_MODE
  // F1 — toggle camera mode
  if (IsKeyPressed(KEY_F1)) {
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
  if (IsKeyPressed(KEY_F2)) {
    int next = (m_player.flightAssist() + 1) % 4;
    m_player.setFlightAssist(next);
  }

  // F3 — toggle infinite thrust charge (testing aid; full god mode in Phase 3+)
  if (IsKeyPressed(KEY_F3)) {
    bool on = !m_player.infiniteCharge();
    m_player.setInfiniteCharge(on);
    TraceLog(LOG_INFO, "Infinite thrust: %s", on ? "ON" : "OFF");
  }

  // F4 — toggle flight recorder
  if (IsKeyPressed(KEY_F4)) {
    if (isRecording())
      stopRecording();
    else
      startRecording();
  }

  // F6 — dump current heightmap to disk for offline inspection.
  // Writes a greyscale PNG and a text dump (stats + ASCII preview) to
  // tests/logs/heightmap-<unixtime>.{png,txt}. The ASCII preview is
  // human-readable so it can be inspected directly without an image
  // viewer.
  if (IsKeyPressed(KEY_F6)) {
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
  if (IsKeyPressed(KEY_F5)) {
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
// Follow camera
// ================================================================
void GameState::updateFollowCamera(float dt) {
  // Single fixed-distance chase camera. Speed-dependent zoom is gone with
  // the Arcade model. The five-view system in camera_system.md replaces
  // this with key-1..5 view selection in Step 3 of the rebuild.
  Vector3 playerPos = m_player.position();
  float playerYaw = m_player.yaw();
  Vector3 horizFwd = {sinf(playerYaw), 0.0f, cosf(playerYaw)};

  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, -Config::CAM_DISTANCE),
                            {0.0f, Config::CAM_HEIGHT, 0.0f}));

  // Terrain clamping — keep camera above the ground (do not remove this)
  float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
  float camMinY = camGroundH + 3.5f;
  if (desiredPos.y < camMinY) desiredPos.y = camMinY;
  if (desiredPos.y < playerPos.y + 2.0f)
    desiredPos.y = playerPos.y + 2.0f;

  // Smooth follow lerp — frame-rate independent
  float lerpSpeed = Config::CAM_LERP * dt;
  if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  // Look ahead of the ship in horizontal plane — horizon stays level.
  m_camera.target = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, 8.0f), {0.0f, 1.5f, 0.0f}));
  m_camera.up = {0.0f, 1.0f, 0.0f};
  m_camera.fovy = Config::CAM_FOV;
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

    if (m_camMode == CamMode::Follow) {
      m_player.update(dt, m_planet);
      updateFollowCamera(dt);
    } else {
      // Free-roam: player suspended
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
    m_player.render();
    EndMode3D();

    drawHUD();
    break;
  }
  case AppState::MainMenu:
    ClearBackground(BLACK);
    DrawText("MAIN MENU (todo)", 40, 40, 28, RAYWHITE);
    break;
  case AppState::Paused:
    DrawText("PAUSED", 40, 40, 28, RAYWHITE);
    break;
  case AppState::GameOver:
    ClearBackground(BLACK);
    DrawText("GAME OVER", 40, 40, 28, RED);
    break;
  case AppState::Victory:
    ClearBackground(BLACK);
    DrawText("VICTORY", 40, 40, 28, GREEN);
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
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Z: %7.1f", pos.z);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Alt:  %6.1f m", pos.y);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "AGL:  %6.1f m", agl);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Spd:  %5.1f%s", spd, thrusting ? " ^" : "");
  DrawText(buf, px, py, 14, thrusting ? Color{255, 200, 80, 240} : col);
  py += lh;
  snprintf(buf, sizeof(buf), "Hdg:  %5.1f  %s", yawDeg, compass);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Pch:  %+6.1f°", pitchDeg);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Roll: %+6.1f°", rollDeg);
  DrawText(buf, px, py, 14, col);
  py += lh;
  snprintf(buf, sizeof(buf), "Chg:  %5.1f", chargePct);
  DrawText(buf, px, py, 14, chargePct < 20.0f ? Color{255, 100, 80, 240} : col);
  py += lh;
  snprintf(buf, sizeof(buf), "Asst: %s", assistStr);
  DrawText(buf, px, py, 14, col);
  py += lh;

#ifdef DEV_MODE
  if (m_camMode == CamMode::FreeRoam) {
    DrawText("[ FREE CAM ]", px, py, 14, YELLOW);
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
    DrawText("HULL", bx, by - 14, 12, col);
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
    DrawText("THRUST", bx, by - 14, 12, col);
    if (m_player.infiniteCharge())
      DrawText("INF", bx + bw + 8, by, 14, {255, 200, 80, 240});
  }
  // Landed indicator
  if (m_player.isLanded()) {
    DrawText("LANDED", bx + bw + 12, sh - 80, 14, {120, 220, 140, 255});
  }

  // ---- Controls bar ----
  DrawRectangle(0, sh - 28, sw, 28, {0, 0, 0, 140});

  const char *controls =
      m_camMode == CamMode::Follow
          ? "Mouse: pitch+yaw  |  W/LMB: thrust  |  A/D: yaw  |  Q/E: roll"
          : "WASD: fly  |  Q/E: up/down  |  Shift: fast  (F1: back to ship)";
  DrawText(controls, 12, sh - 20, 14, {180, 180, 180, 220});

#ifdef DEV_MODE
  // Render dev key labels right-aligned with a fixed gap between tokens so
  // it stays legible no matter what each label's character count is.
  {
    const Color devCol = YELLOW;
    const char *tokens[] = {"[DEV]",     "F1:cam",   "F2:assist",
                            "F3:infthrust", "F4:rec", "F5:reseed",
                            "F6:dump"};
    const int tokenCount = sizeof(tokens) / sizeof(tokens[0]);
    const int gap = 18;
    const int rightMargin = 12;
    int totalW = 0;
    for (int i = 0; i < tokenCount; ++i) {
      totalW += MeasureText(tokens[i], 14);
      if (i + 1 < tokenCount) totalW += gap;
    }
    int x = sw - rightMargin - totalW;
    for (int i = 0; i < tokenCount; ++i) {
      DrawText(tokens[i], x, sh - 20, 14, devCol);
      x += MeasureText(tokens[i], 14) + gap;
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
    DrawText("REC", bx + 42, by + 8, 22, fg);

    // Elapsed time
    float elapsed = static_cast<float>(GetTime()) - g_recordStartTime;
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%.1fs", elapsed);
    DrawText(tbuf, bx + 100, by + 10, 18, {255, 220, 220, 255});
  }
#endif
}

// ================================================================
// shutdown
// ================================================================
void GameState::shutdown() {
#ifdef DEV_MODE
  closeLog();
  stopRecording();
#endif
  m_player.unload();
  m_planet.unload();
}