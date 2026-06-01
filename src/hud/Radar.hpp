#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include <array>
#include <cstdint>
#include <vector>

class EntityManager;

// ====================================================================
// Radar — tactical situational-awareness display.
//
// Tier 1 (this file): disc + altitude strip + IFF blips with shape per
// enemy type + proximity pulse blink. Roadmap calls out Tier 1 as
// MANDATORY before any multi-enemy wave testing — the player can't
// fight what they can't see, and altitude info is the limiting cue
// in 3D combat.
//
// Tier 2 / 3 (deferred): threat vectors, missile warning ring, zone
// overlay, Carrier drone count, ghost blips for lost contacts,
// jamming jitter, radar boost visual. The ghost pool is pre-allocated
// here so when Tier 3 lands no member layout shifts.
//
// See terra_rebuild/radar_system.md for the full spec.
// ====================================================================

enum class BlipType : uint8_t {
  Drone,
  Seeder,
  Fighter,
  Bomber,
  Carrier,
  Turret,
  Friendly,
  Pickup,
  Base,
  LaunchPad,
  Ghost,
};

struct RadarContact {
  Vector3 position{};
  Vector3 velocity{};
  BlipType blipType = BlipType::Fighter;
  float distance = 0.0f;
  float altDelta = 0.0f; // contact.y - player.y
  Color colour = WHITE;
  uint32_t id = 0;       // entity ID — used as jitter seed in Tier 3
  int spawnedCount = 0;  // Carrier drone count label (Tier 2)
};

struct GhostBlip {
  Vector3 lastPos{};
  BlipType type = BlipType::Drone;
  float lifetime = 0.0f;
  bool active = false;
};

// Tier 2 missile warning — one per incoming enemy projectile that's
// closing on the player AND inside RADAR_MISSILE_WARN_MIN. Drawn as
// a fast-blinking red arrow on the disc rim pointing at the threat.
struct IncomingThreat {
  float bearing = 0.0f; // radians in ship-local frame (0 = nose, +X = right)
  float tti = 0.0f;     // seconds to closest approach
};

class Radar {
public:
  Radar() = default;

  // Per-tick: rebuild contacts list from EntityManager.
  void update(float dt, const EntityManager &entities, Vector3 playerPos,
              float playerYaw, float gameTime);

  // Draw the disc + altitude strip at the given screen-space centre.
  void draw(Vector2 discCentre, float discRadius) const;

  // Draw only the altitude strip — used by the Tactical view where
  // the disc is redundant (the whole screen is overhead).
  void drawAltitudeStripOnly(Vector2 stripTopLeft, float height,
                             float width) const;

  bool boosted() const { return m_boosted; }
  float activeRange() const { return m_activeRange; }

private:
  void drawDisc(Vector2 centre, float radius) const;
  void drawAltitudeStrip(Vector2 stripTopLeft, float height,
                         float width) const;
  void drawBlip(Vector2 radarPos, float distance, BlipType type, Color col,
                Vector3 velocity, float playerYaw) const;

  // Ego-centric coordinate transform — rotates world XZ by -playerYaw
  // so the player's nose direction is always "up" on the disc.
  Vector2 worldToRadar(Vector3 worldPos, Vector2 discCentre,
                       float discRadius) const;

  // Primitive helpers — small geometric primitives for blip shapes.
  static void drawTriangleBlip(Vector2 pos, Color col, float size,
                               float facingRad);
  static void drawDiamondBlip(Vector2 pos, Color col, float size);
  static void drawStarBlip(Vector2 pos, Color col, float size);

  // Altitude-relative tint — brighter above, darker below.
  static Color altitudeTint(Color base, float altDelta);

  // Pulse-blink helper — true while blip should be visible this tick.
  static bool blipVisibleNow(float distance, float gameTime);

  // Per-tick state
  std::vector<RadarContact> m_contacts;
  std::array<GhostBlip, 32> m_ghosts{}; // pre-allocated for Tier 3
  Vector3 m_playerPos = {};
  float m_playerYaw = 0.0f;
  float m_activeRange = Config::RADAR_BASE_RANGE;
  float m_gameTime = 0.0f;

  bool m_boosted = false; // wired to RadarBooster friendly when alive

  // Tier 2/3 state
  // Snapshot of last-tick contacts so we can spot disappearances
  // (entity dies / leaves the pool) and spawn ghosts at last-known pos.
  struct PrevContact {
    uint32_t id = 0;
    Vector3 pos = {};
    BlipType type = BlipType::Drone;
  };
  std::vector<PrevContact> m_prevContacts;

  // Jam factor — 0 = clean, 1 = max jitter. Set per tick from the
  // distance to the nearest live Carrier (only enemies jam radar).
  float m_jamFactor = 0.0f;

  // Incoming-threat list rebuilt each tick from the projectile pool.
  // Capped at a reasonable max — chaos still shouldn't fill the rim.
  std::vector<IncomingThreat> m_threats;

  // Helpers
  void updateGhosts(float dt);
  // Apply jam jitter to a screen-space blip position. seed is the
  // contact's entity id so per-blip jitter stays stable frame-to-frame
  // (with a gameTime perturbation so the noise is actually animated).
  Vector2 applyJam(Vector2 pos, uint32_t seed) const;
  // Draw a small velocity-direction arrow at radarPos pointing along
  // the contact's horizontal velocity, length proportional to speed.
  void drawVelocityArrow(Vector2 radarPos, Vector3 velocity,
                         float playerYaw, Color col) const;
  // Draw the rim-mounted incoming-missile warnings as red blinking
  // arrows pointing at each threat's bearing.
  void drawMissileWarnings(Vector2 centre, float radius) const;
};
