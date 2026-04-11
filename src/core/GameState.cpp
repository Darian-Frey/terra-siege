#include "GameState.hpp"
#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
        << "t,ship_x,ship_y,ship_z,vel_x,vel_y,vel_z,speed,target_speed,"
           "yaw_rad,pitch_rad,roll_rad,yaw_deg,pitch_deg,roll_deg,"
           "fwd_x,fwd_y,fwd_z,agl,boost,"
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
               << player.speed() << "," << player.targetSpeed() << ","
               << player.yaw() << "," << player.pitch() << ","
               << player.roll() << ","
               << player.yaw() * rad2deg << ","
               << player.pitch() * rad2deg << ","
               << player.roll() * rad2deg << ","
               << f.x << "," << f.y << "," << f.z << ","
               << agl << ","
               << (player.boosting() ? 1 : 0) << ","
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
  m_planet.generate(12345u);

  // Start player above terrain centre facing north (+Z)
  float mid = m_planet.worldSize() * 0.5f;
  float ground = m_planet.heightAt(mid, mid);
  Vector3 startPos = {mid, ground + 12.0f, mid};

  m_player.init(startPos, Config::FLIGHT_ASSIST_DEFAULT);

  // Camera setup
  m_camera.fovy = Config::CAM_FOV;
  m_camera.projection = CAMERA_PERSPECTIVE;
  m_camera.up = {0.0f, 1.0f, 0.0f};

  // Initialise follow camera at sensible position
  Vector3 fwd = m_player.forward();
  m_camera.position =
      Vector3Add(startPos, Vector3Add(Vector3Scale(fwd, -Config::CAM_DISTANCE),
                                      {0, Config::CAM_HEIGHT, 0}));
  m_camera.target = Vector3Add(startPos, {0, 1.5f, 0});

  // Start in follow mode
  m_camMode = CamMode::Follow;

  m_state = AppState::Playing;

#ifdef DEV_MODE
  openLog();
#endif
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

  // F4 — toggle flight recorder
  if (IsKeyPressed(KEY_F4)) {
    if (isRecording())
      stopRecording();
    else
      startRecording();
  }

  // F5 — cycle craft type
  if (IsKeyPressed(KEY_F5)) {
    int next = (static_cast<int>(m_player.craft()) + 1) %
               static_cast<int>(CraftType::_Count);
    m_player.setCraft(static_cast<CraftType>(next));
  }
#endif
}

// ================================================================
// Follow camera
// ================================================================
void GameState::updateFollowCamera(float dt) {
  Vector3 playerPos = m_player.position();

  // Camera uses HORIZONTAL forward only (yaw axis). The ship's pitch
  // does not tilt the camera — instead the ship visually tilts within
  // the view while the camera stays locked to the horizon. This is the
  // classic Ace Combat / Air Combat 22 chase cam behavior.
  float playerYaw = m_player.yaw();
  Vector3 horizFwd = {sinf(playerYaw), 0.0f, cosf(playerYaw)};

  // Speed-dependent distance, height, and FOV. As the player accelerates
  // past cruise speed, the camera pulls back, lifts slightly, and FOV
  // widens — three classic tricks that make speed feel visceral.
  float speedT = (m_player.speed() - Config::ARCADE_CRUISE_SPEED) /
                 (Config::ARCADE_BOOST_SPEED - Config::ARCADE_CRUISE_SPEED);
  if (speedT < 0.0f) speedT = 0.0f;
  if (speedT > 1.0f) speedT = 1.0f;

  float camDistance = Config::ARCADE_CAM_DISTANCE_MIN +
                      speedT * (Config::ARCADE_CAM_DISTANCE_MAX -
                                Config::ARCADE_CAM_DISTANCE_MIN);
  float camHeight = Config::ARCADE_CAM_HEIGHT_MIN +
                    speedT * (Config::ARCADE_CAM_HEIGHT_MAX -
                              Config::ARCADE_CAM_HEIGHT_MIN);
  float camFov = Config::ARCADE_CAM_FOV_MIN +
                 speedT * (Config::ARCADE_CAM_FOV_MAX -
                           Config::ARCADE_CAM_FOV_MIN);

  // Lerp FOV smoothly so it doesn't snap when the speed changes abruptly
  m_camera.fovy += (camFov - m_camera.fovy) * 4.0f * dt;

  // Desired camera position: behind (in horizontal plane) and above
  // the player. Pitch does NOT affect camera position.
  Vector3 desiredPos = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, -camDistance),
                            {0.0f, camHeight, 0.0f}));

  // Terrain clamping — keep camera above the ground
  {
    float camGroundH = m_planet.heightAt(desiredPos.x, desiredPos.z);
    float camMinY = camGroundH + 3.5f;
    if (desiredPos.y < camMinY)
      desiredPos.y = camMinY;
  }
  if (desiredPos.y < playerPos.y + 2.0f)
    desiredPos.y = playerPos.y + 2.0f;

  // Fast lerp — visible turn motion without jarring snap
  float lerpSpeed = 30.0f * dt;
  if (lerpSpeed > 1.0f)
    lerpSpeed = 1.0f;
  m_camera.position = Vector3Lerp(m_camera.position, desiredPos, lerpSpeed);

  // Look target — ahead of the ship in the HORIZONTAL plane only.
  // The ship visually pitches within the view; the horizon stays level.
  m_camera.target = Vector3Add(
      playerPos, Vector3Add(Vector3Scale(horizFwd, 8.0f), {0.0f, 1.5f, 0.0f}));

  // Camera always level
  m_camera.up = {0.0f, 1.0f, 0.0f};
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
  float tgtSpd = m_player.targetSpeed();
  bool boost = m_player.boosting();

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
  snprintf(buf, sizeof(buf), "Spd:  %5.1f / %.0f%s", spd, tgtSpd,
           boost ? " B" : "");
  DrawText(buf, px, py, 14, boost ? Color{255, 200, 80, 240} : col);
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
  snprintf(buf, sizeof(buf), "Asst: %s", assistStr);
  DrawText(buf, px, py, 14, col);
  py += lh;

#ifdef DEV_MODE
  if (m_camMode == CamMode::FreeRoam) {
    DrawText("[ FREE CAM ]", px, py, 14, YELLOW);
  }
#endif

  // ---- Health bar ----
  const int bx = 12, by = sh - 60, bw = 160, bh = 14;
  DrawRectangle(bx - 1, by - 1, bw + 2, bh + 2, {0, 0, 0, 160});
  DrawRectangle(bx, by, bw, bh, {50, 50, 50, 255});
  int healthW = static_cast<int>(bw * m_player.health() / 100.0f);
  Color hpCol = healthW > bw / 2   ? Color{40, 200, 60, 255}
                : healthW > bw / 4 ? Color{220, 180, 0, 255}
                                   : Color{210, 40, 40, 255};
  DrawRectangle(bx, by, healthW, bh, hpCol);
  DrawText("HULL", bx, by - 16, 12, col);

  // ---- Controls bar ----
  DrawRectangle(0, sh - 28, sw, 28, {0, 0, 0, 140});

  const char *controls =
      m_camMode == CamMode::Follow
          ? "W/S: throttle  |  A/D: bank  |  Mouse: pitch+bank  |  Shift: boost"
          : "WASD: fly  |  Q/E: up/down  |  Shift: fast  (F1: back to ship)";
  DrawText(controls, 12, sh - 20, 14, {180, 180, 180, 220});

  // Craft name (above the controls bar)
  {
    char cbuf[48];
    snprintf(cbuf, sizeof(cbuf), "CRAFT: %s", craftName(m_player.craft()));
    DrawText(cbuf, 12, sh - 46, 14, {200, 220, 255, 220});
  }

#ifdef DEV_MODE
  DrawText("[DEV]  F1:cam  F2:assist  F4:rec  F5:craft",
           sw - 340, sh - 20, 14, YELLOW);

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