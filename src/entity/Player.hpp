#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"

class Planet;

// ====================================================================
// Player — Newtonian hovercraft (Virus/Zarch style).
//
// One physics path. Thrust along local UP, constant world gravity,
// near-zero drag. Pilot tilts the ship to redirect the thrust vector.
//
// See terra_rebuild/flight_mode_rebuild.md for the design spec.
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

  // ---- State accessors ----
  Vector3 position() const { return m_pos; }
  Vector3 velocity() const { return m_vel; }
  Vector3 forward() const; // unit world-space nose direction
  Vector3 up() const;      // unit world-space LOCAL UP (= thrust direction)
  Vector3 right() const;   // unit world-space lateral axis

  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }
  float roll() const { return m_roll; }
  float pitchVis() const { return -m_pitch; } // render-space pitch
  float speed() const;
  float thrustCharge() const { return m_thrustCharge; }
  float health() const { return m_health; }
  bool isAlive() const { return m_health > 0.0f; }
  bool isThrusting() const { return m_thrusting; }
  bool isLanded() const { return m_landed; }

  void setFlightAssist(int level);
  int flightAssist() const { return m_assistLevel; }

  // Dev toggle — when true, thrust charge never drains (HUD pinned at MAX).
  void setInfiniteCharge(bool on) { m_infiniteCharge = on; }
  bool infiniteCharge() const { return m_infiniteCharge; }

  void applyDamage(float amount);

private:
  // Pipeline
  void handleInput(float dt);
  void applyFlightAssist(float dt, const Planet &planet);
  void applyPhysics(float dt, const Planet &planet);
  void wrapPosition(float worldSize);

  // Mesh construction (single hovercraft mesh)
  void buildMesh();

  // Ship state
  Vector3 m_pos = {};
  Vector3 m_vel = {};
  float m_yaw = 0.0f;   // heading (rad), 0 = +Z (north)
  float m_pitch = 0.0f; // pitch (rad), positive = nose UP visually
  float m_roll = 0.0f;  // bank angle (rad), positive = right wing up

  Vector2 m_smoothMouse = {0.0f, 0.0f}; // lowpass-filtered mouse delta

  float m_thrustCharge = Config::NEWTON_THRUST_CHARGE_MAX;
  float m_health = 100.0f;
  bool m_thrusting = false;
  bool m_landed = false;
  bool m_infiniteCharge = false;
  int m_assistLevel = Config::FLIGHT_ASSIST_DEFAULT;

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};
