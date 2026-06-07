#pragma once

#include "raylib.h"

#include <array>
#include <cstdint>
#include <vector>

// ====================================================================
// AudioManager — Engine Phase 5 / first cut.
//
// In-engine procedural SFX synthesis (no external assets). The retro
// 1988 aesthetic is well served by simple parametric waveforms: short
// downward saw sweep for the cannon, filtered white-noise burst for
// the kill explosion, sine harmonics for shield engagement, etc.
// gen_sfx.sh + rFXGen are documented for higher-fidelity follow-up
// authoring — runtime is independent of either.
//
// Mixing model:
//   * raylib's Sound is a one-shot played from a CPU sample buffer.
//   * For overlap (cannon at 20Hz fire rate), each SfxId owns a pool
//     of N aliases (LoadSoundAlias shares the buffer, separate playback
//     state). play3D picks the first non-playing alias, falls back to
//     round-robin steal if all are busy.
//   * Positional audio is camera-relative: pan from sign of dot(dir,
//     listenerRight); volume from a linear distance falloff capped at
//     SFX_MAX_DIST. UI sounds bypass positional logic via play2D.
//
// Init/unload is paired with InitAudioDevice/CloseAudioDevice in
// GameState. Setting the master volume to 0 effectively mutes without
// detaching aliases.
// ====================================================================

enum class SfxId {
  CannonShot,
  PlasmaShot,
  BeamTick,        // short tone repeated while beam is firing
  MissileLaunch,
  Hit,             // generic projectile hit
  KillExplosion,   // enemy destroyed
  PlayerHit,       // distinct thud when the player takes damage
  ShieldFlash,     // player shield absorbs a hit
  Pickup,
  UIBlip,
  Count
};

class AudioManager {
public:
  static constexpr int kPoolSizePerSound = 6;
  // 22050 Hz mono 16-bit — same target as gen_sfx.sh's default. Half
  // the data of 44100 and the retro warmth fits the aesthetic.
  static constexpr int kSampleRate = 22050;

  // Camera-relative positional audio falloff distance — beyond this
  // the sound plays at 0 volume (so distant cannon fire doesn't clutter
  // the soundscape). Tuned to match the player's typical engagement
  // range (~300 units).
  static constexpr float kMaxDistance = 350.0f;

  AudioManager() = default;
  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;

  // Initialise the audio device + generate all sounds. Call once at
  // startup (after InitAudioDevice succeeds). Safe to call when the
  // audio device is unavailable: every play* call becomes a no-op.
  void init();
  void unload();

  // Update the listener pose each frame. listenerRight is used for
  // panning (left/right placement); forward is unused right now but
  // kept so future Doppler / HRTF logic has the data it needs.
  void setListener(Vector3 pos, Vector3 forward, Vector3 right);

  // Play a sound at a world position. Pan + volume are computed from
  // the cached listener pose. volMul lets callers scale per-event
  // (e.g. quieter for friendly auto-turret fire so the player's own
  // shots stay dominant).
  void play3D(SfxId id, Vector3 worldPos, float volMul = 1.0f);

  // UI / non-positional sound — full volume, centered pan.
  void play2D(SfxId id, float volMul = 1.0f);

  // Global multiplier on every sound (0..1). Survives across frames.
  void setMasterVolume(float v);
  float masterVolume() const { return m_masterVolume; }

  bool isReady() const { return m_ready; }

private:
  // Pick a free alias slot for `id` (or round-robin steal). Returns
  // nullptr if the manager isn't ready.
  Sound *acquireSlot(SfxId id);

  // Random pitch [-variance, +variance] applied at play time so each
  // shot reads as a discrete event rather than a sample loop. ±15%
  // on cannon shots is the lively sweet spot per the indie sound-
  // design literature; recurring musical tones (BeamTick / Pickup /
  // UIBlip / ShieldFlash) stay at 0 so they keep their note.
  float pitchVarianceFor(SfxId id) const;
  // Tiny xorshift32 — independent from rest of game RNG so audio
  // playback timing doesn't perturb gameplay determinism.
  uint32_t m_rngState = 0x9e3779b9u;
  float randPitch(float variance);

  bool m_ready = false;
  float m_masterVolume = 0.7f;
  Vector3 m_listenerPos{0, 0, 0};
  Vector3 m_listenerForward{0, 0, 1};
  Vector3 m_listenerRight{1, 0, 0};

  // Source sounds (one per SfxId) — the audio buffer raylib uses.
  std::array<Sound, static_cast<size_t>(SfxId::Count)> m_sources{};
  std::array<bool, static_cast<size_t>(SfxId::Count)> m_sourceLoaded{};

  // Alias pool per SfxId — N concurrent voices.
  std::array<std::array<Sound, kPoolSizePerSound>,
             static_cast<size_t>(SfxId::Count)> m_aliases{};
  std::array<int, static_cast<size_t>(SfxId::Count)> m_nextAlias{};
};
