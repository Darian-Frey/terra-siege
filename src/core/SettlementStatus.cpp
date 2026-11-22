#include "SettlementStatus.hpp"

#include "core/Config.hpp"
#include "entity/EntityManager.hpp"
#include "world/Planet.hpp"
#include "world/Settlements.hpp"

#include <cmath>

namespace {

uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

// Generated place name: one or two syllables and an English-sounding
// ending ("Ashford", "Kelmby", "Stanwick").
std::string makeName(uint32_t h, bool town) {
  static const char *kStart[] = {"Ash", "Bram", "Cal", "Dun", "Elm", "Fen", "Gor", "Hal", "Ing", "Kel",
                                 "Lan", "Mar", "Nor", "Ock", "Pen", "Ravel", "Stan", "Thorn", "Ul", "Wen",
                                 "Brack", "Cold", "Hart", "Mill", "Oak", "Red", "Wit", "Yar"};
  static const char *kMid[] = {"", "", "", "er", "en", "ley", "ing", "ow"};
  static const char *kEnd[] = {"ford", "by", "ham", "ton", "wick", "field", "stead", "bury", "thorpe",
                               "mouth", "well", "combe", "den", "worth"};
  const auto pick = [&](const char *const *list, size_t n) {
    h = mix(h + 0x9e3779b9u);
    return list[h % n];
  };
  std::string name = pick(kStart, sizeof(kStart) / sizeof(kStart[0]));
  name += pick(kMid, sizeof(kMid) / sizeof(kMid[0]));
  name += pick(kEnd, sizeof(kEnd) / sizeof(kEnd[0]));
  if (town && (h & 7u) == 0) name = "Great " + name;
  return name;
}

} // namespace

void SettlementStatus::build(const Planet &planet, uint32_t seed) {
  m_entries.clear();
  const auto &sites = planet.settlements().sites();
  for (int pass = 0; pass < 2; ++pass) // listed (towns, villages) first
    for (size_t i = 0; i < sites.size(); ++i) {
      const SettlementSite &s = sites[i];
      const bool farm = s.kind == SettlementSite::Kind::Farm;
      if (farm != (pass == 1)) continue;
      Entry e;
      e.site = static_cast<int>(i);
      e.pos = {s.x, s.y, s.z};
      e.radius = s.radius;
      e.town = s.kind == SettlementSite::Kind::Town;
      e.listed = !farm;
      if (e.listed) e.name = makeName(mix(seed ^ static_cast<uint32_t>(i) * 0x85ebca6bu), e.town);
      m_entries.push_back(e);
    }
  m_listed = 0;
  for (const Entry &e : m_entries) m_listed += e.listed;
}

int SettlementStatus::lostCount() const {
  int n = 0;
  for (const Entry &e : m_entries) n += e.lost && e.listed;
  return n;
}

void SettlementStatus::update(float dt, const EntityManager &entities, Vector3 playerPos,
                              float playerAgl, Planet &planet, std::vector<Event> &events) {
  InfectionField &field = planet.infection();
  // Cleansing: a low player with no hostile Seeder near scrubs the ground.
  if (playerAgl < Config::SETTLEMENT_CLEANSE_AGL) {
    bool seederNear = false;
    for (const Entity &e : entities.entities()) {
      if (!e.alive || e.type != EntityType::Seeder || e.aiState == AIState::Infected ||
          e.aiState == AIState::Infecting)
        continue;
      const float dx = e.pos.x - playerPos.x, dz = e.pos.z - playerPos.z;
      if (dx * dx + dz * dz < Config::CLEANSE_SAFE_RANGE * Config::CLEANSE_SAFE_RANGE) {
        seederNear = true;
        break;
      }
    }
    if (!seederNear)
      field.cleanse(playerPos.x, playerPos.z, Config::CLEANSE_RADIUS, Config::CLEANSE_RATE * dt);
  }

  for (size_t i = 0; i < m_entries.size(); ++i) {
    Entry &st = m_entries[i];
    // An infecting Seeder: low over the settlement.
    bool attacked = false;
    for (const Entity &e : entities.entities()) {
      if (!e.alive || e.type != EntityType::Seeder || e.aiState == AIState::Infected ||
          e.aiState == AIState::Infecting)
        continue;
      const Vector3 c = planet.nearestCopy(st.pos, e.pos);
      const float dx = e.pos.x - c.x, dz = e.pos.z - c.z, r = st.radius + Config::SEEDER_INFECT_REACH;
      if (dx * dx + dz * dz < r * r && e.pos.y - planet.surfaceAt(e.pos.x, e.pos.z) < Config::SEEDER_INFECT_AGL + 20.0f) {
        attacked = true;
        break;
      }
    }
    if (attacked) {
      if (st.listed && st.sinceThreat > Config::SETTLEMENT_ALERT_REPEAT)
        events.push_back({Event::Kind::UnderAttack, static_cast<int>(i)});
      st.sinceThreat = 0.0f;
    } else {
      st.sinceThreat += dt;
    }
    st.infection = field.average(st.pos.x, st.pos.z, st.radius, st.radius > 60.0f ? 16.0f : 8.0f);
    if (!st.lost && st.infection >= Config::SETTLEMENT_LOST_LEVEL) {
      st.lost = true;
      if (st.listed) events.push_back({Event::Kind::Lost, static_cast<int>(i)});
    } else if (st.lost && st.infection <= Config::SETTLEMENT_RECLAIM_LEVEL) {
      st.lost = false;
      if (st.listed) events.push_back({Event::Kind::Reclaimed, static_cast<int>(i)});
    }
    st.state = attacked             ? State::UnderAttack
               : st.lost            ? State::Infected
               : st.infection > 0.02f ? State::Infecting
                                      : State::Safe;
  }
}
