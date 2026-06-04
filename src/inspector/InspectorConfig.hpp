#pragma once

#include <filesystem>
#include <vector>

namespace tsmesh {

// Per-user inspector config. Lives at ~/.config/terra-siege/inspector.cfg
// next to settings.cfg. Holds the recent-files MRU list plus the last
// directory the open-file dialog visited so re-launches resume where
// the user left off. Hand-editable plain key=value text, one path per
// line for the recents block.
struct InspectorConfig {
  static constexpr size_t kMaxRecent = 5;

  std::vector<std::filesystem::path> recentFiles; // most-recent first
  std::filesystem::path lastDirectory;

  bool load(const std::filesystem::path &path);
  bool save(const std::filesystem::path &path) const;

  // Promote `p` to the head of the MRU. Caps at kMaxRecent entries.
  void pushRecent(const std::filesystem::path &p);

  // Resolve the platform config path. Linux: $HOME/.config/terra-siege/
  // inspector.cfg. Creates the parent directory if it doesn't exist.
  static std::filesystem::path defaultPath();
};

} // namespace tsmesh
