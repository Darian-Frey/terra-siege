#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include "raymath.h"
#include <cstdint>

namespace tsmesh {
class MeshRegistry;
}

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
  // Renders the hovercraft. If `registry` has a player mesh loaded
  // (assets/meshes/hovercraft.obj), it's drawn with the full
  // roll/pitch/yaw matrix. Otherwise falls back to the procedural
  // mesh built in buildMesh() during init().
  void render(const tsmesh::MeshRegistry *registry = nullptr) const;
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

  // Hull regeneration — called by RepairStation when the player is
  // within its heal radius. Caps at PLAYER_HULL_HP. Does NOT refill
  // shields (that's ShieldBooster's job).
  void heal(float amount);

  // Weapon slot enums — three slots (Primary / Secondary / Special)
  // plus an independent Auto Turret toggle. Cycled with Tab / Z / X.
  enum class PrimaryWeapon : uint8_t { Cannon = 0, Plasma = 1, Beam = 2 };
  enum class SecondaryWeapon : uint8_t {
    Missile = 0,
    Cluster = 1,
    DepthCharge = 2,
  };
  enum class SpecialWeapon : uint8_t { EMP = 0, ShieldBooster = 1 };
  PrimaryWeapon primaryWeapon() const { return m_primaryWeapon; }
  SecondaryWeapon secondaryWeapon() const { return m_secondaryWeapon; }
  SpecialWeapon specialWeapon() const { return m_specialWeapon; }
  bool autoTurretEnabled() const { return m_autoTurretEnabled; }

  // Loadout setters — called by the pre-flight LoadoutSelect screen
  // (5f) right after init() and before the player can take input, so
  // the chosen primary/secondary/special is the starting weapon for
  // the round. Tab/Z/X/T cycling still works mid-flight on top.
  void setPrimaryWeapon(PrimaryWeapon w) { m_primaryWeapon = w; }
  void setSecondaryWeapon(SecondaryWeapon w) { m_secondaryWeapon = w; }
  void setSpecialWeapon(SpecialWeapon w) { m_specialWeapon = w; }
  void setAutoTurretEnabled(bool on) { m_autoTurretEnabled = on; }

  // Ammo / energy / cooldown accessors for HUD.
  int missileAmmo() const { return m_missileAmmo; }
  int clusterAmmo() const { return m_clusterAmmo; }
  int depthChargeAmmo() const { return m_depthChargeAmmo; }
  float beamEnergy() const { return m_beamEnergy; }
  float beamEnergyMax() const; // Config::BEAM_ENERGY_MAX
  bool beamFiring() const { return m_beamFiring; }
  float empCooldown() const { return m_empCooldown; }
  float empMaxCooldown() const; // for HUD ring readout
  float shieldBoosterCooldown() const { return m_shieldBoosterCooldown; }
  float shieldBoosterMaxCooldown() const;

  // Pending-shot accessors — GameState calls each tick. Returns true
  // when a shot should be spawned, fills the out parameters, and
  // clears the request flag. Each weapon has its own queue slot so
  // multiple weapons can fire on the same tick (rare, but harmless).
  bool consumePendingShot(Vector3 &outPos, Vector3 &outVel); // primary
  bool consumePendingMissile(Vector3 &outPos, Vector3 &outVel);
  bool consumePendingDepthCharge(Vector3 &outPos, Vector3 &outVel);
  // Cluster — single carrier missile spawn. The carrier splits into
  // 4 sub-missiles when it nears its lock target; split logic lives
  // in EntityManager::updateProjectile (ClusterParent kind).
  bool consumePendingCluster(Vector3 &outPos, Vector3 &outVel);
  bool consumePendingEMP(Vector3 &outPos); // pos only — instant area effect
  bool consumePendingShieldBoost();        // signal-only; effect applied internally
  // Beam firing this tick? GameState reads each frame; if true it should
  // raycast forward from outBeamOrigin along outBeamDir up to BEAM_RANGE
  // and apply continuous damage to the first hit.
  bool beamIsFiringThisTick(Vector3 &outOrigin, Vector3 &outDir);
  // Auto Turret fire — runs each tick when the subsystem is on AND a
  // target sat in the firing cone; the player has no input here, the
  // turret picks its own moment.
  bool consumePendingTurretShot(Vector3 &outPos, Vector3 &outVel);

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

  // Secondary slot. RMB fires whichever secondary is active. Each
  // variant tracks its own ammo. Cluster fires 4 pending shots in
  // one trigger pull (handled in consumePendingCluster).
  SecondaryWeapon m_secondaryWeapon = SecondaryWeapon::Missile;
  bool m_secondaryFireRequested = false;
  float m_secondaryTimer = 0.0f;
  int m_missileAmmo = Config::MISSILE_AMMO_MAX;
  int m_clusterAmmo = Config::CLUSTER_AMMO_MAX;
  int m_depthChargeAmmo = Config::DEPTH_CHARGE_MAX;
  bool m_pendingMissile = false;
  bool m_pendingDepthCharge = false;
  bool m_pendingCluster = false;
  Vector3 m_missilePos = {};
  Vector3 m_missileVel = {};
  Vector3 m_depthChargePos = {};
  Vector3 m_depthChargeVel = {};
  Vector3 m_clusterPos = {};
  Vector3 m_clusterVel = {};

  // Special slot. F activates whichever special is selected. EMP =
  // area stun, ShieldBooster = instant full sector refill. Each has
  // its own cooldown so cycling X mid-fight doesn't bypass them.
  SpecialWeapon m_specialWeapon = SpecialWeapon::EMP;
  bool m_specialFireRequested = false;
  bool m_pendingEMP = false;
  bool m_pendingShieldBoost = false;
  float m_empCooldown = 0.0f;
  float m_shieldBoosterCooldown = 0.0f;
  Vector3 m_empPos = {};

  // Beam Laser — continuous-fire primary. Energy meter drains while
  // firing (m_beamFiring tracks this tick's state for HUD + render);
  // recharges otherwise. Beam raycast + damage application lives in
  // GameState because it needs EntityManager access; Player just
  // tracks energy + reports whether we're currently firing.
  bool m_beamFiring = false;
  float m_beamEnergy = Config::BEAM_ENERGY_MAX;
  Vector3 m_beamOrigin = {};
  Vector3 m_beamDir = {};

  // Auto Turret — independent passive subsystem. Toggled with T.
  // Picks its own target each tick when on. Aim is interpolated
  // toward the target so it doesn't snap-fire from any angle.
  bool m_autoTurretEnabled = false;
  float m_turretYaw = 0.0f; // world-space rotation of the parented turret
  float m_turretTimer = 0.0f;
  bool m_pendingTurretShot = false;
  Vector3 m_turretShotPos = {};
  Vector3 m_turretShotVel = {};

  // Edge-trigger state for Tab (primary cycle), Z (secondary cycle),
  // X (special cycle), F (fire special), T (Auto Turret toggle).
  // 120Hz physics over 60Hz render makes IsKeyPressed unreliable.
  bool m_tabWasDown = false;
  bool m_zWasDown = false;
  bool m_xWasDown = false;
  bool m_fWasDown = false;
  bool m_tWasDown = false;

  // Mesh / model
  Mesh m_mesh = {};
  Model m_model = {};
  bool m_built = false;
};
