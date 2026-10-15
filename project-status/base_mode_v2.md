# Base Mode (v2) — Design

**Status:** Active design as of 2026-06-02. Supersedes Part 2 of [game_modes_and_features.md](project-status/game_modes_and_features.md) (the original 20v20 design). That doc stands as historical reference; carryover sections (resources, weapons, helicopter mode, smoke trail, etc.) still apply unchanged.

**Track:** Slice C in [ROADMAP.md](ROADMAP.md). Implementation begins after Slices A and B are stable.

---

## What changed from v1

| | v1 (game_modes_and_features.md) | v2 (this doc) |
|--|--|--|
| Starting state | 20 friendly bases vs 20 enemy bases | Friendly bases + radar network; a few enemy landers already grounded |
| Faction symmetry | Symmetric, both sides have autonomous AI economies running in parallel | Asymmetric — friendly is static infrastructure, enemy is the active invader |
| Map layout | Two halves split along Z with no-man's-land at each front | Wide-open map; landers scattered anywhere |
| Player's role | Combatant alongside friendly AI economy | Sole mobile defender — friendly side is mostly passive infrastructure |
| Win condition | All enemy bases destroyed or infected | All enemy landers destroyed AND all infected bases retaken |
| Loss condition | All friendly bases lost OR player destroyed | Unchanged |
| Infection rule | Shields-stripped → Infectious Missile flips | Shields-stripped **AND** hull ≤30% → flip. Same in both directions |
| Infection re-flip | Once flipped, cannot flip again (ships and bases) | **Ships**: rule unchanged (permanent). **Bases**: can flip freely |
| Radar | Per-base detection ring | **Connected network** — bases + radar stations share detection across the cluster; degrades visibly as nodes fall |
| Score | Live ratio of friendly vs enemy base count | Map state — count of landers remaining + count of infected bases not yet recovered |

---

## Game shape

Asymmetric single-player. Player is the only mobile defender of an established planetary infrastructure under invasion.

**Friendly side** is static: bases produce defender ships only when attacked, share resources only when explicitly threatened. No autonomous AI economy running in the background. Most of the friendly side is "infrastructure that exists until destroyed".

**Enemy side** is the active threat. Each grounded lander runs its own little RTS: collectors gather, builders extend the local radar ring, ships venture out to attack friendly bases. Landers expand by infecting captured friendly bases (which then become enemy bases producing more enemies).

**Player** flies between the two. Decides which friendly bases to defend, which enemy landers to attack, when to commit the time-cost of stripping shields + damaging hull on a base for an infection flip vs just destroying it.

---

## Starting conditions

| Difficulty | Friendly bases | Enemy landers | Notes |
|--|--|--|--|
| Easy | 12 | 3 | All TUNE |
| Normal | 10 | 5 | |
| Hard | 8 | 7 | Faster enemy production tick? |

At game start:

- All friendly bases pre-placed; radar network live and fully connected
- All enemy landers already grounded — visible on the player's radar from second one
- Player takes off from a landing pad at one designated friendly base
- Player has standard loadout (loadout selection still applies as in wave mode)

**No more landers arrive during play.** The threat is finite from second one. The game is "clear the map" — long enough to feel meaningful, short enough to end.

---

## Win condition

Both must be true:

1. All enemy landers destroyed (including any landers that were grounded at the start)
2. All infected bases retaken (re-flipped friendly) OR destroyed (acceptable but reduces the friendly side's count)

When both clear, the game ends in victory and the session-result screen shows.

## Loss condition

Either:

- All friendly bases lost (destroyed or infected, with no recoverable bases left)
- Player ship destroyed (existing rule from wave mode)

---

## Lander entity (new)

The defining new entity. Acts as a flying base on the way in, a grounded base after landing.

**Properties:**

| Property | Value | Notes |
|--|--|--|
| HP (hull) | ~600 | TUNE — significantly larger than Carrier (which is the previous biggest enemy) |
| Shields | 4-sector, ~200 HP per sector | Sturdy. Stripping all four is a real commitment |
| Mass | High | Mostly a stationary or slow-moving target |
| Visual | Bulky industrial — Carrier-scale silhouette, more square than the curved Carrier |

**Lifecycle:**

1. **Pre-landing (initial state — already grounded at game start, OR descending if we add late-arriving landers later):** Same combat profile as grounded. If we add descent later, mid-descent is a vulnerability window — shields active but lander cannot manoeuvre.
2. **Grounded:** Acts as an enemy base. Runs production. Has hardpoints (a few defensive turrets). Radar ring built around it by its first builder.
3. **Destroyed:** Large explosion. Drops Wreckage node (Metal yield ≥ Carrier, ~150). All ships it spawned that are still alive continue with leaderless behaviour from v1 §2.7. Builder + collectors in its area become harvestable wreckage.

**Production** carries over from v1 §2.3 mostly unchanged — Drones / Fighters / Collectors / Builders on autonomous timers gated by resource cost. Drones are *enemy* drones now (not player-callable orbit drones — those still come from friendly bases in v1 §6).

---

## Friendly base (simplified)

Carries the v1 base struct but the autonomous-economy fields go away. A friendly base is *mostly passive*. It exists. It has shields and hull. It has a landing pad the player can dock at. It's connected to neighbours via the radar network.

**What friendly bases still do:**

- **Defend when attacked.** Take damage → shields/hull deplete → produce a few Fighter-class defenders to engage the attacker. Defenders are leaderless after spawn (existing AI).
- **Repair the player when docked.** Land on the pad → hull repairs over time, ammo restocks, missiles reloaded.
- **Send help calls across the radar network** (see Radar Network section).
- **Be infectable.** If shields go down AND hull drops to ≤30%, an infection missile flips the base to enemy.

**What friendly bases don't do:**

- No autonomous collectors (no friendly resource economy)
- No autonomous builders (radar stations are pre-built at game start; rebuild is out of scope for v2 — destroyed radars stay destroyed)
- No autonomous reinforcement calls to other friendly bases (other than via the help-call network)
- No fighter production for offense (only for defense when directly attacked)

The friendly side is *infrastructure under attack*, not an economy at war.

---

## Enemy base (= grounded lander)

A grounded lander runs the v1 §2.3 production loop largely unchanged:

- **Drones** — kamikaze swarm escort, defends the lander
- **Fighters** — venture out to attack friendly bases
- **Collectors** — drive to nearby resource nodes, return cargo, accumulate Metal + Bio at the lander
- **Builders** — construct a radar ring around the lander (inner 8, outer 12 — v1 §3.2 numbers)

Resource gating, production timers, builder behaviour all carry from v1 §2.3 / §3 unchanged.

**Key difference:** there's no global friendly economy to compete against. The enemy lander's expansion is bounded by what its collectors can find in its local area. Resource nodes within range of an enemy lander are *contested* — friendly collectors don't exist to deny them, but the player can destroy enemy collectors or starve a lander by killing nodes (via player fly-through harvest of the contested node).

---

## Infection mechanic (updated)

Applies to bases only — ships are still permanent-flip per v1 §5.1.

**Requirements to infect a base** (same in both directions — player infecting an enemy base, OR enemy ships infecting a friendly base):

1. All four shield sectors at zero
2. Hull HP ≤ 30% of max
3. Apply infection — for the player this is the Infectious Missile from v1 §7.4. For enemies, presumed equivalent ground-attack from a Bomber or specialised infection-carrier ship *(open question — see below)*

**On flip:**

- Base immediately changes faction
- Shields begin recharging from 0
- Hull begins regen from 30% (per v1 §2.6 `BASE_HULL_REGEN`)
- 60s recovery window during which the base is vulnerable but already producing for its new faction
- Ships already spawned by the base retain their original faction (don't flip with the base)
- New ships produced after infection are the new faction

**Re-infection:** a base can be flipped any number of times. No "permanent" lockout. Each flip resets the recovery timer.

**Why hull ≤30% on top of shields-stripped:** infection should feel like a *major investment* not a quick trick. Stripping shields alone is too easy with the Shield Laser from Slice B. Requiring 70% hull damage means the player has to commit a real attack pass — Shield Laser → Plasma/Beam to do hull damage → Infectious Missile. Symmetric for enemy attackers, slowing the rate at which the AI takes friendly bases.

---

## Radar network

The radar network is the player's strategic awareness — and a visible degradation curve as the invasion succeeds.

**Topology:**

- Every friendly base and every standing radar station is a **node**
- Two nodes are **linked** if they're within `NETWORK_LINK_RANGE` (TUNE — try 250–400 units) of each other
- Nodes in a connected component share enemy detection — any sighting at any node in the cluster shows on the player's HUD radar

**Coverage:**

- Each node has its own detection range (v1 §3.2: 150 / 350 / 600 depending on its radar ring completion)
- The total coverage of a cluster is the union of all member detection circles
- Holes appear where clusters split or coverage circles shrink

**Player's personal radar:**

The player ship has a **short-range personal radar** that always works, regardless of the network state. Range: ~150 units (TUNE — slightly less than a single radar-equipped base). It gives the player situational awareness in their immediate vicinity even if every friendly node falls.

**Visualization (HUD radar disc):**

- Long-range network blips drawn one way (e.g. larger, fully opaque)
- Personal-radar blips drawn another way (e.g. smaller, slightly transparent) so the player can tell which is which
- Areas outside both coverage types are blacked out / fogged on the HUD radar
- As the network breaks down, more of the radar disc fogs over — visible loss curve

**Open visual question** *(deferred to implementation)*: do we also show the network *graph* on the HUD (lines between linked nodes) so the player can see which connections are intact, or only the detection *output* (where enemies are)? Showing the graph is more readable; showing only output is more immersive. Try output-only first, add lines if play-testers want them.

---

## Help calls

When a friendly base takes damage:

1. The base broadcasts a "help-call" event into its connected radar cluster
2. Every node in the cluster (including the player if their personal radar is currently inside the cluster) receives the event
3. The player HUD shows a directional indicator: "Base X under attack" with an arrow pointing toward it

**Network breakdown impact:**

- If a friendly base is in a cluster the player isn't connected to, the player doesn't get the help-call
- This means defending the network has tactical value beyond just keeping radars alive — a broken network means bases die silently
- The session-result screen can show how many help-calls went unanswered as a stat

---

## Player resources (carries from v1 §4.3)

Unchanged from v1 — the player still maintains a small Metal + Bio pool in base mode, used for resource-cost weapons (Shield Missile, Infectious Missile) per v1 §7.3 / §7.4. Fly-through harvest, base supply within `BASE_SUPPLY_RANGE` all unchanged.

The simplification is on the *friendly base side* (no autonomous economy), not the *player side*.

---

## Slice C sub-slicing

Suggested sub-slices, each independently shippable:

| Sub-slice | Scope | Notes |
|--|--|--|
| **C.1** | Game mode picker (Wave / Base) + Base Mode shell that boots the player into an empty map | UI work + GameMode enum + state transitions |
| **C.2** | Friendly base entity (passive, defender production on damage, landing-pad repair) + radar station entity | Carries from v1 base struct with autonomous fields stripped |
| **C.3** | Lander entity + grounded production (collectors, builders, fighters, drones) | The new big piece — landers are the active invader |
| **C.4** | Radar network — node connectivity graph, cluster-shared detection, fogging of unreached HUD areas + personal radar | Pure information layer over existing entities |
| **C.5** | Help calls — broadcast on damage, propagation along network, HUD indicator | Builds on C.4's connectivity graph |
| **C.6** | Infection v2 — shields-stripped + hull ≤30% gate, symmetric direction, re-flip allowed | Modifies the Slice B infection state machine |
| **C.7** | Win / loss detection + session-result screen | Once all the moving parts work |
| **C.8** | Difficulty presets + base/lander placement balancing | TUNE pass |

C.1 and C.2 are independent and can land in either order. Everything else stacks roughly in sequence.

---

## Open questions (deferred to implementation)

These don't block writing the slicing plan — flag with `// TUNE` or `// DESIGN` in code when they come up:

1. **How does the enemy infect a friendly base?** Same Infectious Missile carried by a specific enemy ship type (Bomber-class infection-carrier)? Or a slow drain when enemies occupy the area around a damaged base?
2. **Continuous landers arriving later?** Confirmed NO for v2 — the initial landers are the whole threat. Reconsider only if play-testing finds the game ends too quickly.
3. **Show the radar network graph on HUD, or just the output?** Try output-only first.
4. **`NETWORK_LINK_RANGE` value.** Start at 300 units. Should be tuned so the default placement gives a connected network but losing 1–2 mid-cluster bases visibly fragments it.
5. **Player ship can't be infected** (carryover from v1 §5.1) — still undercuts theme. v2 doesn't change this; revisit if play-testing makes the theme feel hollow.
6. **Friendly-collector-and-builder rebuilds for destroyed radars?** v2 says no — destroyed radars stay destroyed. Reconsider if play-testing finds the loss curve too steep.

---

## Carryover from v1 (unchanged)

These v1 sections still apply verbatim — see [game_modes_and_features.md](project-status/game_modes_and_features.md) for spec:

- §4 Resource System (Metal + Bio, node types, wreckage decay) — unchanged
- §5 Infection — ship-side rules unchanged (permanent flip, reboot period, infected ship behaviour). Base-side rules updated per this doc
- §6 Orbit Drone System — player-callable drones from friendly bases, unchanged
- §7 New Weapons — Shield Laser, Shield Missile, Infectious Missile, 4-weapon cycle, shared energy pool. All carried over and land in Slice B
- §8 Helicopter Flight Mode — independent of base mode, ships when ships
- §9 Damaged Ship Visuals — smoke trails + retreat AI. Slice A.

---

## Related

- [ROADMAP.md](ROADMAP.md) — three-track plan and Slice C placement
- [game_modes_and_features.md](project-status/game_modes_and_features.md) — original v1 design, kept as historical reference
- [terra_siege_inspect_roadmap.md](terra_siege_inspect_roadmap.md) — inspector roadmap; Phase E (primitives) + F.6 (resources tool) needed for authoring lander / radar / builder meshes during Slice C
