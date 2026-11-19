# meshgen

Python definitions for the ship, structure and scenery meshes in `assets/meshes/`. Blender runs them headless and writes OBJ files in the game's format: palette materials `c00`–`c31`, faces wound outward, frame +X right, +Y up, +Z nose. See `project-status/3d_assets.md` for the format.

| File | Contents |
| --- | --- |
| `shiplib.py` | `Ship` builder and part helpers (`loft`, `block`, `stack`, `strut`, `spike`), the OBJ writer and the preview renderer |
| `player.py` | `hovercraft` |
| `enemies.py` | `fighter`, `drone`, `bomber`, `seeder`, `carrier`, `tank` |
| `structures.py` | `base` (control tower), `base_turret`, `collector`, `radar_booster`, `lander`. These leave room for the parts the game draws in code: the Collector cab and booster dish |
| `scenery.py` | `tree_conifer`, `tree_broadleaf`, `rock_a`, `rock_b`, `rock_c`, `antenna`, `crash_site`, `crash_debris`. Drawn instanced by `Decorations`, so keep them small |
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
