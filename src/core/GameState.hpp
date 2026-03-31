#pragma once

// ====================================================================
// GameState — top-level state machine
// Owns all subsystems and orchestrates update/render each frame.
// ====================================================================

enum class AppState {
  MainMenu,
  Playing,
  Paused,
  GameOver,
  Victory,
};

class GameState {
public:
  void init();
  void update(float dt);
  void render(float alpha);
  void shutdown();

  AppState state() const { return m_state; }

private:
  AppState m_state = AppState::MainMenu;

  // Subsystems will be added here as phases progress
  // e.g.:
  //   Planet          m_planet;
  //   EntityManager   m_entities;
  //   WeaponSystem    m_weapons;
  //   ShieldSystem    m_shields;
  //   WaveManager     m_waves;
  //   AudioManager    m_audio;
  //   HUD             m_hud;
};