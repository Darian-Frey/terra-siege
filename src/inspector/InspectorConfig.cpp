#include "InspectorConfig.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace tsmesh {

namespace {

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
    ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                   s[b - 1] == '\n'))
    --b;
  return s.substr(a, b - a);
}

} // anonymous namespace

std::filesystem::path InspectorConfig::defaultPath() {
  const char *home = std::getenv("HOME");
  if (!home || !*home) return "inspector.cfg";
  std::filesystem::path p = home;
  p /= ".config";
  p /= "terra-siege";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  p /= "inspector.cfg";
  return p;
}

bool InspectorConfig::load(const std::filesystem::path &path) {
  std::ifstream f(path);
  if (!f.is_open()) return false;

  recentFiles.clear();
  std::string line;
  while (std::getline(f, line)) {
    auto h = line.find('#');
    if (h != std::string::npos) line = line.substr(0, h);
    line = trim(line);
    if (line.empty()) continue;

    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (key.empty() || val.empty()) continue;

    if (key == "recent") {
      if (recentFiles.size() < kMaxRecent) recentFiles.emplace_back(val);
    } else if (key == "last_dir") {
      lastDirectory = val;
    }
  }
  return true;
}

bool InspectorConfig::save(const std::filesystem::path &path) const {
  std::ofstream f(path, std::ios::out | std::ios::trunc);
  if (!f.is_open()) return false;

  f << "# terra-siege inspector config — auto-generated, hand-editable\n";
  if (!lastDirectory.empty())
    f << "last_dir = " << lastDirectory.string() << "\n";
  for (const auto &p : recentFiles)
    f << "recent = " << p.string() << "\n";
  return true;
}

void InspectorConfig::pushRecent(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(p, ec);
  if (ec) abs = p;
  auto it = std::find_if(recentFiles.begin(), recentFiles.end(),
                         [&](const std::filesystem::path &q) {
                           std::error_code ec2;
                           return std::filesystem::equivalent(q, abs, ec2);
                         });
  if (it != recentFiles.end()) recentFiles.erase(it);
  recentFiles.insert(recentFiles.begin(), abs);
  if (recentFiles.size() > kMaxRecent)
    recentFiles.resize(kMaxRecent);
}

} // namespace tsmesh
