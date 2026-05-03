#pragma once

#include "core/Config.hpp"
#include "raylib.h"
#include <array>
#include <cstddef>
#include <cstdint>

class Planet; // forward — Particles.cpp includes the header for heightAt

// ====================================================================
// Particles — flat pre-allocated CPU pool sized at PARTICLE_POOL_SIZE.
//
// Particles are simple billboarded points: position + velocity,
// linear age/lifetime, start/end colour and size. Render uses raylib
// DrawBillboardRec against a runtime-generated radial-gradient texture
// for additive-style glow without an asset dependency.
//
// Round-robin allocator — when the pool fills, oldest particle is
// recycled. With 2000 slots and 140 emit/sec exhaust at 0.35s
// lifetime (~50 active), there's huge headroom for explosions and
// missile trails later.
// ====================================================================

class ParticleSystem {
public:
  ParticleSystem() = default;
  ~ParticleSystem() = default;

  // Generate the shared particle texture. Call once after the GL
  // context exists (i.e. after InitWindow but before any emit).
  void init();
  void unload();

  // Reset all particles to inactive. Use when reloading the world.
  void clear();

  // Visual shape per particle. Cube = small flat-shaded box (original
  // Virus / Zarch geometric look). Billboard = soft glow, kept around
  // for future use on explosions / missile trails.
  enum class Shape : uint8_t { Cube = 0, Billboard = 1 };

  // Physics flags per particle.
  static constexpr uint8_t FLAG_BOUNCE = 1u << 0;  // bounce off terrain
  static constexpr uint8_t FLAG_GRAVITY = 1u << 1; // apply gravity to velocity

  // Spawn a single particle at world position p with initial velocity
  // v, given start colour, start size, lifetime in seconds, shape,
  // and physics flags.
  void emit(Vector3 p, Vector3 v, Color startColor, float startSize,
            float lifetime, Shape shape = Shape::Cube,
            uint8_t flags = FLAG_BOUNCE | FLAG_GRAVITY);

  // Specialised emitter for ship exhaust. Bursts emitRate*dt particles
  // per frame at the thruster point with velocity along thrustDir
  // plus random spread, inheriting shipVel for inertia.
  void emitExhaust(Vector3 thrusterPos, Vector3 thrustDir, Vector3 shipVel,
                   float dt);

  // Tick all live particles forward in time. Applies per-particle
  // gravity and ground bouncing using the planet's heightmap.
  void update(float dt, const Planet &planet);

  // Draw all live particles as billboards against the camera.
  // Must be called inside BeginMode3D / EndMode3D.
  void render(Camera3D camera) const;

  int liveCount() const { return m_liveCount; }

private:
  struct Particle {
    Vector3 pos = {};
    Vector3 vel = {};
    Color startColor = WHITE;
    float startSize = 0.5f;
    float age = 0.0f;
    float lifetime = 0.0f;
    bool alive = false;
    Shape shape = Shape::Cube;
    uint8_t flags = 0;
  };

  std::array<Particle, Config::PARTICLE_POOL_SIZE> m_particles{};
  size_t m_next = 0;        // round-robin allocator cursor
  int m_liveCount = 0;      // for debug/HUD telemetry
  Texture2D m_texture = {}; // shared radial-gradient billboard
  bool m_textureLoaded = false;

  // xorshift32 RNG, deterministic per construction
  uint32_t m_rng = 0xC0FFEEu;
  float randF(float lo, float hi);
};
