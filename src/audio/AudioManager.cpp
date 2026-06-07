#include "AudioManager.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// ====================================================================
// Synth helpers — 16-bit mono raylib Wave at AudioManager::kSampleRate.
// Allocated via malloc() so raylib's UnloadWave can free() the buffer
// (Wave.data is a void* matching raylib's LoadWave / UnloadWave
// contract).
//
// Three techniques used throughout that the first-cut synth lacked:
//
//   * LAYERING — every percussive sound mixes 3 voices (body / sub-
//     kick / noise transient). Real laser/explosion design relies on
//     this; a single oscillator always reads as "synthy". Mix is
//     summed pre-saturation so peaks soft-clip into harmonics rather
//     than hard-clipping.
//   * BIT-CRUSH — selected sounds quantise their float sample to N
//     amplitude levels before the s16 write. 6-bit (64 levels) gives
//     the retro hardware grit without sounding broken; musical tones
//     (BeamTick / ShieldFlash / Pickup) stay clean.
//   * SOFT-CLIP — tanh saturator on the mixed sum so layered peaks
//     warm up instead of clipping flat. Adds the harmonic richness a
//     mastering compressor would.
//
// Per-shot pitch variance lives in AudioManager::play3D — not here.
// ====================================================================
namespace {

constexpr int kRate = AudioManager::kSampleRate;
constexpr float kPI = 3.14159265358979f;
constexpr float kTwoPI = 2.0f * kPI;

uint32_t xorshift32(uint32_t &state) {
  uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

// White-noise sample in [-1, +1].
float noise(uint32_t &rng) {
  return (static_cast<int32_t>(xorshift32(rng)) / 2147483648.0f);
}

// Linear attack / exponential decay envelope.
float env(float t, float attackSec, float decayRate) {
  float a = (attackSec > 0.0f) ? std::min(1.0f, t / attackSec) : 1.0f;
  float d = expf(-decayRate * std::max(0.0f, t - attackSec));
  return a * d;
}

// Bit-crusher: quantise [-1, +1] sample to `levels` amplitude steps.
// 64 = 6-bit (crunchy but clean), 16 = 4-bit (chiptune). Bypass with
// levels >= 32768 (effectively no quantisation visible after the s16
// write).
inline float bitCrush(float s, int levels) {
  if (levels >= 32767) return s;
  float lvl = static_cast<float>(levels);
  return roundf(s * lvl) / lvl;
}

// Soft-clip / saturator. tanh-based — peaks fold into harmonic
// distortion rather than hard-clipping. Drive of 1.3 fattens layered
// mixes without overcooking single-oscillator sounds.
inline float softClip(float s, float drive = 1.3f) {
  return tanhf(s * drive);
}

inline void writeS16(int16_t *out, size_t idx, float s) {
  if (s > 1.0f) s = 1.0f;
  if (s < -1.0f) s = -1.0f;
  out[idx] = static_cast<int16_t>(s * 32700.0f);
}

Wave makeWave(float durationSec) {
  Wave w{};
  w.sampleRate = static_cast<unsigned int>(kRate);
  w.sampleSize = 16;
  w.channels = 1;
  w.frameCount =
      static_cast<unsigned int>(std::max(1.0f, durationSec * kRate));
  size_t bytes = static_cast<size_t>(w.frameCount) * sizeof(int16_t);
  w.data = std::malloc(bytes);
  std::memset(w.data, 0, bytes);
  return w;
}

// ---- Cannon ----
// THREE LAYERS — bright body (saw down-sweep 800→180 Hz), sub-kick
// (sine 120→40 Hz, fast decay), noise transient (4 ms snap). 6-bit
// crush + soft-clip on the mix. ~90 ms total.
Wave synthCannon() {
  Wave w = makeWave(0.090f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rng = 0x9e3779b9u;
  float pBody = 0.0f, pKick = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;

    // L1: Bright saw body — down-sweep gives the "shot fired" gesture.
    float bodyFreq = 800.0f * expf(-22.0f * t) + 180.0f;
    pBody += bodyFreq / kRate;
    if (pBody >= 1.0f) pBody -= 1.0f;
    float body = (2.0f * pBody - 1.0f) * 0.55f;
    float bodyEnv = expf(-38.0f * t);

    // L2: Sub-kick — adds the felt "thump" most retro shooters miss.
    float kickFreq = 120.0f * expf(-50.0f * t) + 40.0f;
    pKick += kickFreq / kRate;
    float kick = sinf(kTwoPI * pKick) * 0.45f;
    float kickEnv = expf(-55.0f * t);

    // L3: Noise transient — sharp first 4 ms.
    float transient = 0.0f;
    if (t < 0.004f) {
      float k = 1.0f - (t / 0.004f);
      transient = noise(rng) * 0.7f * k;
    }

    float mix = body * bodyEnv + kick * kickEnv + transient;
    mix = bitCrush(mix, 64); // 6-bit
    mix = softClip(mix, 1.3f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Plasma ----
// Chorused sine pair + octave overtone + a noise tail that rides the
// release for "energy" character. Lighter crush (8-bit) keeps the
// melodic core clean.
Wave synthPlasma() {
  Wave w = makeWave(0.140f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rng = 0xfeedb00bu;
  float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    float f1 = 280.0f * (1.0f + 0.10f * sinf(kTwoPI * 6.0f * t));
    float f2 = 290.0f;
    float f3 = 560.0f;
    p1 += f1 / kRate; p2 += f2 / kRate; p3 += f3 / kRate;
    float core = sinf(kTwoPI * p1) * 0.45f +
                 sinf(kTwoPI * p2) * 0.35f +
                 sinf(kTwoPI * p3) * 0.18f;
    // Noise tail — fades in from 30 ms, lingers as the tone fades.
    float tailEnv = (t > 0.030f) ? (1.0f - expf(-12.0f * (t - 0.030f))) : 0.0f;
    float tail = noise(rng) * 0.18f * tailEnv;
    float e = env(t, 0.005f, 18.0f);
    float mix = (core + tail) * e;
    mix = bitCrush(mix, 256); // 8-bit
    mix = softClip(mix, 1.15f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Beam tick ----
// Looped sustain — must crossfade cleanly. Two harmonised sines at
// 320/480 Hz + a faint 80 Hz body. No bit-crush (musical).
Wave synthBeamTick() {
  Wave w = makeWave(0.060f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    p1 += 320.0f / kRate; p2 += 480.0f / kRate; p3 += 80.0f / kRate;
    float s = sinf(kTwoPI * p1) * 0.32f +
              sinf(kTwoPI * p2) * 0.22f +
              sinf(kTwoPI * p3) * 0.18f;
    float duration = 0.060f;
    float fade = 1.0f;
    if (t < 0.005f) fade = t / 0.005f;
    else if (t > duration - 0.005f) fade = (duration - t) / 0.005f;
    writeS16(buf, i, softClip(s * fade, 1.1f));
  }
  return w;
}

// ---- Missile launch ----
// THREE LAYERS — filtered noise whoosh, descending sine rumble, a
// click transient at t=0. 6-bit crush for grit. 260 ms.
Wave synthMissileLaunch() {
  Wave w = makeWave(0.260f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rng = 0xa5a5deadu;
  float pRumble = 0.0f;
  float lp = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;

    // L1: filtered noise (whoosh).
    float n = noise(rng);
    lp = lp + (n - lp) * 0.16f;
    float whoosh = lp * 0.75f;

    // L2: descending sub-rumble.
    float rumbleFreq = 95.0f * expf(-3.0f * t) + 55.0f;
    pRumble += rumbleFreq / kRate;
    float rumble = sinf(kTwoPI * pRumble) * 0.6f;

    // L3: click — first 3 ms.
    float click = 0.0f;
    if (t < 0.003f) click = noise(rng) * 0.9f * (1.0f - t / 0.003f);

    float e = env(t, 0.010f, 5.5f);
    float mix = (whoosh + rumble + click) * e;
    mix = bitCrush(mix, 64); // 6-bit
    mix = softClip(mix, 1.25f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Hit ----
// Quick noise burst + tiny sub click. 60 ms. 6-bit crush.
Wave synthHit() {
  Wave w = makeWave(0.060f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rng = 0x12345abcu;
  float pSub = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    float n = noise(rng);
    pSub += (180.0f * expf(-50.0f * t)) / kRate;
    float sub = sinf(kTwoPI * pSub) * 0.5f;
    float e = env(t, 0.001f, 75.0f);
    float mix = (n * 0.85f + sub) * e;
    mix = bitCrush(mix, 64);
    mix = softClip(mix, 1.3f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Kill explosion ----
// THREE LAYERS — sub-bass kick (60→25 Hz), filtered noise body, late
// crackle. 700 ms. 8-bit crush — power needs depth, full crunch
// would make it small.
Wave synthKillExplosion() {
  Wave w = makeWave(0.700f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rngBody = 0xbadf00du;
  uint32_t rngCrackle = 0x1337c0deu;
  float pKick = 0.0f;
  float lp = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;

    // L1: filtered noise body.
    float n = noise(rngBody);
    lp = lp + (n - lp) * 0.08f;
    float body = lp * 0.65f;

    // L2: deep sub-bass kick — falls 60→25 Hz, slow envelope.
    float kickFreq = 60.0f * expf(-2.5f * t) + 25.0f;
    pKick += kickFreq / kRate;
    float kick = sinf(kTwoPI * pKick) * 0.95f;
    float kickEnv = expf(-4.0f * t);

    // L3: late crackle — random sparse impulses after 100 ms.
    float crackle = 0.0f;
    if (t > 0.08f) {
      // ~5% chance per sample of a small click.
      uint32_t r = xorshift32(rngCrackle);
      if ((r & 0xff) < 5) crackle = noise(rngCrackle) * 0.35f * expf(-3.0f * t);
    }

    float bodyEnv = expf(-5.5f * t);
    float mix = body * bodyEnv + kick * kickEnv + crackle;
    mix = bitCrush(mix, 256); // 8-bit
    mix = softClip(mix, 1.2f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Player hit ----
// THREE LAYERS — sharp click, bass body 90→35 Hz, mid-band rumble.
// Must FEEL like it hits the player. 250 ms.
Wave synthPlayerHit() {
  Wave w = makeWave(0.250f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  uint32_t rng = 0xc01dca7eu;
  float pBass = 0.0f, pMid = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;

    // L1: click — first 5 ms.
    float click = 0.0f;
    if (t < 0.005f) click = noise(rng) * 0.9f * (1.0f - t / 0.005f);

    // L2: bass body — deep 90→35 Hz sine.
    float bassFreq = 90.0f * expf(-7.0f * t) + 35.0f;
    pBass += bassFreq / kRate;
    float bass = sinf(kTwoPI * pBass) * 0.95f;
    float bassEnv = expf(-9.0f * t);

    // L3: mid rumble — broader spectrum so it cuts through bass.
    float midFreq = 220.0f * expf(-12.0f * t) + 80.0f;
    pMid += midFreq / kRate;
    float mid = sinf(kTwoPI * pMid) * 0.35f;
    float midEnv = expf(-15.0f * t);

    float mix = click + bass * bassEnv + mid * midEnv;
    mix = bitCrush(mix, 256); // 8-bit
    mix = softClip(mix, 1.25f);
    writeS16(buf, i, mix);
  }
  return w;
}

// ---- Shield flash ----
// Musical chord — A4 + E5 + A5 (root, fifth, octave). Stays clean
// (no crush). Tells the player "shield engaged" with a tonal pop.
Wave synthShieldFlash() {
  Wave w = makeWave(0.130f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    p1 += 440.0f / kRate;
    p2 += 660.0f / kRate;
    p3 += 880.0f / kRate;
    float s = sinf(kTwoPI * p1) * 0.40f +
              sinf(kTwoPI * p2) * 0.28f +
              sinf(kTwoPI * p3) * 0.18f;
    float e = env(t, 0.003f, 22.0f);
    writeS16(buf, i, softClip(s * e, 1.1f));
  }
  return w;
}

// ---- Pickup ----
// Ascending two-tone bleep — stays clean (musical). 180 ms.
Wave synthPickup() {
  Wave w = makeWave(0.180f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  float p = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    float freq = (t < 0.090f) ? 660.0f : 880.0f;
    p += freq / kRate;
    float s = sinf(kTwoPI * p) * 0.55f;
    float e = env(t, 0.005f, 10.0f);
    writeS16(buf, i, softClip(s * e, 1.1f));
  }
  return w;
}

// ---- UI blip ----
// Tiny short tick. 30 ms. Clean.
Wave synthUI() {
  Wave w = makeWave(0.030f);
  int16_t *buf = static_cast<int16_t *>(w.data);
  float p = 0.0f;
  for (unsigned i = 0; i < w.frameCount; ++i) {
    float t = static_cast<float>(i) / kRate;
    p += 1200.0f / kRate;
    float s = sinf(kTwoPI * p) * 0.30f;
    float e = env(t, 0.001f, 60.0f);
    writeS16(buf, i, s * e);
  }
  return w;
}

Wave synthForId(SfxId id) {
  switch (id) {
  case SfxId::CannonShot:    return synthCannon();
  case SfxId::PlasmaShot:    return synthPlasma();
  case SfxId::BeamTick:      return synthBeamTick();
  case SfxId::MissileLaunch: return synthMissileLaunch();
  case SfxId::Hit:           return synthHit();
  case SfxId::KillExplosion: return synthKillExplosion();
  case SfxId::PlayerHit:     return synthPlayerHit();
  case SfxId::ShieldFlash:   return synthShieldFlash();
  case SfxId::Pickup:        return synthPickup();
  case SfxId::UIBlip:        return synthUI();
  default: return Wave{};
  }
}

} // anonymous namespace

// ====================================================================
// AudioManager — init / unload / play
// ====================================================================
void AudioManager::init() {
  if (!IsAudioDeviceReady()) return;
  for (size_t i = 0; i < m_sources.size(); ++i) {
    SfxId id = static_cast<SfxId>(i);
    Wave w = synthForId(id);
    if (w.frameCount == 0) continue;
    m_sources[i] = LoadSoundFromWave(w);
    UnloadWave(w);
    m_sourceLoaded[i] = (m_sources[i].frameCount > 0);
    if (!m_sourceLoaded[i]) continue;
    for (int k = 0; k < kPoolSizePerSound; ++k) {
      m_aliases[i][k] = LoadSoundAlias(m_sources[i]);
    }
    m_nextAlias[i] = 0;
  }
  m_ready = true;
}

void AudioManager::unload() {
  if (!m_ready) return;
  for (size_t i = 0; i < m_sources.size(); ++i) {
    if (!m_sourceLoaded[i]) continue;
    for (int k = 0; k < kPoolSizePerSound; ++k) {
      UnloadSoundAlias(m_aliases[i][k]);
    }
    UnloadSound(m_sources[i]);
    m_sourceLoaded[i] = false;
  }
  m_ready = false;
}

void AudioManager::setListener(Vector3 pos, Vector3 forward, Vector3 right) {
  m_listenerPos = pos;
  m_listenerForward = forward;
  m_listenerRight = right;
}

void AudioManager::setMasterVolume(float v) {
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  m_masterVolume = v;
}

Sound *AudioManager::acquireSlot(SfxId id) {
  if (!m_ready) return nullptr;
  size_t i = static_cast<size_t>(id);
  if (i >= m_sources.size() || !m_sourceLoaded[i]) return nullptr;
  for (int tries = 0; tries < kPoolSizePerSound; ++tries) {
    int k = m_nextAlias[i];
    m_nextAlias[i] = (k + 1) % kPoolSizePerSound;
    if (!IsSoundPlaying(m_aliases[i][k])) return &m_aliases[i][k];
  }
  int k = m_nextAlias[i];
  m_nextAlias[i] = (k + 1) % kPoolSizePerSound;
  return &m_aliases[i][k];
}

float AudioManager::pitchVarianceFor(SfxId id) const {
  // Per-SfxId envelope. Percussive / weapon sounds get lots of
  // variance so consecutive shots feel like distinct events; tonal
  // sounds stay at 0 so they keep their musical identity.
  switch (id) {
  case SfxId::CannonShot:    return 0.15f;
  case SfxId::PlasmaShot:    return 0.08f;
  case SfxId::MissileLaunch: return 0.06f;
  case SfxId::Hit:           return 0.20f;
  case SfxId::KillExplosion: return 0.10f;
  case SfxId::PlayerHit:     return 0.05f;
  case SfxId::BeamTick:      return 0.0f;
  case SfxId::ShieldFlash:   return 0.0f;
  case SfxId::Pickup:        return 0.0f;
  case SfxId::UIBlip:        return 0.0f;
  default: return 0.0f;
  }
}

float AudioManager::randPitch(float variance) {
  if (variance <= 0.0f) return 1.0f;
  // [-variance, +variance] uniform around 1.0.
  uint32_t r = xorshift32(m_rngState);
  float u = (static_cast<int32_t>(r) / 2147483648.0f); // [-1, +1]
  return 1.0f + u * variance;
}

void AudioManager::play3D(SfxId id, Vector3 worldPos, float volMul) {
  Sound *s = acquireSlot(id);
  if (!s) return;

  Vector3 d = Vector3Subtract(worldPos, m_listenerPos);
  float dist = Vector3Length(d);
  if (dist >= kMaxDistance) return;
  float falloff = 1.0f - (dist / kMaxDistance);
  falloff = falloff * falloff;

  float pan = 0.5f;
  if (dist > 0.01f) {
    Vector3 dn = Vector3Scale(d, 1.0f / dist);
    float dot = Vector3DotProduct(dn, m_listenerRight);
    pan = 0.5f - dot * 0.45f;
    if (pan < 0.0f) pan = 0.0f;
    if (pan > 1.0f) pan = 1.0f;
  }

  SetSoundVolume(*s, falloff * volMul * m_masterVolume);
  SetSoundPan(*s, pan);
  SetSoundPitch(*s, randPitch(pitchVarianceFor(id)));
  PlaySound(*s);
}

void AudioManager::play2D(SfxId id, float volMul) {
  Sound *s = acquireSlot(id);
  if (!s) return;
  SetSoundVolume(*s, volMul * m_masterVolume);
  SetSoundPan(*s, 0.5f);
  SetSoundPitch(*s, randPitch(pitchVarianceFor(id)));
  PlaySound(*s);
}
