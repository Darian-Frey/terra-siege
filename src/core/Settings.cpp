#include "Settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

// ====================================================================
// Helpers
// ====================================================================
namespace {
std::string trim(const std::string &s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
    ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                   s[b - 1] == '\n'))
    --b;
  return s.substr(a, b - a);
}

bool parseBool(const std::string &v) {
  std::string t = v;
  for (auto &c : t)
    if (c >= 'A' && c <= 'Z') c += 32;
  return (t == "true" || t == "1" || t == "yes" || t == "on");
}
} // namespace

// ====================================================================
// Path resolution — Linux uses $HOME/.config/terra-siege/settings.cfg.
// Creates the parent directory if missing so save() never fails on a
// fresh install.
// ====================================================================
std::string Settings::defaultPath() {
  const char *home = std::getenv("HOME");
  if (!home || !*home) return "settings.cfg"; // fallback to cwd
  std::filesystem::path p = home;
  p /= ".config";
  p /= "terra-siege";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  p /= "settings.cfg";
  return p.string();
}

// ====================================================================
// load — merges file values onto current state. Unknown keys ignored
// so the file format can grow without breaking older saves.
// ====================================================================
bool Settings::load(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) return false;

  std::string line;
  while (std::getline(f, line)) {
    // Strip comments
    auto h = line.find('#');
    if (h != std::string::npos) line = line.substr(0, h);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;

    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (key.empty()) continue;

    if (key == "invert_yaw")
      invertYaw = parseBool(val);
    else if (key == "invert_pitch")
      invertPitch = parseBool(val);
    else if (key == "god_mode")
      godMode = parseBool(val);
    else if (key == "default_view") {
      try {
        defaultView = std::stoi(val);
      } catch (...) {
      }
      if (defaultView < 0) defaultView = 0;
      if (defaultView > 4) defaultView = 4;
    } else if (key == "wireframe_hud")
      wireframeHUD = parseBool(val);
    else if (key == "master_volume") {
      try {
        masterVolume = std::stof(val);
      } catch (...) {
      }
      if (masterVolume < 0.0f) masterVolume = 0.0f;
      if (masterVolume > 1.0f) masterVolume = 1.0f;
    }
  }
  return true;
}

// ====================================================================
// save — writes a complete config so the file is self-documenting.
// ====================================================================
bool Settings::save(const std::string &path) const {
  std::ofstream f(path, std::ios::out | std::ios::trunc);
  if (!f.is_open()) return false;

  f << "# terra-siege settings — auto-generated, hand-editable\n";
  f << "invert_yaw = " << (invertYaw ? "true" : "false") << "\n";
  f << "invert_pitch = " << (invertPitch ? "true" : "false") << "\n";
  f << "god_mode = " << (godMode ? "true" : "false") << "\n";
  f << "default_view = " << defaultView
    << "  # 0=Chase 1=Velocity 2=Tactical 3=ThreatLock 4=Classic\n";
  f << "wireframe_hud = " << (wireframeHUD ? "true" : "false") << "\n";
  f << "master_volume = " << masterVolume
    << "  # 0..1; 0 = silent\n";
  return true;
}
