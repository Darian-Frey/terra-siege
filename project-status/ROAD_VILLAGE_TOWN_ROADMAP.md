# Roads, Villages and Towns — Roadmap

**Status:** Proposed, 2026-11-22. Not started. Nothing here is committed to FEATURES or ROADMAP yet. Feature IDs, decisions and phases are proposals until accepted.
**Track:** New "Settlements" track, to sit beside the Engine, Features and Tooling tracks in [ROADMAP.md](../ROADMAP.md).
**Builds on:** the farmland patchwork and forest mask ([D-035](../DECISIONS.md#d-035-farmland-patchwork-from-an-angled-bsp-shared-by-terrain-and-decorations)), instanced scenery and stand models ([D-034](../DECISIONS.md#d-034-trees-and-rocks-are-obj-models-drawn-with-gpu-instancing), [D-036](../DECISIONS.md#d-036-multi-plant-stand-models-cut-the-scenery-instance-count)), and the water surface ([D-038](../DECISIONS.md#d-038-water-is-not-ground-planetsurfaceat-for-anything-above-it)).

---

## 1. Goal

Make the world look lived in. Add:

- a **road network** linking towns, villages and farms
- **villages**: a dozen to a few dozen buildings strung along a road round a church or green
- **towns**: denser streets, a square, bigger buildings and some industry
- **farmsteads**: one to four buildings at the end of a lane, inside the field patchwork

The look has to hold to the project's aesthetic ([D-013](../DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures)): flat-shaded, low-poly, palette colours, no textures. The original *Virus* sold its world with a few simple houses and trees. This aims for the same read at a much larger scale.

### Non-goals (for now)

- Traffic, pedestrians or anything else moving on the roads
- Night lighting (there is no day/night cycle)
- Building interiors, or buildings you can enter
- Hand-placed or named real places

---

## 2. Constraints from the existing code

Every phase has to respect these. Each links to its source.

| Constraint | What it means here |
| --- | --- |
| Toroidal world, unwrapped player ([D-007](../DECISIONS.md#d-007-unwrapped-player-coordinates-on-a-toroidal-world)) | Roads crossing the world edge must stay continuous. Store road polylines so that a segment can run past the edge, and draw and measure at the nearest copy ([AV-005](../ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap)). |
| Flat-shaded, palette colours ([D-010](../DECISIONS.md#d-010-32-colour-palette-via-obj-material-names), [D-013](../DECISIONS.md#d-013-flat-shaded-low-poly-aesthetic-no-textures)) | Buildings use `c00`–`c31` only. Roofs, walls and road surfaces have to come from the existing 32 colours, or a palette change goes through D-010. |
| OBJ models from meshgen ([D-009](../DECISIONS.md#d-009-obj-text-format-as-the-single-mesh-source-of-truth)) | Every building and prop is a `scripts/meshgen/` definition. Terrain stays procedural, so road *surfaces* are terrain colouring, not models. |
| Instanced scenery ([D-034](../DECISIONS.md#d-034-trees-and-rocks-are-obj-models-drawn-with-gpu-instancing), [D-036](../DECISIONS.md#d-036-multi-plant-stand-models-cut-the-scenery-instance-count)) | Buildings and props draw through the `Decorations` instancing path, one draw call per model. Thousands of buildings are fine; one draw call each is not. |
| Fixed pools, no hot-path allocation ([D-012](../DECISIONS.md#d-012-flat-type-tagged-entity-pools-no-ecs-no-hot-path-allocation)) | The entity pool holds 256. Buildings can't each be an `Entity`. Any gameplay has to work per settlement, or through a separate lightweight store. |
| Field layout is one BSP ([D-035](../DECISIONS.md#d-035-farmland-patchwork-from-an-angled-bsp-shared-by-terrain-and-decorations)) | D-035's reversal condition is "fields must follow roads". Roads cutting the patchwork means revisiting D-035 (phase R.3). |
| Water surface ([D-038](../DECISIONS.md#d-038-water-is-not-ground-planetsurfaceat-for-anything-above-it)) | Roads avoid water or cross on bridges. Bridge decks sit on `surfaceAt()`, not `heightAt()` ([AV-018](../ATTACK_VECTORS.md#av-018-seabed-height-used-for-something-above-the-water)). |
| Load time ([AV-014](../ATTACK_VECTORS.md#av-014-loading-progress-callback-lost-on-terrain-regeneration), [AV-017](../ATTACK_VECTORS.md#av-017-per-call-full-rebuild-of-scenery-instances)) | Road routing and settlement layout run at world load, behind the loading bar, with progress steps. Batch any decoration clears. |
| Bases and pads | Settlements keep clear of base platforms and relay towers, and must not sit on a pad's flattened ground. |
| Tuning in `Config.hpp` ([D-027](../DECISIONS.md#d-027-confighpp-as-the-single-source-of-tuning-truth-transitional)) | All sizes, counts and spacings are Config constants, marked `// TUNE` where playtest-driven. |

---

## 3. Design overview

### 3.1 Generation order

World load gains two steps, so it becomes:

1. Heightmap (existing)
2. **Settlement sites**: pick town, village and farmstead locations
3. **Road network**: route roads between sites over the terrain
4. Field map (existing, now seeded with the roads as boundaries)
5. Terrain chunks (existing, now also tinting road surfaces)
6. Decorations (existing, now also buildings and props, and clearing round them)
7. Bases, radar network and the player start (existing)

Settlements and roads come first because fields, scenery and the base layout all have to make room for them.

### 3.2 Settlement sites

A new `world/Settlements` module, owned by `Planet` like `FieldMap`:

- **Towns** (`TOWN_COUNT`, perhaps 3–5 per world): large, gentle grassland areas, ideally near water (a coast or lake), spaced far apart.
- **Villages** (`VILLAGE_COUNT`, perhaps 15–25): gentle grassland, spaced out, and preferably between towns so roads join them naturally.
- **Farmsteads** (many): inside farming country ([D-035](../DECISIONS.md#d-035-farmland-patchwork-from-an-angled-bsp-shared-by-terrain-and-decorations)), one per few fields, at the end of a lane.
- All sites keep a minimum distance from each other and from base sites, and none sit in water or on steep or high ground.

### 3.3 Road network

Three road classes:

| Class | Joins | Width | Surface |
| --- | --- | --- | --- |
| Main road | town to town, town to nearby village | ~7 m | dark asphalt |
| Lane | village to village, village to main road | ~4.5 m | lighter tarmac |
| Track | farmstead to lane, field access | ~3 m | gravel or dirt |

**Routing.** Join towns with a minimum spanning tree plus a few extra links so there are loops, then hang villages and farms off that. Each link is routed with A* over the heightmap grid:

- the cost rises steeply with slope and rises with height (so roads follow valleys)
- water is impassable unless a bridge is allowed (R.6)
- an existing road is cheaper, so roads merge and share
- the world wraps, so the grid search wraps too

Smooth the resulting grid path into a polyline (Chaikin or spline smoothing), then simplify it.

**Surface rendering.** Draw roads exactly the way field tints are drawn ([D-035](../DECISIONS.md#d-035-farmland-patchwork-from-an-angled-bsp-shared-by-terrain-and-decorations)): clip the terrain triangles along the road edges and colour the pieces inside the road. The road is then part of the terrain mesh, so it can't z-fight at any distance (unlike a decal or overlay; compare the hatch flicker in [BUG-029](../BUGS.md#bug-029-base-defenders-pop-in-above-the-pad-and-vanish-instead-of-taking-off-and-landing)). A centre line or verge can be a second, narrower clip.

**Grading.** Optionally, level the terrain across the road so it doesn't run along a side slope. This reuses the heightmap editing behind `Planet::flattenPad()`, with a gentle blend.

**Fields follow roads.** Feed road segments to the `FieldMap` BSP as forced first cuts, so roads become field boundaries the way real roads do. Hedges, walls and verges then line them for free. This is the D-035 revision.

### 3.4 Villages

A village is laid out along its roads:

- **Core**: a church or chapel and a green, or a pond, at a road junction.
- **Street frontage**: houses face the road on both sides, set back a few metres, with gardens behind and a short wall or hedge in front.
- **Fringe**: a few farmhouses, barns and outbuildings where the village thins into fields.
- **Size**: `VILLAGE_BUILDINGS_MIN..MAX`, perhaps 12–40 buildings.

### 3.5 Towns

A town is denser and more regular:

- **Centre**: a square or market place with a town hall, church and shops, surrounded by 2–3 storey terraces.
- **Streets**: a rough grid of short streets off the main roads, following the terrain.
- **Edges**: houses with gardens, then light industry: warehouses, a factory with a chimney, a water tower.
- **Coastal towns**: optionally a harbour quay and a lighthouse.
- **Size**: `TOWN_BUILDINGS_MIN..MAX`, perhaps 60–200 buildings.

### 3.6 Buildings in the engine

- **Drawing:** buildings are more instanced models in `Decorations`, each with a position, yaw (aligned to its road) and scale.
- **Level, not sheared:** buildings stay level, unlike groves. Each model has a foundation skirt reaching about 2 m below ground, so it sits cleanly on a slope. Building sites avoid slopes steeper than `BUILDING_MAX_SLOPE`.
- **Rows:** terrace and shop-row models are segments laid end to end along a street, like hedge segments.
- **Clearing:** settlement footprints and road corridors are kept free of forest and hedges in one batch ([AV-017](../ATTACK_VECTORS.md#av-017-per-call-full-rebuild-of-scenery-instances)).

### 3.7 Budget

| Item | Estimate |
| --- | --- |
| Buildings per world | ~1,500–3,000 (towns ~600, villages ~600, farms ~800) |
| Street props | ~3,000–6,000 (poles, lamps, signs, parked cars) |
| Triangles per building | 60–300 (cottage ~80, church ~250) |
| Visible at once (800 m radius) | perhaps 100–400 buildings, well within budget next to ~164k trees |

The real costs are load time (routing) and the extra terrain triangles from clipping road edges. Both should be measured in R.2 and R.3.

---

## 4. Gameplay (open questions)

Visuals come first; gameplay is a later, optional phase (R.8). The choices below change what that phase builds, so they need deciding:

1. **What are settlements for?** Options:
   - scenery only
   - defended population, where losing towns costs score or lives, like friendly structures
   - infection targets: seeders or landers turn settlements over, and the player wins them back, which fits *Virus*'s theme of a spreading blight
2. **Can buildings be destroyed?** By the player, by enemies, or both? Is there a penalty for shooting your own villages?
3. **Do roads matter to anything?** Collectors could drive on roads faster, enemy ground units could follow them, and roads could link bases.
4. **Bridges.** Should roads cross lakes and sea inlets on bridges, or always go round?
5. **Radar and map.** Should towns show on the radar or a map, and should they have generated names?

Within the pool limit (D-012), a settlement can be one lightweight record holding its health, its infection state and the range of its building instances. Damage swaps building instances to rubble or burnt models; no per-building `Entity` is needed.

---

## 5. Assets

Every model is a meshgen definition (`scripts/meshgen/`, probably a new `settlements.py`) in palette colours. Sizes are approximate. Triangle counts are targets. "Variants" means separate models, to cut down repetition.

### 5.1 Road surfaces and markings (terrain, not models)

| Asset | Description | Notes |
| --- | --- | --- |
| Main road surface | dark asphalt, ~7 m wide | Terrain clip tint (TerrainChunk colour, like field tints) |
| Lane surface | lighter tarmac, ~4.5 m | Terrain clip tint |
| Track surface | gravel or earth, ~3 m | Terrain clip tint |
| Centre line | broken pale line on main roads | Second, narrow clip; optional |
| Verge | thin darker-green strip either side | Optional clip |
| Village green, town square | open grass or paved area | Terrain tint polygon |

### 5.2 Road structures and furniture

| Model | Description | Size | Tris | Variants |
| --- | --- | --- | --- | --- |
| `bridge_stone` | arched stone road bridge segment, for rivers or lake necks | 8 m span | 150 | 1 |
| `bridge_girder` | steel girder bridge segment, for longer crossings | 12 m span | 120 | 1 |
| `bridge_pier` | stone or concrete pier for multi-span bridges | — | 40 | 1 |
| `telegraph_pole` | wooden pole with crossbar, lining lanes | 7 m | 20 | 1 |
| `street_lamp` | town lamp post | 5 m | 24 | 1 |
| `road_sign` | small post-mounted sign | 2 m | 12 | 2 |
| `signpost` | finger post at junctions | 2.5 m | 24 | 1 |
| `crash_barrier` | segment for main roads on embankments | 4 m | 30 | 1 |
| `bus_shelter` | small open shelter | 3 m | 40 | 1 |
| `phone_box` | red telephone kiosk | 2.5 m | 20 | 1 |

### 5.3 Rural and farm buildings

| Model | Description | Size | Tris | Variants |
| --- | --- | --- | --- | --- |
| `farmhouse` | two-storey stone or white farmhouse, pitched roof, chimney | 10×7 m | 90 | 2 |
| `barn` | large timber or stone barn | 16×9 m | 60 | 2 |
| `dutch_barn` | open-sided hay barn with curved roof | 14×8 m | 70 | 1 |
| `grain_silo` | cylindrical silo, domed top | 5 m dia, 12 m | 60 | 1 |
| `outbuilding` | small shed or stable | 6×4 m | 30 | 2 |
| `greenhouse` | glass-house frame | 8×4 m | 40 | 1 |
| `windmill` | tower mill with sails (static) | 14 m | 150 | 1 |
| `hay_bales` | stack or scatter of round bales | 4 m | 40 | 2 |
| `tractor` | parked tractor | 4 m | 60 | 1 |

### 5.4 Village buildings

| Model | Description | Size | Tris | Variants |
| --- | --- | --- | --- | --- |
| `cottage` | single-storey cottage, pitched roof, chimney | 8×6 m | 60 | 3 |
| `house` | two-storey detached house | 9×7 m | 80 | 3 |
| `terrace_row` | segment of three terraced houses, for street frontage | 15×7 m | 120 | 2 |
| `church` | stone church with tower or spire | 22×10 m, 20 m spire | 250 | 2 (tower, spire) |
| `chapel` | small chapel | 10×6 m | 70 | 1 |
| `pub` | larger two-storey inn | 14×8 m | 100 | 1 |
| `village_shop` | small shop with a front | 8×7 m | 70 | 1 |
| `village_hall` | single-storey hall | 14×8 m | 60 | 1 |
| `war_memorial` | cross or obelisk for the green | 4 m | 30 | 1 |
| `garden_wall` | low brick or stone wall segment (frontage) | 4 m | 40 | 2 |
| `picket_fence` | white garden-fence segment | 4 m | 40 | 1 |

### 5.5 Town buildings

| Model | Description | Size | Tris | Variants |
| --- | --- | --- | --- | --- |
| `townhouse` | three-storey terraced townhouse segment | 6×10 m | 80 | 3 |
| `shop_row` | segment of shops with flats above | 18×10 m | 150 | 2 |
| `town_hall` | civic building with clock tower | 24×16 m | 250 | 1 |
| `market_hall` | open arcaded market | 18×12 m | 150 | 1 |
| `apartment_block` | 4–6 storey flats | 20×12 m | 90 | 2 |
| `office_block` | glass-and-concrete block | 16×16 m | 70 | 2 |
| `warehouse` | low industrial shed | 30×18 m | 50 | 2 |
| `factory` | factory with a sawtooth roof | 36×22 m | 120 | 1 |
| `chimney` | tall factory chimney (stand-alone or attached) | 30 m | 40 | 1 |
| `water_tower` | elevated tank | 20 m | 70 | 1 |
| `petrol_station` | canopy and kiosk | 16×10 m | 90 | 1 |
| `parked_car` | small car (street and car-park props) | 4 m | 30 | 3 colours by tint, or 3 models |

### 5.6 Coastal (optional)

| Model | Description | Size | Tris | Variants |
| --- | --- | --- | --- | --- |
| `harbour_quay` | stone quay segment on the water line | 8 m | 60 | 1 |
| `lighthouse` | striped lighthouse | 18 m | 100 | 1 |
| `fishing_boat` | moored boat on the water surface | 7 m | 60 | 1 |

### 5.7 Damage states (only if buildings can be destroyed)

| Model | Description | Tris | Variants |
| --- | --- | --- | --- |
| `rubble_small` | low rubble pile for cottages and houses | 40 | 2 |
| `rubble_large` | larger pile for big buildings | 60 | 2 |
| `burnt_shell` | roofless gutted walls (house footprint) | 60 | 2 |
| Smoke and fire | burning buildings | — | Particles, not models (D-009) |

### 5.8 Palette

The existing 32 colours ([D-010](../DECISIONS.md#d-010-32-colour-palette-via-obj-material-names)) already cover a lot:

| Use | Palette entries |
| --- | --- |
| Roof tiles | `c27` terracotta (160,70,60), `c16` red (200,50,50) |
| Slate roofs | `c21` / `c22` navy (70,80,110 / 40,50,80) |
| White render | `c15` (200,200,200), `c01` (white) |
| Stone | `c26` (110,100,90), `c08` (128,128,128) |
| Brick, timber | `c09` tan (192,128,64), `c14` brown (128,64,0) |
| Windows | `c10` / `c23` blues |
| Doors and trim | `c00`, `c17` yellow, `c13` dark green |

Missing: a warmer red brick and a mid-brown thatch would help most. Adding them means changing the palette, which D-010 governs. Decide in R.4.

Road surface colours are terrain colours in `TerrainChunk`, like the field tints, so they need no palette slots.

---

## 6. Phases

Each phase ends playable, and gets its docs (FEATURES, DECISIONS, CHANGELOG) in the same commit as its code. Effort: S = a session, M = a few sessions, L = a week or more of sessions.

### R.1 — Settlement sites (S)

**Goal:** choose and visualise town, village and farmstead sites.
**Deliverables:** a `world/Settlements` module with site selection and Config constants. A DEV overlay or a top-down debug map (like the field preview tool) marks the sites.
**Acceptance:** sites are deterministic per seed, sensibly spaced, and on gentle dry grassland clear of bases.
**Assets:** none.

### R.2 — Road network and surfaces (L)

**Goal:** roads visible on the terrain, joining every settlement.
**Deliverables:**
- A* routing over the heightmap with slope, height and water costs
- path smoothing and simplification, with polylines stored across the world wrap
- terrain clipping and tinting for the three road classes (`TerrainChunk`, reusing the field clipping)
- routing runs behind the loading bar, with progress steps
**Acceptance:**
- roads form one connected network, avoid water and steep ground, and wrap seamlessly
- no z-fighting at any distance
- load time stays reasonable; record the measured figure here
**Assets:** road surface colours (5.1).
**Risks:** routing cost on a 1024² grid (keep it bounded and cached); the extra terrain triangles from clipping.

### R.3 — Fields and scenery follow roads (M)

**Goal:** roads become field boundaries and corridors through forest.
**Deliverables:**
- road segments go into the `FieldMap` BSP as forced cuts
- hedges, walls or verges line roads through farmland
- forest, grove and hedge placement keeps clear of road corridors
- the D-035 revision recorded as a new decision
**Acceptance:** no tree, hedge or wall stands on a road; roads read as field edges.
**Assets:** reuses the hedge, wall and fence segments; optionally `telegraph_pole`.

### R.4 — First buildings: rural and village set (M)

**Goal:** the first building models.
**Deliverables:**
- `scripts/meshgen/settlements.py` with the farm (5.3) and village (5.4) models, foundation skirts and preview renders
- a palette decision (5.8)
**Acceptance:** models render correctly in the inspector and contact sheets, within their triangle targets.
**Assets:** 5.3, 5.4.

### R.5 — Villages and farmsteads placed (M)

**Goal:** villages and farms appear in the world.
**Deliverables:**
- village layout (core, frontage along roads, fringe) and farmstead placement, drawn as instanced decorations aligned to roads, with footprints cleared in one batch
- garden walls and fences on frontages
- farm lanes (track class) joined to farmsteads
**Acceptance:**
- villages look like villages: buildings face their road and nothing overlaps
- the frame rate is unchanged in playtesting
**Assets:** 5.3, 5.4, plus `garden_wall` and `picket_fence`.

### R.6 — Bridges (M, optional)

**Goal:** roads cross lake necks and inlets.
**Deliverables:** A* allows short water crossings at a cost, and places bridge spans and piers on `surfaceAt()`.
**Acceptance:** bridges meet the road at both ends, and their decks sit at a sensible height above the water.
**Assets:** `bridge_stone`, `bridge_girder`, `bridge_pier`.

### R.7 — Towns (L)

**Goal:** towns with streets, a centre and industry.
**Deliverables:**
- town street layout (centre square, a rough grid off the main roads)
- the town model set (5.5), with terraces and shop rows as street segments
- street furniture (5.2) and parked cars
- optionally, coastal props (5.6)
**Acceptance:** towns read as towns from the air, the frame rate holds, and load time is recorded.
**Assets:** 5.2, 5.5, optionally 5.6.

### R.8 — Settlement gameplay (L, depends on section 4)

**Goal:** settlements matter in play.
**Deliverables:** whatever section 4 decides, for example:
- a lightweight settlement record (health, infection state, range of building instances)
- building damage swapping to rubble or burnt shells, plus smoke
- radar or map markers, and score or penalty hooks
**Acceptance:** defined once section 4 is settled.
**Assets:** 5.7.

---

## 7. Risks

| Risk | Mitigation |
| --- | --- |
| Road routing makes the load slow | Bound the search; route on a coarser grid (e.g. 16 m) then refine; time it and report progress ([AV-014](../ATTACK_VECTORS.md#av-014-loading-progress-callback-lost-on-terrain-regeneration)) |
| Roads or buildings broken at the world edge | Unwrapped polylines; nearest-copy drawing ([AV-005](../ATTACK_VECTORS.md#av-005-world-fixed-objects-drawn-or-measured-without-the-toroidal-wrap)); test with a seed whose town sits across the edge |
| Z-fighting road surfaces | Draw roads as clipped terrain, never as overlays |
| Buildings floating or buried on slopes | Foundation skirts; a slope limit on building sites; optional ground levelling under towns |
| Too much repetition | Several variants per class, random yaw within road alignment, mixed palettes |
| Field layout regressions | R.3 changes D-035; re-run the field preview tools and the outline check from the patchwork work |
| Scope creep into gameplay | Keep R.1–R.7 visual-only; R.8 starts only once section 4 is decided |

---

## 8. Documentation impact (when work starts)

- **FEATURES:** new entries, probably one per phase group: road network (R.2–R.3), villages and farmsteads (R.4–R.5), bridges (R.6), towns (R.7) and settlement gameplay (R.8). They take the next free F- IDs.
- **DECISIONS:** new entries for:
  - settlement and road generation order and routing (R.2)
  - the D-035 revision so fields follow roads (R.3)
  - any palette change (R.4)
  - settlement gameplay (R.8)
- **ROADMAP:** add a Settlements track row linking this file.
- **ARCHITECTURE:** a `world/Settlements` row, and the TerrainChunk and FieldMap changes.
- **ATTACK_VECTORS:** road wrap and load-time entries if new failure modes turn up.
