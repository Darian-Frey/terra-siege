#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"

class Planet;

// ====================================================================
// FlightMode — physics model selection.
//   Classic  = Newtonian Virus-style (thrust along local UP, gravity,
//              near-zero drag, full manual attitude).
//   Arcade   = Air Combat 22 style (bank-to-turn, velocity locked to
//              forward, auto-level, smoothing).
// ====================================================================
enum class FlightMode {
  Classic,
  Arcade,
  _Count
};
const char *flightModeName(FlightMode m);

// ====================================================================
// CraftType — selectable ship mesh. More can be added later.
// ====================================================================
enum class CraftType {
  DeltaWing,    // Swept-back delta fighter (arcade)
  ForwardSwept, // X-29/Su-47 style forward-swept wings + canards
  X36,          // NASA X-36 tailless lambda-wing demonstrator
  YB49,         // Northrop YB-49 flying wing bomber
  Saucer,       // Simple flying saucer — Classic/Newtonian mode
  _Count        // sentinel for cycling
};

const char *craftName(CraftType c);

// ====================================================================
// Player — hovercraft physics, input, flight assist, mesh render
// ====================================================================

class Player {
public:
  Player() = default;
  ~Player() = default;

  void init(Vector3 startPos,
            int flightAssistLevel = Config::FLIGHT_ASSIST_DEFAULT,
            CraftType craft = CraftType::DeltaWing);

  // Swap to a different craft at runtime — unloads old mesh and
  // builds the new one in place. State (position, velocity, etc.) is
  // preserved.
  void setCraft(CraftType craft);
  CraftType craft() const { return m_craftType; }

  // Swap physics model at runtime. Auto-assigns the Saucer craft when
  // switching to Classic (the fighter meshes don't match Newtonian
  // handling). Caller can still override with setCraft() afterwards.
  void setFlightMode(FlightMode mode);
  FlightMode flightMode() const { return m_flightMode; }
  void update(float dt, const Planet &planet);
  void render() const;
  void unload();

  // State accessors (used by camera, HUD, radar, weapons)
  Vector3 position() const { return m_pos; }
  Vector3 velocity() const { return m_vel; }
  Vector3 forward() const; // unit forward vector in world space (yaw + pitch)
  Vector3 right() const;   // unit right vector in world space
  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }
  float roll() const { return m_roll; }
  float pitchVis() const { return m_pitchVis; }
  float speed() const { return m_currentSpeed; }
  float targetSpeed() const { return m_targetSpeed; }
  bool boosting() const { return m_boosting; }
  float health() const { return m_health; }
  bool isAlive() const { return m_health > 0.0f; }
  bool thrusting() const { return m_thrusting; }

  void setFlightAssist(int level);
  int flightAssist() const { return m_assistLevel; }

  void applyDamage(float amount);

private:
  // Pipeline
  void handleInput(float dt);
  void handleArcadeInput(float dt);
  void handleClassicInput(float dt);
  void applyPhysics(float dt, const Planet &planet);
  void applyArcadePhysics(float dt, const Planet &planet);
  void applyClassicPhysics(float dt, const Planet &planet);
  void applyFlightAssist(float dt);

  // Mesh construction
  void buildMesh();

  // Ship state
  Vector3 m_pos = {};
  Vector3 m_vel = {};
  float m_yaw = 0.0f;       // heading (rad), 0 = +Z (north)
  float m_pitch = 0.0f;     // flight pitch (rad), positive = nose up
  float m_roll = 0.0f;      // bank angle (rad), positive = right wing up
  float m_pitchVis = 0.0f;  // render-space pitch (= -m_pitch for raylib row vec)
  float m_turnInput = 0.0f; // -1..1 (kept for HUD/legacy queries)
  float m_rollRate = 0.0f;  // smoothed bank rate (rad/s)
  float m_pitchRate = 0.0f; // smoothed pitch rate (rad/s)
  Vector2 m_smoothMouse = {0.0f, 0.0f}; // lowpass-filtered mouse delta
  float m_currentSpeed = 0.0f;
  float m_targetSpeed = 0.0f;
  bool m_boosting = false;
  float m_health = 100.0f;
  bool m_thrusting = false;
  int m_assistLevel = 2;
  CraftType m_craftType = CraftType::DeltaWing;
  FlightMode m_flightMode = FlightMode::Classic;

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};