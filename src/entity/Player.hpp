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
  // Drop-shadow on the terrain directly below the ship. Fades with
  // altitude. Called from GameState between planet and player render
  // so the shadow sits on the terrain below the ship.
  void renderGroundShadow(const Planet &planet) const;
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

  // Dev god-mode toggle — when true: infinite thrust (charge pinned at
  // MAX), invincibility (applyDamage no-op), and any future ammo /
  // energy drain is bypassed. F3 in DEV_MODE.
  void setGodMode(bool on) { m_godMode = on; }
  bool godMode() const { return m_godMode; }

  // Input inversion — settable from the future settings menu. Yaw
  // defaults inverted because the user reports left/right reads
  // backwards with the current convention.
  void setInvertYaw(bool on) { m_invertYaw = on; }
  bool invertYaw() const { return m_invertYaw; }
  void setInvertPitch(bool on) { m_invertPitch = on; }
  bool invertPitch() const { return m_invertPitch; }

  void applyDamage(float amount);

  // Cannon firing — Player tracks the cooldown internally. Each tick
  // GameState calls consumePendingShot(); if it returns true, a
  // projectile should be spawned at outPos with outVel.
  bool consumePendingShot(Vector3 &outPos, Vector3 &outVel);

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
  bool m_godMode = false;
  bool m_invertYaw = true;   // user reports left/right reads backwards
  bool m_invertPitch = false;
  int m_assistLevel = Config::FLIGHT_ASSIST_DEFAULT;

  // Cannon firing state — handleInput sets m_fireRequested while LMB/
  // SPACE is held; update ticks m_cannonTimer down and arms a pending
  // shot when the timer hits zero. GameState consumes the shot,
  // spawns the projectile via EntityManager, and the timer is reset.
  bool m_fireRequested = false;
  bool m_pendingShot = false;
  float m_cannonTimer = 0.0f;
  Vector3 m_shotPos = {};
  Vector3 m_shotVel = {};

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};
