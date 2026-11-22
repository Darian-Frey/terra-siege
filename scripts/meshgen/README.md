# meshgen

Python definitions for the ship, structure and scenery meshes in `assets/meshes/`. Blender runs them headless and writes OBJ files in the game's format: palette materials `c00`–`c31`, faces wound outward, frame +X right, +Y up, +Z nose. See `project-status/3d_assets.md` for the format.

| File | Contents |
| --- | --- |
| `shiplib.py` | `Ship` builder and part helpers (`loft`, `block`, `stack`, `strut`, `spike`), the OBJ writer and the preview renderer |
| `player.py` | `hovercraft`, `defender` |
| `enemies.py` | `fighter`, `drone`, `bomber`, `seeder`, `carrier`, `tank` |
| `structures.py` | `base` (control tower), `base_platform` and `base_ramp` (bake Config pad and ramp sizes: change both and regenerate), `base_turret` (housing) and `base_turret_guns` (elevating guns), `collector` with `collector_cab_empty` / `collector_cab_loaded`, `radar_booster` with its spinning `radar_booster_dish`, `lander` |
| `settlements.py` | farm set (`farmhouse_a`/`_b`, `barn_a`/`_b`, `dutch_barn`, `grain_silo`, `outbuilding_a`/`_b`, `greenhouse`, `windmill`, `hay_bales_a`/`_b`, `tractor`), village set (`cottage_a`–`_c`, `house_a`–`_c`, `terrace_row_a`/`_b`, `church_a`/`_b`, `chapel`, `pub`, `village_shop`, `village_hall`, `war_memorial`, `garden_wall_a`/`_b`, `picket_fence`) and town set (`townhouse_a`–`_c`, `shop_row_a`/`_b`, `town_hall`, `market_hall`, `apartment_block_a`/`_b`, `office_block_a`/`_b`, `warehouse_a`/`_b`, `factory`, `chimney`, `water_tower`, `petrol_station`, `parked_car_a`–`_c`, `street_lamp`, `telegraph_pole`, `road_sign`, `signpost`, `bus_shelter`, `phone_box`) and bridge parts (`bridge_stone`, `bridge_girder`, `bridge_pier`; spans 8 m and 12 m along +Z, pier unit height). Front faces +Z, walls to 2 m below ground, window and door panels 0.12 m proud. Footprints are mirrored in `SettlementLayout.cpp` |
| `scenery.py` | `tree_conifer`, `tree_broadleaf`, `tree_poplar`, `tree_birch`, `bush`, `tree_dead`, `grove_conifer_a`/`_b`, `grove_broadleaf_a`/`_b`, `grove_birch`, `grove_mixed`, `copse_conifer`, `copse_broadleaf`, `copse_birch`, `copse_mixed`, `hedge_a`, `hedge_b`, `hedge_tree`, `hedge_birch`, `wall_a`, `wall_b`, `fence_a`, `fence_b`, `rock_a`, `rock_b`, `rock_c`, `antenna`, `crash_site`, `crash_debris`. Drawn instanced by `Decorations`, so keep them small |
| `projectiles.py` | `bolt` (lasers, cannon tracers), `orb` (plasma), `depth_charge`. Bolt and orb are white/light grey because the game tints them per shot |
| `build.py` | Entry point |

## Usage

Run these from the repo root. They need Blender 4.x.

```bash
blender -b --factory-startup -P scripts/meshgen/build.py                      # regenerate every model in assets/meshes/
blender -b --factory-startup -P scripts/meshgen/build.py -- tank fighter      # regenerate only these models
blender -b --factory-startup -P scripts/meshgen/build.py -- --out /tmp/m --render /tmp/m   # write OBJs and preview PNGs elsewhere
```

## Authoring notes

- **Coordinates.** Definitions use x right, y forward (nose), z up. The writer converts them to the game frame.
- **Mirroring.** `sym(x, y, z)` adds a mirrored vertex pair, and `tri_sym` / `quad_sym` add the matching mirrored faces.
- **Face winding.** `face()` flips each face so it points away from a reference point. That point is the ship's `ref` by default, or the part's own centre in the helpers. Pass `ref=None` to keep the winding you gave. Use `twoside()` for thin fins, because enemies are drawn with backface culling on.
- **Zero-area triangles** are dropped on write, because the game's loader would skip them and warn.
- **Hand edits get overwritten.** These OBJ files are generated. If one is edited in `terra-siege-inspect`, regenerating overwrites that edit, so make the change in the definition here as well.
- **Size and sidecars.** Keep each model's footprint close to its `*.meta.json` `collisionRadius`.
