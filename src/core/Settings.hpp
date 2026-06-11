#pragma once

#include <string>

// ====================================================================
// Settings — runtime-configurable values that survive across launches.
//
// All fields default to sensible values; load() merges from the config
// file, save() writes the current state out. Format is plain key=value
// text so users can hand-edit if needed.
//
// Defaults reflect the current play feel: yaw inverted (user reported
// left/right reads backwards with the standard convention), pitch not
// inverted, god mode off, default camera = Chase, wireframe HUD on.
// ====================================================================

struct Settings {
  bool invertYaw = true;
  bool invertPitch = false;
  bool godMode = false;
  int defaultView = 0;     // matches CameraView enum (0=Chase…4=Classic)
  bool wireframeHUD = true;
  float masterVolume = 0.7f; // 0..1, applied to AudioManager at boot
  // Slice C — last game mode picked from the main menu. Persisted
  // so the user doesn't have to re-pick after every relaunch.
  // 0 = Wave, 1 = Base; matches GameMode enum.
  int lastGameMode = 0;

  // Returns true on successful read; missing file is not an error
  // (defaults remain in place, file will be created on first save).
  bool load(const std::string &path);
  bool save(const std::string &path) const;

  // Resolve the platform settings path. Linux: $HOME/.config/terra-siege/
  // Returns the full path including the file name. Creates the parent
  // directory if it doesn't exist.
  static std::string defaultPath();
};
