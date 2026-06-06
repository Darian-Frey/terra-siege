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

  if (auto it = root.find("hull");
      it != root.end() && it->second.is<picojson::object>()) {
    v.hullPresent = true;
    const auto &o = it->second.get<picojson::object>();
    extractFloat(o, "hp", v.hullHP, warnings, "hull");
    extractFloat(o, "collisionRadius", v.hullCollisionRadius, warnings, "hull");
    extractFloat(o, "mass", v.hullMass, warnings, "hull");
    if (auto wit = o.find("wreckage");
        wit != o.end() && wit->second.is<picojson::object>()) {
      const auto &w = wit->second.get<picojson::object>();
      extractFloat(w, "metal", v.hullWreckageMetal, warnings, "hull.wreckage");
      extractFloat(w, "bio", v.hullWreckageBio, warnings, "hull.wreckage");
    }
  }

  if (auto it = root.find("shields");
      it != root.end() && it->second.is<picojson::object>()) {
    v.shieldsPresent = true;
    const auto &o = it->second.get<picojson::object>();
    extractString(o, "model", v.shieldModel, warnings, "shields");
    extractFloat(o, "hp", v.shieldHP, warnings, "shields");
    extractFloat(o, "regen", v.shieldRegen, warnings, "shields");
    extractFloat(o, "delay", v.shieldDelay, warnings, "shields");
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

  if (auto it = root.find("weapons");
      it != root.end() && it->second.is<picojson::array>()) {
    const auto &arr = it->second.get<picojson::array>();
    v.weapons.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
      if (!arr[i].is<picojson::object>()) {
        warnings.emplace_back("weapons[" + std::to_string(i) +
                              "]: expected object");
        continue;
      }
      const auto &o = arr[i].get<picojson::object>();
      std::string p = "weapons[" + std::to_string(i) + "]";
      ProfileView::Weapon w;
      extractString(o, "name", w.name, warnings, p.c_str());
      extractString(o, "type", w.type, warnings, p.c_str());
      extractFloat(o, "fireRate", w.fireRate, warnings, p.c_str());
      extractFloat(o, "damage", w.damage, warnings, p.c_str());
      extractFloat(o, "projSpeed", w.projSpeed, warnings, p.c_str());
      extractFloat(o, "range", w.range, warnings, p.c_str());
      // ammo is an int but stored as a JSON number — accept either
      // double or int64 by going through the double extractor and
      // truncating.
      if (auto ait = o.find("ammo");
          ait != o.end() && ait->second.is<double>()) {
        w.ammo = static_cast<int>(ait->second.get<double>());
      }
      extractString(o, "cooldownGroup", w.cooldownGroup, warnings, p.c_str());
      v.weapons.push_back(std::move(w));
    }
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

// Fetch (or create) a nested JSON object by key. Returned reference
// is stable for the rest of the merge — picojson::object is just a
// std::map, so insert + lookup return stable refs.
picojson::object &ensureObject(picojson::object &parent, const char *key) {
  auto it = parent.find(key);
  if (it == parent.end() || !it->second.is<picojson::object>())
    parent[key] = picojson::value(picojson::object{});
  return parent[key].get<picojson::object>();
}

// Merge every typed field the inspector currently understands back
// into the DOM. F.1 set forward/scale/pivot; F.2 adds identity/hull/
// shields. Keys we don't know stay untouched — the round-trip
// preservation test (T-unknown-keys) validates this.
//
// `present` flags gate the F.2 sections: writing a hull / shields
// block when the file never had one would be surprising (the
// inspector hasn't edited it). When the user enters values via the
// F.2 tools they'll flip the present flag themselves, so this is
// only a guard against the "open / save unchanged" path.
void mergeTypedEdits(picojson::value &root, const ProfileView &v) {
  if (!root.is<picojson::object>()) {
    root = picojson::value(picojson::object{});
  }
  auto &obj = root.get<picojson::object>();

  // F.1 transform — always written (these have safe defaults).
  obj["forward"] = vec3Value(v.forward);
  obj["scale"] = picojson::value(static_cast<double>(v.scale));
  obj["pivot"] = vec3Value(v.pivot);

  // identity — written if any string is non-empty so an unchanged
  // file with no identity section doesn't sprout an empty one.
  if (!v.displayName.empty() || !v.entityClass.empty() ||
      !v.faction.empty() || obj.find("identity") != obj.end()) {
    auto &ident = ensureObject(obj, "identity");
    if (!v.displayName.empty())
      ident["displayName"] = picojson::value(v.displayName);
    if (!v.entityClass.empty())
      ident["class"] = picojson::value(v.entityClass);
    if (!v.faction.empty())
      ident["faction"] = picojson::value(v.faction);
  }

  // hull — written when the present flag is up (it flips at load
  // time iff the JSON had a hull block, or when the HullTool edits).
  if (v.hullPresent) {
    auto &h = ensureObject(obj, "hull");
    h["hp"] = picojson::value(static_cast<double>(v.hullHP));
    h["collisionRadius"] =
        picojson::value(static_cast<double>(v.hullCollisionRadius));
    h["mass"] = picojson::value(static_cast<double>(v.hullMass));
    if (v.hullWreckageMetal > 0.0f || v.hullWreckageBio > 0.0f ||
        h.find("wreckage") != h.end()) {
      auto &w = ensureObject(h, "wreckage");
      w["metal"] =
          picojson::value(static_cast<double>(v.hullWreckageMetal));
      w["bio"] = picojson::value(static_cast<double>(v.hullWreckageBio));
    }
  }

  // shields — same present-flag guard.
  if (v.shieldsPresent) {
    auto &s = ensureObject(obj, "shields");
    s["model"] = picojson::value(v.shieldModel);
    s["hp"] = picojson::value(static_cast<double>(v.shieldHP));
    s["regen"] = picojson::value(static_cast<double>(v.shieldRegen));
    s["delay"] = picojson::value(static_cast<double>(v.shieldDelay));
  }

  // weapons — full overwrite of the array when the tool has anything
  // to say. The "anything to say" test is the conservative one:
  // either the in-memory list is non-empty, OR a weapons key already
  // existed (so we don't strip it on a no-op save).
  if (!v.weapons.empty() || obj.find("weapons") != obj.end()) {
    picojson::array arr;
    arr.reserve(v.weapons.size());
    for (const ProfileView::Weapon &w : v.weapons) {
      picojson::object wo;
      wo["name"] = picojson::value(w.name);
      if (!w.type.empty()) wo["type"] = picojson::value(w.type);
      wo["fireRate"] = picojson::value(static_cast<double>(w.fireRate));
      wo["damage"] = picojson::value(static_cast<double>(w.damage));
      wo["projSpeed"] = picojson::value(static_cast<double>(w.projSpeed));
      wo["range"] = picojson::value(static_cast<double>(w.range));
      if (w.ammo >= 0)
        wo["ammo"] = picojson::value(static_cast<double>(w.ammo));
      if (!w.cooldownGroup.empty())
        wo["cooldownGroup"] = picojson::value(w.cooldownGroup);
      arr.emplace_back(wo);
    }
    obj["weapons"] = picojson::value(arr);
  }

  // hardpoints — same rule. Each hardpoint is a small flat object;
  // we always write pos / dir / fireArcDeg numerically and only emit
  // name + weapon when non-empty (so freshly-added hardpoints round
  // trip without empty-string clutter).
  if (!v.hardpoints.empty() || obj.find("hardpoints") != obj.end()) {
    picojson::array arr;
    arr.reserve(v.hardpoints.size());
    for (const ProfileView::Hardpoint &hp : v.hardpoints) {
      picojson::object ho;
      if (!hp.name.empty()) ho["name"] = picojson::value(hp.name);
      ho["pos"] = vec3Value(hp.pos);
      ho["dir"] = vec3Value(hp.dir);
      ho["fireArcDeg"] =
          picojson::value(static_cast<double>(hp.fireArcDeg));
      if (!hp.weapon.empty())
        ho["weapon"] = picojson::value(hp.weapon);
      arr.emplace_back(ho);
    }
    obj["hardpoints"] = picojson::value(arr);
  }
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
  mergeTypedEdits(root, profile.view);

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
