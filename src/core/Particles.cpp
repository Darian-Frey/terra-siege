#include "Particles.hpp"
#include "raymath.h"
#include "world/Planet.hpp"
#include <cmath>

// --------------------------------------------------------------------
// RNG — local xorshift32 so the system doesn't depend on Heightmap's RNG.
// --------------------------------------------------------------------
float ParticleSystem::randF(float lo, float hi) {
  m_rng ^= m_rng << 13;
  m_rng ^= m_rng >> 17;
  m_rng ^= m_rng << 5;
  float t = static_cast<float>(m_rng) / static_cast<float>(0xFFFFFFFFu);
  return lo + t * (hi - lo);
}

// --------------------------------------------------------------------
// Init — build a 32x32 radial-gradient billboard texture once.
// White centre fading to transparent edges. Coloured per-particle at
// draw time via the tint argument.
// --------------------------------------------------------------------
void ParticleSystem::init() {
  if (m_textureLoaded) return;
  Image img = GenImageGradientRadial(32, 32, 0.0f, WHITE, BLANK);
  m_texture = LoadTextureFromImage(img);
  UnloadImage(img);
  m_textureLoaded = true;
  for (auto &p : m_particles) p.alive = false;
  m_liveCount = 0;
}

void ParticleSystem::unload() {
  if (m_textureLoaded) {
    UnloadTexture(m_texture);
    m_textureLoaded = false;
  }
  clear();
}

void ParticleSystem::clear() {
  for (auto &p : m_particles) p.alive = false;
  m_liveCount = 0;
  m_next = 0;
}

// --------------------------------------------------------------------
// Emit — round-robin allocate from the pool. If we wrap into a live
// slot, that particle is recycled (oldest-first replacement).
// --------------------------------------------------------------------
void ParticleSystem::emit(Vector3 p, Vector3 v, Color startColor,
                          float startSize, float lifetime, Shape shape,
                          uint8_t flags) {
  Particle &slot = m_particles[m_next];
  if (!slot.alive) ++m_liveCount;
  slot.pos = p;
  slot.vel = v;
  slot.startColor = startColor;
  slot.startSize = startSize;
  slot.age = 0.0f;
  slot.lifetime = lifetime;
  slot.alive = true;
  slot.shape = shape;
  slot.flags = flags;
  m_next = (m_next + 1) % m_particles.size();
}

// --------------------------------------------------------------------
// Exhaust burst — emits `EXHAUST_EMIT_RATE * dt` particles around the
// thruster, velocity = scaled thrustDir + lateral spread + ship inertia.
// Caller is expected to gate this on whether the player is thrusting.
// --------------------------------------------------------------------
void ParticleSystem::emitExhaust(Vector3 thrusterPos, Vector3 thrustDir,
                                 Vector3 shipVel, float dt) {
  // Accumulator-free: spawn a stochastic count this frame so the rate
  // averages correctly without needing per-frame leftover state.
  float expectedSpawns = Config::EXHAUST_EMIT_RATE * dt;
  int n = static_cast<int>(expectedSpawns);
  // Stochastic remainder
  if (randF(0.0f, 1.0f) < (expectedSpawns - static_cast<float>(n))) ++n;

  for (int i = 0; i < n; ++i) {
    // Small position jitter around the thruster (lateral, not along
    // thrustDir — gives the exhaust a column shape).
    Vector3 jitter = {randF(-0.25f, 0.25f), randF(-0.05f, 0.05f),
                      randF(-0.25f, 0.25f)};
    Vector3 pos = Vector3Add(thrusterPos, jitter);

    Vector3 thrust = Vector3Scale(thrustDir, Config::EXHAUST_INITIAL_SPEED);
    Vector3 spread = {randF(-Config::EXHAUST_SPREAD, Config::EXHAUST_SPREAD),
                      randF(-Config::EXHAUST_SPREAD, Config::EXHAUST_SPREAD),
                      randF(-Config::EXHAUST_SPREAD, Config::EXHAUST_SPREAD)};
    Vector3 vel = Vector3Add(thrust, spread);
    vel = Vector3Add(vel, shipVel); // inherit ship inertia

    // Hot-orange to red palette — exhaust colour, fades to dark over life.
    unsigned char r = static_cast<unsigned char>(randF(245.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(randF(140.0f, 200.0f));
    unsigned char b = static_cast<unsigned char>(randF(40.0f, 80.0f));
    Color col = {r, g, b, 230};

    float life = Config::EXHAUST_LIFETIME * randF(0.85f, 1.15f);
    emit(pos, vel, col, Config::EXHAUST_INITIAL_SIZE, life, Shape::Cube,
         FLAG_BOUNCE | FLAG_GRAVITY);
  }
}

// --------------------------------------------------------------------
// Update — age, integrate, gravity, ground bounce. Bounce algorithm
// matches Lander/Zarch (lander.bbcelite.com BounceParticle) — all 3
// velocity components halved on hit, Y reflected. Symmetric 50%
// energy loss gives the original's tight, low-bouncing exhaust feel.
// --------------------------------------------------------------------
void ParticleSystem::update(float dt, const Planet &planet) {
  for (auto &p : m_particles) {
    if (!p.alive) continue;
    p.age += dt;
    if (p.age >= p.lifetime) {
      p.alive = false;
      --m_liveCount;
      continue;
    }

    if (p.flags & FLAG_GRAVITY)
      p.vel.y -= Config::EXHAUST_GRAVITY * dt;

    p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));

    if (p.flags & FLAG_BOUNCE) {
      float groundH = planet.heightAt(p.pos.x, p.pos.z);
      if (p.pos.y < groundH) {
        // Snap to ground and reflect — match the original's symmetric
        // half-velocity-on-bounce behaviour.
        p.pos.y = groundH;
        if (p.vel.y < 0.0f)
          p.vel.y = -p.vel.y * Config::EXHAUST_RESTITUTION;
        p.vel.x *= Config::EXHAUST_BOUNCE_FRICTION;
        p.vel.z *= Config::EXHAUST_BOUNCE_FRICTION;
      }
    }
  }
}

// --------------------------------------------------------------------
// Render — billboard each live particle with a tinted, alpha-faded
// copy of the shared texture. Size shrinks from start toward 0.3*start
// over the particle's lifetime so exhaust gas appears to dissipate.
// --------------------------------------------------------------------
void ParticleSystem::render(Camera3D camera) const {
  Rectangle src = {};
  if (m_textureLoaded) {
    src = {0.0f, 0.0f, static_cast<float>(m_texture.width),
           static_cast<float>(m_texture.height)};
  }

  for (const auto &p : m_particles) {
    if (!p.alive) continue;
    float t = p.age / p.lifetime;
    if (t > 1.0f) t = 1.0f;

    // Modest size shrink, alpha holds bright until last 25% of life
    // then fades off — matches the original's "stays sharp, then
    // disappears" behaviour rather than a long soft fade.
    float size = p.startSize * (1.0f - 0.4f * t);
    Color tint = p.startColor;
    float alphaT = (t < 0.75f) ? 1.0f : (1.0f - (t - 0.75f) / 0.25f);
    tint.a = static_cast<unsigned char>(p.startColor.a * alphaT);

    if (p.shape == Shape::Cube) {
      // Small flat-shaded cube — Archimedes-era pixel-block look.
      DrawCubeV(p.pos, {size, size, size}, tint);
    } else if (m_textureLoaded) {
      DrawBillboardRec(camera, m_texture, src, p.pos, {size, size}, tint);
    }
  }
  (void)camera;
}
