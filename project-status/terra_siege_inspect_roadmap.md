# terra-siege-inspect — Roadmap

A focused OBJ editor for authoring and tweaking terra-siege entity assets. Not a general-purpose modeller; not a Blender replacement. Its job is to make the loop "tweak geometry / metadata → save → re-launch game" tight enough that small visual changes don't require a full Blender round-trip.

This doc lists the features we'd want and groups them into phases. Each phase is roughly independent — pick whichever has the highest payoff next.

---

## Design principles

- **Source-of-truth stays in the OBJ.** Geometry edits modify only `v` lines; everything else (comments, materials, faces, normals, ordering) survives byte-exact. The Blender round-trip must never break.
- **Terra-siege entity behaviour lives in a sidecar file** (see [Phase F](#phase-f--terra-siege-entity-sidecar)). The OBJ format can't carry forward-axis, hardpoints, hull HP, weapons, AI profile, etc. — those go in a parallel `*.meta.json` so each OBJ file stays valid on its own.
- **Sidecar by default, registry override later.** Each mesh has its own `*.meta.json` carrying its entity profile, one-to-one. A registry file (one JSON pointing at multiple meshes with per-faction overrides) lets one mesh serve multiple entity types — but we add that only when we need it. Start one-to-one.
- **Tools slot into the existing `Tool` registry.** Most features below are a new `Tool` subclass plus one `m_tools.push_back(...)` in `Inspector::Inspector()`. File ops, asset browser, and view modes are Inspector-global, not tools.
- **Simple over feature-rich.** Each feature should be the minimum useful version. We're not building Maya — we're building "Blender's geometry pane, with terra-siege entity authoring bolted on".
- **No external GUI library yet.** Plain raylib `DrawText` / `DrawRectangle` panels are enough for now. ImGui is on the table later if the UI gets dense, but every feature below should be reachable without it.

Scope tags below: **S** ≈ one evening, **M** ≈ one to two days, **L** ≈ multi-day.

---

## Phase A — Workspace & file ops *(immediate next)*

The inspector should boot to an empty workspace and let the user open / close / save files from inside it, rather than requiring an OBJ path on the CLI.

| Feature | Scope | Notes |
|--|--|--|
| Boot with no OBJ | S | Show an empty 3D scene with a "no file loaded" overlay and a hint to press `O` (open) |
| `O` — Open file | S | Either a typed path prompt or a minimal scrollable list of `assets/meshes/*.obj` |
| `Ctrl+S` — Save | S | Existing path; warn if nothing dirty (no-op already correct) |
| `Ctrl+Shift+S` — Save As | M | New path, copies the source file then rewrites `v` lines (preserves comments/materials in the destination) |
| `Ctrl+W` — Close current file | S | Prompts on unsaved changes; returns to empty workspace |
| Drag-and-drop OBJ onto window | S | raylib has `IsFileDropped()` / `LoadDroppedFiles()` — easy |
| Unsaved-changes prompt | S | Modal overlay: "Save / Discard / Cancel" on Close, Open-new, or Quit |
| Recent files list | S | Top-5 paths persisted to `~/.config/terra-siege/inspector.cfg` |
| Status bar | S | Bottom line: full path, vertex count, dirty state, active tool — replaces the current scattered HUD lines |
| CLI path arg still works | — | Existing `terra-siege-inspect <path>` keeps working; it just becomes a shortcut for "boot and immediately open this file" |

**Acceptance:** launch the inspector with no args, press `O`, pick `hovercraft.obj`, edit a vertex, `Ctrl+S`, `Ctrl+W`, repeat with a different file — all without restarting.

---

## Phase B — Vertex editing depth

The current `VertexTool` does single-vertex drag with X/Y/Z axis lock. Enough features to actually shape geometry.

| Feature | Scope | Notes |
|--|--|--|
| Multi-select | M | Shift-click to add, click-empty to clear, box-select with Ctrl+LMB drag |
| Translate selection | S | Move all selected verts by the drag delta (not just one) |
| Numeric input | S | When a vertex is selected, show three editable text fields (X/Y/Z); typing a value snaps it |
| Undo / redo | M | Ring buffer of pre-edit vertex snapshots; `Ctrl+Z` / `Ctrl+Y`. R-to-reload-from-disk remains for hard reset |
| Mirror across X / Y / Z plane | M | Massive win for symmetrical ships: edit one wing, mirror to the other. Needs a "symmetry mode" toggle that keeps mirrored pairs synced |
| Snap to grid | S | Configurable step (0.1, 0.25, 0.5, 1.0); hold a modifier to suspend |
| Snap to nearest vertex | S | While dragging, if cursor is near another vertex (in screen space), snap target to that vertex's position |
| Vertex weld (merge nearby) | M | Tool that finds vertices within ε of each other and collapses them. Critical when stitching duplicated halves |
| Vertex split | M | Inverse of weld — duplicate a vertex so the two faces using it can move independently |
| Stats overlay | S | Vert / face / palette-colour counts in the corner |

**Acceptance:** open `fighter.obj`, multi-select the wingtip cluster, type new coords for one, watch the symmetry mirror update the other side, undo, redo, save.

---

## Phase C — Materials & palette

OBJ palette colours come from material names (`c00`..`c31`). Right now you can't reassign them in the inspector — only in a text editor. Fix that.

| Feature | Scope | Notes |
|--|--|--|
| Palette swatch panel | S | A vertical strip of 32 colour squares on the right edge of the screen, labelled with material name |
| Face pick | M | Click a face in 3D → highlight it, show its current material |
| Assign material to selected face | S | With a face selected, click a palette square to reassign. Writes a new `usemtl cXX` group on save |
| Bulk reassign | S | Multi-select faces (shift-click), assign-all |
| Eyedropper | S | Hover-over-face mode: shows that face's material in the HUD |
| Palette config | M | Read `assets/meshes/palette.txt` (or wherever it lives) so the swatch reflects the canonical 32 colours, not hardcoded |
| Unused-colour indicator | S | Dim out palette squares no face in the current mesh references |

**Round-trip impact:** material assignment changes mean the loader/saver must do more than just rewrite `v` lines. Add a second save path: `saveObjMaterials()` that rewrites the `usemtl` lines and surrounding `f` groups. Keep the no-edit fast path (T-06) untouched.

---

## Phase D — Topology editing

Right now you can only move existing vertices. To actually build assets you need to add and remove geometry.

| Feature | Scope | Notes |
|--|--|--|
| Add vertex | S | Click in empty space → adds a vertex at the click-ray projected onto a chosen plane (XZ by default) |
| Delete vertex | S | `Del` on selected; removes the vertex AND every face using it |
| Add face from selection | S | Select 3 or 4 verts (in order), `F` to add a face; CCW winding by default |
| Delete face | S | Face pick + `Del` |
| Flip face winding | S | `Shift+F` on selected face — fixes inverted normals |
| Auto-flip non-CCW faces | M | One-shot button: detect faces whose normal points away from the mesh centroid, offer to flip them all |
| Subdivide face | M | Splits a triangle into 4 by midpoint, or a quad into 4 by edge-midpoints |
| Extrude face | M | Duplicates a face, links the originals with side faces. Essential for building ship hulls |
| Bridge two edges | L | Connects two open edges with a new quad strip. Nice-to-have, not essential |
| Recompute normals on save | S | Already done at load; add an explicit "normalize now" button for sanity-checking |

**Acceptance:** open an empty file, drop 8 verts in cube positions, build 12 faces, assign palette colours, save — get a valid cube OBJ that the game loader accepts.

---

## Phase E — Primitive insertion

If we want to actually *create* assets in the inspector (not just edit ones authored in Blender), primitives save hours.

| Feature | Scope | Notes |
|--|--|--|
| Insert cube | S | Configurable size; pivoted at origin or cursor |
| Insert cylinder | S | Configurable radius, height, segments |
| Insert cone | S | Configurable radius, height, segments |
| Insert sphere | M | Icosphere subdivisions (not UV-sphere — keeps it flat-shaded-friendly) |
| Insert plane / quad | S | Two-triangle quad for floors, panels |
| Triangulate quads | S | Loader expects triangles; primitive insertion may emit quads — one-button convert |
| Merge into current mesh | S | "Insert" really means "append to the current vertex/face lists" — no separate object concept |

**Out of scope:** boolean ops, lofting, lathe, NURBS, anything procedural that Blender does better.

---

## Phase F — Terra-siege entity sidecar

The OBJ format can't carry the data the game needs beyond geometry. A sidecar JSON file per mesh holds the entity profile — identity, hull, shields, weapons, hardpoints, AI, FX, resources. Once a mesh has a sidecar, the game stops hardcoding "if type == Fighter, use these constants" and reads the profile.

**Filename convention:** `assets/meshes/foo.obj` → `assets/meshes/foo.meta.json`. Loader looks for the sidecar; absence is fine (defaults apply).

**Schema sketch** — incremental, each sub-phase adds a section:

```json
{
  "identity":   { "class": "ship-flyer", "displayName": "Fighter", "faction": "enemy" },
  "hull":       { "hp": 100, "collisionRadius": 1.4, "mass": 1.0,
                  "wreckage": { "metal": 50 } },
  "shields":    { "model": "omni", "hp": 40, "regen": 8, "delay": 3 },
  "weapons":    [
    { "name": "main-cannon", "type": "cannon",
      "fireRate": 0.20, "damage": 8, "projSpeed": 200, "range": 200 }
  ],
  "hardpoints": [
    { "name": "nose", "pos": [0, 0, 1.2], "dir": [0, 0, 1],
      "weapon": "main-cannon", "fireArcDeg": 6 }
  ],
  "ai":         { "profile": "pursue-attack-evade",
                  "detectionRange": 350, "attackRange": 180,
                  "evadeAtHPFrac": 0.60, "retreatAtHPFrac": 0.40,
                  "targetPref": "player" },
  "infection":  { "canBeInfected": true, "rebootDuration": 3.0,
                  "speedPenaltyAfter": 0.80 },
  "fx":         { "smokeAtHPFrac": 0.50,
                  "engineEmitters": [ {"name":"main","pos":[0,0,-1.2],"dir":[0,0,-1]} ],
                  "deathExplosionScale": 1.0 },
  "resources":  { "yieldOnDeath": { "metal": 0, "bio": 0 },
                  "capacity": null, "production": [] },
  "forward":    [0, 0, 1],
  "scale":      1.0,
  "pivot":      [0, 0, 0]
}
```

Sections omitted from a file = "this entity doesn't have that". An asteroid skips `weapons` / `ai` / `infection`. A base skips nothing.

### F.1 — Sidecar foundation

| Feature | Scope | Notes |
|--|--|--|
| Sidecar load on open | M | If `*.meta.json` exists next to the OBJ, load and validate. Unknown keys warn but don't refuse |
| Sidecar save when dirty | S | Each F.* tool sets its own dirty flag; Inspector's `S` hotkey flushes them all through one writer |
| Schema validator | M | JSON parser + range checks; clear error message on bad data, with line numbers if possible |
| Viewer overlay (read-only) | S | Even before tools edit, display whatever the sidecar already contains — forward arrow, hardpoint icons, fire arcs, smoke threshold, AI ranges. Sanity check + makes the data legible |
| `forward` / `scale` / `pivot` mini-tool | S | These three are too small to deserve their own tool; folded into F.1 with simple form fields |

### F.2 — Identity, hull, shields *(unblocks Slice B migration)*

| Feature | Scope | Notes |
|--|--|--|
| `identity` panel | S | Asset class dropdown (`static` / `ground` / `hover` / `flyer` / `projectile`), display name, default faction. No 3D viz |
| `hull` form | S | HP, collision radius, mass, wreckage yield. Collision overlay rendered as translucent wireframe |
| `shields` form | M | Model picker (`omni` / `4-sector` / `per-face`), HP per sector, regen rate, regen delay. Translucent shield viz: sphere for omni, pies for 4-sector, highlighted faces for per-face |

**Game-side impact:** `MeshRegistry` grows a parallel `EntityProfileRegistry`. Entity spawn reads HP / shield / etc. from the profile instead of `Config::FIGHTER_HP`. One-time migration per entity type — once a sidecar exists, its old Config constants get removed.

### F.3 — Weapons + hardpoints *(unblocks Slice B authoring)*

| Feature | Scope | Notes |
|--|--|--|
| `weapons` list | S | Named weapon stat blocks (type, fire rate, damage, projectile speed, range, ammo, cooldown group). Form panel, no 3D viz. Defined once per entity, referenced by hardpoints |
| `hardpoints` tool | M | Add / rename / delete / position named mount points. Each renders as a 3D icon with a direction arrow. Each hardpoint references a weapon by name — multiple hardpoints can share one weapon |
| Fire arc visualizer | S | Translucent cone overlay at each hardpoint showing its fire arc angle |

Subsumes the original draft's standalone `hardpoints` / `forward` / `emitters` tools — weapons and hardpoints pair, emitters move to F.5.

### F.4 — AI profile *(needed when infection lands in Slice B and bases land in Slice C)*

| Feature | Scope | Notes |
|--|--|--|
| Profile picker | S | Dropdown: `pursue-attack-evade` / `kamikaze` / `strafe-friendlies` / `boids-swarm` / `stationary-turret` / `none`. The existing C++ AI dispatch becomes a switch on this string |
| Range fields | S | Detection range, attack range, evade-at-HP-frac, retreat-at-HP-frac |
| Range visualisation | S | Concentric wireframe circles around the mesh at the configured ranges |
| Target preference | S | `player` / `friendlies` / `bases` / `nearest` |
| Infection block | S | `canBeInfected` toggle, reboot duration, speed penalty after — small fields, fits the same tool panel |

### F.5 — FX *(F.5.1 unblocks Slice A; rest can wait)*

| Feature | Scope | Notes |
|--|--|--|
| **F.5.1** smoke threshold | S | Slider, 0..1 of max HP. Below this fraction the entity emits damage smoke. Live preview in 3D — **Slice A reads this field even before the tool exists** (just edit the JSON by hand) |
| Engine emitters | M | Same UI as hardpoints but for particle origins. Game maps named emitters to particle systems |
| Engine glow colour | S | Colour picker, optional override of faction default |
| Death explosion scale | S | Float, multiplies the base explosion radius |

### F.6 — Resources *(Slice C only)*

| Feature | Scope | Notes |
|--|--|--|
| Yield on death | S | Metal + Bio amounts |
| Resource capacity | S | Bases only — Metal cap, Bio cap |
| Production array | M | List of `{ unit, period, metalCost, bioCost }`. Hot table editor: add row, pick unit type, set timings |

---

## Phase G — Workflow polish

Once the core editing loop is solid, these are the productivity boosters.

| Feature | Scope | Notes |
|--|--|--|
| View modes | S | Wireframe / shaded / both. `1`/`2`/`3` to cycle. |
| Orthographic views | M | Top / Front / Side — useful for precise alignment. `5` to toggle perspective ↔ ortho, `7`/`1`/`3` for top/front/side (Blender-style numpad) |
| Show normals as arrows | S | Toggle that draws a short line out of each face along its normal — instant visual check for inverted faces |
| Show vertex indices | S | Tiny number labels at each vertex. Off by default (clutters); on while debugging |
| Show bounding box / sphere | S | Visualizes the collision shape the game uses (or would use) |
| Configurable grid | S | Scale + snap step in the status bar; `+` / `-` to halve / double |
| Backface culling toggle | S | Useful when authoring open meshes |
| Asset browser panel | M | Strip down the left edge: thumbnails (or just names) of every `assets/meshes/*.obj`, click to switch. Handy for cross-referencing while authoring |
| Live file watch | M | Detect external Blender re-exports of the current file, prompt to reload (or auto-reload if clean) |
| In-game hotswap | L | A "preview in game" button: launches `terra-siege` with an env var pointing to the current edits, sees the mesh on a target practice dummy. Round-trip in seconds, not minutes |
| Diff vs git HEAD | M | Show which vertices moved since the last commit. Heatmap colouring on the mesh |
| Reference mesh overlay | S | Load a second OBJ as a faded outline behind the current one — for "match this silhouette" work |
| Screenshot | S | `F12` saves a PNG of the current view, useful for PR diffs |

---

## Out of scope (probably forever)

These would be useful in a general modeller but are not worth building here:

- UV mapping / texture coords (game is flat-shaded — no textures)
- Skeletal rigging or skinning (terra-siege has no skinned meshes)
- Animation keyframes (turret rotation etc. is handled procedurally in-game from a `dir` vector, not baked animation)
- Boolean operations, NURBS, sculpting
- Multiple objects per file (one `o` directive per OBJ — game expects this)
- Importing GLB / FBX / STL (OBJ-only by design, see [3d_assets.md](3d_assets.md))
- Lighting / rendering preview beyond flat-shaded directional sun

---

## Implementation seams

Most features above slot into one of these places:

- **New tool in `src/inspector/`** — implements `Tool`, registered in `Inspector::Inspector()`. Covers: vertex multi-select, mirror, material assign, primitive insertion, and every F.* entity-profile tool (identity, hull, shields, weapons, hardpoints, AI, FX, resources).
- **Inspector-global** — file menu, drag-and-drop, asset browser, view modes, undo stack, status bar. Lives in `Inspector` directly.
- **Mesh subsystem (`src/mesh/`)** — sidecar load/save, schema validator, palette config loader, `saveObjMaterials()`. Used by both game and inspector.
- **Game-side reader** — `EntityProfileRegistry` for sidecar entity profiles; consumers across `entity/`, `weapon/`, `ai/`, `shield/` for the constants they used to read from `Config.hpp`.

---

## Open questions

- **Where does the palette config live?** Hardcoded constexpr table, or a `palette.txt` next to the meshes? The latter is more editable but adds a file to keep in sync.
- **Undo across save?** Common to wipe the undo stack on save. Acceptable for now.
- **Is "in-game hotswap" worth the engineering?** Reloading the `MeshRegistry` and `EntityProfileRegistry` at runtime needs care (GPU model lifecycle, in-flight entities holding old pointers). Skip until everything else is shipped.
- **Registry-file override** — once Slice C lands and we might want one mesh to serve both an enemy and a captured-friendly variant, we add a registry-file format (one JSON pointing at multiple meshes with per-faction stat overrides). Not now.

---

## Suggested next step

**Phase A** is the user-blocking item and is mostly small tasks — should be one or two sessions of focused work. Suggested order within Phase A:

1. Boot-without-OBJ + empty-workspace overlay
2. `O` (open) with a path prompt
3. Drag-and-drop file load
4. `Ctrl+S` works as today (no change needed beyond rebinding from bare `S`)
5. `Ctrl+Shift+S` (save-as)
6. `Ctrl+W` (close) + unsaved-changes prompt
7. Status bar replaces scattered HUD lines
8. Recent files list

After Phase A, **Phase F.1** (sidecar foundation — read-only viewer overlay of the JSON, plus the `forward` / `scale` / `pivot` mini-fields) is the next highest-leverage step. It unblocks the game-side `EntityProfileRegistry` migration that Slice B depends on. Phase E (primitives) and F.6 (resources) land closer to Slice C, when base / radar / builder meshes need authoring.

See [ROADMAP.md](ROADMAP.md) for how this tooling timeline interleaves with the engine and feature tracks.
