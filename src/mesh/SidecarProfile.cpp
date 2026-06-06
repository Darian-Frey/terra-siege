#include "SidecarProfile.hpp"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <fstream>
#include <sstream>

namespace tsmesh {

// ====================================================================
// ProfileDom — pimpl wrapper around picojson::value so the header
// doesn't drag the parser include into every consumer.
// ====================================================================
struct ProfileDom {
  picojson::value root; // top-level object (or null if no file)
};

void ProfileDomDeleter::operator()(ProfileDom *p) const noexcept {
  delete p;
}

namespace {

// Tiny scalar extractors. Each takes the field's owning object plus a
// key + reference to write into; flips `present` on success. Bad
// types append a warning rather than throwing — partial data still
// renders, the warning tells the user what was wrong.
void extractString(const picojson::object &obj, const char *key,
                   std::string &out, std::vector<std::string> &warnings,
                   const char *path) {
  auto it = obj.find(key);
  if (it == obj.end()) return;
  if (!it->second.is<std::string>()) {
    warnings.emplace_back(std::string(path) + "." + key + ": expected string");
    return;
  }
  out = it->second.get<std::string>();
}

void extractFloat(const picojson::object &obj, const char *key, float &out,
                  std::vector<std::string> &warnings, const char *path) {
  auto it = obj.find(key);
  if (it == obj.end()) return;
  if (!it->second.is<double>()) {
    warnings.emplace_back(std::string(path) + "." + key + ": expected number");
    return;
  }
  out = static_cast<float>(it->second.get<double>());
}

void extractBool(const picojson::object &obj, const char *key, bool &out,
                 std::vector<std::string> &warnings, const char *path) {
  auto it = obj.find(key);
  if (it == obj.end()) return;
  if (!it->second.is<bool>()) {
    warnings.emplace_back(std::string(path) + "." + key + ": expected bool");
    return;
  }
  out = it->second.get<bool>();
}

// 3-float array → Vector3. Accepts [x, y, z]; warns on length mismatch
// or non-number entries. Missing key is silent (defaults stand).
void extractVec3(const picojson::object &obj, const char *key, Vector3 &out,
                 std::vector<std::string> &warnings, const char *path) {
  auto it = obj.find(key);
  if (it == obj.end()) return;
  if (!it->second.is<picojson::array>()) {
    warnings.emplace_back(std::string(path) + "." + key +
                          ": expected array [x, y, z]");
    return;
  }
  const auto &arr = it->second.get<picojson::array>();
  if (arr.size() != 3) {
    warnings.emplace_back(std::string(path) + "." + key +
                          ": expected exactly 3 elements");
    return;
  }
  float xyz[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    if (!arr[i].is<double>()) {
      warnings.emplace_back(std::string(path) + "." + key + "[" +
                            std::to_string(i) + "]: expected number");
      return;
    }
    xyz[i] = static_cast<float>(arr[i].get<double>());
  }
  out = {xyz[0], xyz[1], xyz[2]};
}

// Walk the typed sections we understand. Anything we don't touch
// survives because the full DOM is preserved on save.
void extractView(const picojson::object &root, ProfileView &v,
                 std::vector<std::string> &warnings) {
  extractVec3(root, "forward", v.forward, warnings, "");
  extractFloat(root, "scale", v.scale, warnings, "");
  extractVec3(root, "pivot", v.pivot, warnings, "");

  if (auto it = root.find("identity");
      it != root.end() && it->second.is<picojson::object>()) {
    const auto &o = it->second.get<picojson::object>();
    extractString(o, "displayName", v.displayName, warnings, "identity");
    extractString(o, "class", v.entityClass, warnings, "identity");
    extractString(o, "faction", v.faction, warnings, "identity");
  }

  if (auto it = root.find("ai");
      it != root.end() && it->second.is<picojson::object>()) {
    v.aiPresent = true;
    const auto &o = it->second.get<picojson::object>();
    extractFloat(o, "detectionRange", v.detectionRange, warnings, "ai");
    extractFloat(o, "attackRange", v.attackRange, warnings, "ai");
    extractFloat(o, "evadeAtHPFrac", v.evadeAtHPFrac, warnings, "ai");
    extractFloat(o, "retreatAtHPFrac", v.retreatAtHPFrac, warnings, "ai");
  }

  if (auto it = root.find("fx");
      it != root.end() && it->second.is<picojson::object>()) {
    v.fxPresent = true;
    const auto &o = it->second.get<picojson::object>();
    extractFloat(o, "smokeAtHPFrac", v.smokeAtHPFrac, warnings, "fx");
  }

  if (auto it = root.find("hardpoints");
      it != root.end() && it->second.is<picojson::array>()) {
    const auto &arr = it->second.get<picojson::array>();
    v.hardpoints.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      if (!arr[i].is<picojson::object>()) {
        warnings.emplace_back("hardpoints[" + std::to_string(i) +
                              "]: expected object");
        continue;
      }
      const auto &o = arr[i].get<picojson::object>();
      std::string p = "hardpoints[" + std::to_string(i) + "]";
      ProfileView::Hardpoint hp;
      extractString(o, "name", hp.name, warnings, p.c_str());
      extractVec3(o, "pos", hp.pos, warnings, p.c_str());
      extractVec3(o, "dir", hp.dir, warnings, p.c_str());
      extractFloat(o, "fireArcDeg", hp.fireArcDeg, warnings, p.c_str());
      extractString(o, "weapon", hp.weapon, warnings, p.c_str());
      v.hardpoints.push_back(std::move(hp));
    }
  }
}

// Build a 3-element JSON array from a Vector3.
picojson::value vec3Value(Vector3 v) {
  picojson::array a;
  a.emplace_back(static_cast<double>(v.x));
  a.emplace_back(static_cast<double>(v.y));
  a.emplace_back(static_cast<double>(v.z));
  return picojson::value(a);
}

// Merge the F.1 typed fields (forward / scale / pivot) back into the
// DOM. Other keys are left untouched. If the root is not an object
// (or is null because the file didn't exist), seed a fresh empty
// object before writing.
void mergeF1Edits(picojson::value &root, const ProfileView &v) {
  if (!root.is<picojson::object>()) {
    root = picojson::value(picojson::object{});
  }
  auto &obj = root.get<picojson::object>();
  obj["forward"] = vec3Value(v.forward);
  obj["scale"] = picojson::value(static_cast<double>(v.scale));
  obj["pivot"] = vec3Value(v.pivot);
}

} // anonymous namespace

std::filesystem::path sidecarPathFor(const std::filesystem::path &obj) {
  std::filesystem::path p = obj;
  p.replace_extension(".meta.json");
  return p;
}

bool loadProfile(const std::filesystem::path &path, EntityProfile &out) {
  out.warnings.clear();
  out.view = ProfileView{};
  out.dom.reset(new ProfileDom{});
  out.loaded = false;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return false; // missing is fine — defaults stand
  }

  std::ifstream f(path);
  if (!f.is_open()) {
    out.warnings.emplace_back("sidecar: open failed: " + path.string());
    return false;
  }

  std::stringstream ss;
  ss << f.rdbuf();
  std::string raw = ss.str();

  std::string err = picojson::parse(out.dom->root, raw);
  if (!err.empty()) {
    out.warnings.emplace_back("sidecar parse: " + err);
    out.dom->root = picojson::value{};
    return false;
  }
  if (!out.dom->root.is<picojson::object>()) {
    out.warnings.emplace_back("sidecar: top-level must be an object");
    return false;
  }

  extractView(out.dom->root.get<picojson::object>(), out.view, out.warnings);
  out.loaded = true;
  return true;
}

bool saveProfile(const std::filesystem::path &path,
                 const EntityProfile &profile) {
  // Ensure the DOM is initialised even if the file didn't exist on
  // disk before — we create one with just the F.1 fields populated.
  picojson::value root;
  if (profile.dom && profile.dom->root.is<picojson::object>()) {
    root = profile.dom->root;
  }
  mergeF1Edits(root, profile.view);

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::out | std::ios::trunc);
  if (!f.is_open()) return false;
  // serialize with prettify=true so hand-editing the file later stays
  // pleasant. picojson's pretty-printer uses 2-space indent.
  f << root.serialize(true);
  return f.good();
}

} // namespace tsmesh
