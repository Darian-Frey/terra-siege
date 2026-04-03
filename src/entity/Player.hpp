#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"

class Planet;

// ====================================================================
// Player — hovercraft physics, input, flight assist, mesh render
// ====================================================================

class Player {
public:
  Player() = default;
  ~Player() = default;

  void init(Vector3 startPos,
            int flightAssistLevel = Config::FLIGHT_ASSIST_DEFAULT);
  void update(float dt, const Planet &planet);
  void render() const;
  void unload();

  // State accessors (used by camera, HUD, radar, weapons)
  Vector3 position() const { return m_pos; }
  Vector3 velocity() const { return m_vel; }
  Vector3 forward() const; // unit forward vector in world space
  Vector3 right() const;   // unit right vector in world space
  float yaw() const { return m_yaw; }
  float roll() const { return m_roll; }
  float pitchVis() const { return m_pitchVis; }
  float speed() const;
  float health() const { return m_health; }
  bool isAlive() const { return m_health > 0.0f; }
  bool thrusting() const { return m_thrusting; }

  void setFlightAssist(int level);
  int flightAssist() const { return m_assistLevel; }

  void applyDamage(float amount);

private:
  // Pipeline
  void handleInput(float dt);
  void applyPhysics(float dt, const Planet &planet);
  void applyFlightAssist(float dt);

  // Mesh construction
  void buildMesh();

  // Ship state
  Vector3 m_pos = {};
  Vector3 m_vel = {};
  float m_yaw = 0.0f;       // heading (rad), 0 = +Z (north)
  float m_roll = 0.0f;      // visual banking (rad)
  float m_pitchVis = 0.0f;  // visual pitch (rad, cosmetic only)
  float m_turnInput = 0.0f; // -1..1 (used by flight assist)
  float m_health = 100.0f;
  bool m_thrusting = false;
  int m_assistLevel = 2;

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};