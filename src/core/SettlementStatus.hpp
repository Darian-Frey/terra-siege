#pragma once

#include "raylib.h"

#include <cstdint>
#include <string>
#include <vector>

class EntityManager;
class Planet;

// ====================================================================
// Settlement gameplay state (Settlements track R.8a, F-052).
//
// Settlements are infection targets, in the spirit of Virus. Infection
// lives on the ground (InfectionField): Seeders spread it under
// themselves, and a settlement's infection is the mean over its
// footprint. Past SETTLEMENT_LOST_LEVEL it is lost, and it stays lost
// until the player has cleansed it below SETTLEMENT_RECLAIM_LEVEL.
// Flying low with no Seeder near cleanses the ground under the player.
//
// One lightweight record per settlement, not an entity (D-012). Towns
// and villages are "listed": they have a generated name, a radar marker
// and HUD events. Farmsteads are tracked (Seeders target them) but
// stay quiet.
// ====================================================================
class SettlementStatus {
public:
  enum class State : uint8_t { Safe, UnderAttack, Infecting, Infected };
  struct Entry {
    int site = -1;          // index into Planet::settlements().sites()
    Vector3 pos{};          // centre, in the world square
    float radius = 0.0f;
    bool town = false;
    bool listed = false;    // a town or village: named, on the radar, in events
    std::string name;
    float infection = 0.0f; // 0..1
    State state = State::Safe;
    float sinceThreat = 1e9f; // seconds since an infector was in reach
    bool lost = false;        // reached full infection; needs cleansing
  };
  // Something that happened this tick, for the HUD and audio.
  struct Event {
    enum class Kind : uint8_t { UnderAttack, Lost, Reclaimed } kind;
    int entry;
  };

  // Every settlement site (towns and villages first, then farmsteads).
  void build(const Planet &planet, uint32_t seed);
  // Cleanse under a low player, read each settlement's infection off the
  // ground, update states. Appends this tick's events.
  void update(float dt, const EntityManager &entities, Vector3 playerPos, float playerAgl,
              Planet &planet, std::vector<Event> &events);
  int listedCount() const { return m_listed; }

  const std::vector<Entry> &entries() const { return m_entries; }
  int lostCount() const;

private:
  std::vector<Entry> m_entries;
  int m_listed = 0;
};
