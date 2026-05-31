#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cstdint>

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
  // Directional-shield impact bubble. Drawn LAST in the 3D pass so
  // its translucent alpha blends correctly over the ship + world.
  // Invisible unless the shield was just engaged (within
  // PLAYER_SHIELD_FLASH seconds of the last absorbing hit).
  void renderShieldBubble() const;
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

  // Directional shield accessors — sector index uses the ShieldSector
  // enum (Front=0, Rear=1, Right=2, Left=3). HUD reads these to draw
  // the 4-segment shield pie. sectorMax is the spawn-time per-sector HP.
  float sectorHP(int sector) const;
  float sectorMax(int sector) const;
  float sectorHPFrac(int sector) const; // hp/max, clamped [0,1]

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

  // Scalar damage — goes straight to hull, bypasses all shielding.
  // Used for self-inflicted damage where direction makes no sense
  // (crashes, hard landings).
  void applyDamage(float amount);

  // Directional damage — overflows from the impacted shield sector
  // into hull. hitPos is world-space; the impact direction relative
  // to the ship's yaw picks the sector (front/rear/left/right).
  // All enemy projectile + drone contact hits go through this path.
  void applyDamage(float amount, Vector3 hitPos);

  // Primary fire slot — Cannon (default) or Plasma. Toggled with Q.
  // Plasma trades fire rate for splash damage; same fire button (LMB).
  enum class PrimaryWeapon : uint8_t { Cannon = 0, Plasma = 1 };
  PrimaryWeapon primaryWeapon() const { return m_primaryWeapon; }
  int missileAmmo() const { return m_missileAmmo; }
  float empCooldown() const { return m_empCooldown; }
  float empMaxCooldown() const; // for HUD ring readout

  // Pending-shot accessors — GameState calls each tick. Returns true
  // when a shot should be spawned, fills the out parameters, and
  // clears the request flag. Each weapon has its own queue slot so
  // multiple weapons can fire on the same tick (rare, but harmless).
  bool consumePendingShot(Vector3 &outPos, Vector3 &outVel); // primary
  bool consumePendingMissile(Vector3 &outPos, Vector3 &outVel);
  bool consumePendingEMP(Vector3 &outPos); // pos only — instant area effect

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

  // 4-sector directional shield (Phase 4). Mirror of the Entity-side
  // structure used by Carrier so the damage routing code path is
  // shared in spirit. Per-sector timers so opposite faces recharge
  // independently when one is being pressured. Indexed by the
  // ShieldSector enum (Front=0, Rear=1, Right=2, Left=3).
  float m_sectorHP[4] = {0, 0, 0, 0};
  float m_sectorMax[4] = {0, 0, 0, 0};
  float m_sectorTimer[4] = {0, 0, 0, 0};
  // Bubble-flash timer — single global value rather than per-sector
  // because the visual is just "shield engaged" feedback (directional
  // detail lives on the HUD pie if we ever bring it back). Set in
  // applyDamage when the shield absorbs anything; decays in update.
  float m_shieldFlashTimer = 0.0f;
  bool m_thrusting = false;
  bool m_landed = false;
  bool m_godMode = false;
  bool m_invertYaw = true;   // user reports left/right reads backwards
  bool m_invertPitch = false;
  int m_assistLevel = Config::FLIGHT_ASSIST_DEFAULT;

  // Primary fire (Cannon or Plasma — toggled with Q). handleInput
  // sets m_fireRequested while LMB/SPACE is held; update ticks
  // m_cannonTimer down and arms a pending shot when the timer hits
  // zero. The fire-rate constant is chosen per primary weapon.
  bool m_fireRequested = false;
  bool m_pendingShot = false;
  float m_cannonTimer = 0.0f;
  Vector3 m_shotPos = {};
  Vector3 m_shotVel = {};
  PrimaryWeapon m_primaryWeapon = PrimaryWeapon::Cannon;

  // Secondary — Missile (homing, proportional nav). RMB fires when
  // ammo > 0 and cooldown ready. Lock-on target id is set at fire
  // time inside GameState (it has access to EntityManager); Player
  // only signals the fire intent and tracks the cooldown + ammo.
  bool m_missileFireRequested = false;
  bool m_pendingMissile = false;
  float m_missileTimer = 0.0f;
  int m_missileAmmo = Config::MISSILE_AMMO_MAX;
  Vector3 m_missilePos = {};
  Vector3 m_missileVel = {};

  // Special — EMP (area stun). F key fires; long cooldown. Pending
  // flag tells GameState to scan + stun all enemies in radius.
  bool m_empFireRequested = false;
  bool m_pendingEMP = false;
  float m_empCooldown = 0.0f; // counts DOWN; 0 = ready
  Vector3 m_empPos = {};

  // Edge-trigger state for Tab (primary toggle) and F (EMP). Held at
  // 120Hz physics over 60Hz render means IsKeyPressed is unreliable
  // — track the last frame's state explicitly.
  bool m_tabWasDown = false;
  bool m_fWasDown = false;

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};
