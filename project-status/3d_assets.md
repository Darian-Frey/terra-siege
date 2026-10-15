# 3D Asset Pipeline (terra-siege)

> **Status:** Active
> **Provenance:** Claude (initial draft, awaiting hub-and-spoke synthesis review)
> **Last reviewed:** 2026-05-20
> **Why this status:** Pipeline spec for terra-siege mesh authoring and in-engine inspection. No code yet written against this spec; D-IDs herein are provisional and should be renumbered on integration with terra-siege's main `DECISIONS.md`.

---

## 1. Purpose

Defines how 3D mesh assets are authored, stored, loaded, and edited for terra-siege. Establishes OBJ as the single source of truth, specifies a palette-indexed colour convention compatible with the original *Virus* (1988) flat-shaded aesthetic, and scopes a minimal in-engine mesh inspector for vertex-level edits.

This document is the contract Claude Code should implement against. Anything outside its scope is explicitly out of scope (see §11 Anti-goals).

---

## 2. Pipeline overview

```
                       authoring
       ┌─────────┐    ┌────────────────┐    runtime
       │ Blender │ ─→ │ assets/meshes/ │ ──────────┐
       └─────────┘    │   *.obj        │           │
            ↑         └────────────────┘           ↓
            │                  ↑              ┌──────────┐
            │                  │              │ raylib   │
            │                  │              │ LoadModel│
            │            edit in place        └──────────┘
            │                  │
            │         ┌─────────────────┐
            └─────────│ in-engine       │
                      │ mesh inspector  │
                      │ (--inspect mode)│
                      └─────────────────┘
```

Two authoring entry points (Blender, inspector), one storage format (OBJ), one runtime loader (raylib). No conversion step, no asset cache, no build-time codegen.

---

## 3. Single source of truth: OBJ

### 3.1 Why OBJ

- Text format → git diffs cleanly; code review of mesh changes is possible
- Native Blender export, no plugin required
- Native raylib load via `LoadModel`
- Tool-agnostic survival: if Blender or raylib disappear, OBJ files remain readable
- Trivially parseable; the inspector can implement read+write in ~200 lines of C++17

### 3.2 What we use from OBJ

- `v x y z` — vertex positions
- `f a b c` (and `f a b c d`) — face indices (triangles and quads)
- `o name` — object name (informational)
- `g name` — group name (used for sub-mesh separation, e.g. hull vs cockpit)
- `usemtl name` — material reference, interpreted as palette index (§6)

### 3.3 What we ignore from OBJ

- `vn` — vertex normals. **Always recompute per-face at load time.** Guarantees faceted appearance regardless of Blender's export state.
- `vt` — texture coordinates. terra-siege is untextured.
- MTL sidecar file — see §6.

### 3.4 What we do not emit

The pipeline is read-mostly for non-position data. Inspector saves preserve everything except vertex positions verbatim (§8.3).

---

## 4. Directory layout

```
terra-siege/
├── assets/
│   └── meshes/
│       ├── hovercraft.obj
│       ├── enemy_drone.obj
│       ├── building_silo.obj
│       └── ...
├── src/
│   └── mesh/
│       ├── obj_loader.h
│       ├── obj_loader.cpp     # OBJ → Mesh3D with recomputed normals
│       ├── palette.h          # static Color kPalette[]
│       └── inspector.cpp      # --inspect mode (see §8)
├── tests/
│   └── fixtures/
│       └── meshes/            # test inputs for T-01..T-08
└── ...
```

Meshes live directly under `assets/meshes/` with no subdirectory hierarchy until count exceeds ~30 files. Filenames are lowercase, snake_case, `.obj`.

---

## 5. Blender authoring guidelines

### 5.1 Setup

- Blender 4.x (Linux build, matches dev environment)
- Units: metres (raylib world space is metres given the 1025×1025 terrain scale)
- Default orientation on export: forward `-Z`, up `Y`, to match raylib's coordinate convention

### 5.2 Authoring rules

1. **Low-poly only.** Reference: hovercraft ≈ 30 tris, buildings ≈ 10–20 tris, enemies ≈ 15–40 tris. If a mesh exceeds 200 triangles, stop and reconsider.
2. **Triangulate before export** (Mesh → Triangulate, or use the OBJ exporter's option). The loader supports quads as a fallback but triangles are canonical.
3. **No UVs, no textures, no rigs, no modifiers.** Apply any modifiers before export. Strip unused data.
4. **One object per file.** Multiple `o` blocks per file are not forbidden but complicate the inspector; prefer one mesh per OBJ.
5. **Use materials only for palette indexing** (§6).

### 5.3 Export settings

`File → Export → Wavefront (.obj)`:

- ✅ Selection Only (when exporting a single mesh)
- ✅ Apply Modifiers
- ✅ Include Normals (we ignore them, but Blender complains less if enabled)
- ❌ Include UVs
- ✅ Triangulate Mesh
- ✅ Materials → "Export"
- Forward axis: `-Z`
- Up axis: `Y`

---

## 6. Palette system

### 6.1 Convention

terra-siege uses a fixed palette defined in C++ (`src/mesh/palette.h`). Blender material names encode the palette index:

| Material name      | Palette index   |
|--------------------|-----------------|
| `c00`..`c15`       | 0–15            |
| `palette_00`..`palette_15` | 0–15 (alias) |
| anything else      | fallback (debug magenta) |

### 6.2 Initial palette (Virus-inspired, refine in code)

```cpp
// src/mesh/palette.h
#pragma once
#include "raylib.h"

static constexpr Color kPalette[16] = {
    {  0,   0,   0, 255}, // 0:  black
    {255, 255, 255, 255}, // 1:  white
    {255,   0,   0, 255}, // 2:  red
    {  0, 255,   0, 255}, // 3:  green
    {  0,   0, 255, 255}, // 4:  blue
    {255, 255,   0, 255}, // 5:  yellow
    {255,   0, 255, 255}, // 6:  magenta
    {  0, 255, 255, 255}, // 7:  cyan
    {128, 128, 128, 255}, // 8:  grey
    {192, 128,  64, 255}, // 9:  terrain brown
    { 64, 128, 192, 255}, // 10: sky blue
    {255, 128,   0, 255}, // 11: orange (hovercraft)
    {128,   0, 255, 255}, // 12: purple (enemy)
    {  0, 128,   0, 255}, // 13: dark green
    {128,  64,   0, 255}, // 14: dark brown
    {200, 200, 200, 255}, // 15: light grey
};

static constexpr Color kPaletteFallback = {255, 0, 255, 255}; // debug magenta
```

### 6.3 MTL handling

- Blender exports a `.mtl` sidecar. **Do not commit it to git.** Add `assets/meshes/*.mtl` to `.gitignore`.
- The loader ignores the MTL file entirely. Material names are parsed from `usemtl` directives in the OBJ itself.
- Blender uses the MTL on import for preview colours — this is local-only convenience and is not authoritative.

### 6.4 Index parsing

```cpp
// Returns palette index 0..15, or -1 for fallback.
// "c07" → 7, "palette_12" → 12, "anything_else" → -1
int parse_palette_index(std::string_view material_name);
```

A failed parse is a warning, not an error; the loader substitutes `kPaletteFallback` and continues.

---

## 7. Load-time behaviour

### 7.1 Loader contract

```cpp
// src/mesh/obj_loader.h
#pragma once
#include "raylib.h"
#include <filesystem>
#include <vector>

struct Mesh3D {
    std::vector<Vector3> vertices;      // unique positions
    std::vector<Vector3> face_normals;  // one per triangle (recomputed)
    std::vector<int>     indices;       // flat triangle list (3 * tri_count)
    std::vector<int>     face_palette;  // palette index per triangle (-1 = fallback)
};

Mesh3D LoadObjMesh(const std::filesystem::path& path);
Model  MeshToRaylibModel(const Mesh3D& mesh);
```

`Mesh3D` is the engine-native representation; `Model` is the raylib upload. The inspector works on `Mesh3D`; the renderer consumes `Model`.

### 7.2 Steps

1. Parse OBJ line by line. Collect `v`, `f`, `usemtl`, `g`, `o` directives.
2. Triangulate any quads encountered (`f a b c d` → `a b c` and `a c d`).
3. For each face, compute `normal = normalize(cross(B - A, C - A))`.
4. For each face, record the most recent `usemtl` value's parsed palette index.
5. Return the populated `Mesh3D`.

### 7.3 Validation at load time

- Empty mesh → error, abort load
- Degenerate triangle (zero-area, i.e. cross product magnitude < 1e-8) → warn, skip
- Vertex index out of range → error, abort load
- Material not matching pattern → warn, fallback colour

Warnings go to stderr in dev builds, are silenced in release.

---

## 8. In-engine mesh inspector

### 8.1 Invocation

```
$ ./terra-siege --inspect assets/meshes/hovercraft.obj
```

When `--inspect <path>` is passed, the engine boots into inspector mode instead of the game. The inspector reuses the engine's raylib setup, window, and rendering — no separate binary.

### 8.2 Scope (what the inspector does)

- Load the named OBJ via the same loader the game uses
- Render the mesh with face normals and palette colours, on a neutral background
- Camera: orbit around mesh origin (mouse-drag rotate, scroll zoom). raylib `Camera3D` in `CAMERA_THIRD_PERSON` mode is sufficient.
- Display each vertex as a small sphere (raylib `DrawSphere`, radius ≈ 0.5% of mesh bounding sphere)
- Allow vertex selection (raycast against vertex spheres using `GetMouseRay` + `GetRayCollisionSphere`)
- Drag a selected vertex along the camera-aligned plane; modifier keys `X`/`Y`/`Z` constrain to world axis
- Hotkey `S` saves the OBJ back to the same path
- Hotkey `R` reloads from disk (discards unsaved changes)
- Hotkey `Q` quits

### 8.3 File I/O contract (round-trip preservation)

The inspector must **preserve everything except vertex positions** on save:

- Original file is read as a list of lines on load
- On save, only lines matching `^v ` are rewritten with new positions (formatted as `v %.6f %.6f %.6f`)
- Comments, blank lines, `vn`, `vt`, `f`, `o`, `g`, `usemtl`, and all whitespace/ordering are preserved verbatim
- On save **with edits**, a single line of the form `# edited by terra-siege inspector YYYY-MM-DD HH:MM:SS` is appended or updated at end of file
- On save **without edits**, the file is not written at all (no-op). This makes T-06 trivially true.

This guarantees a Blender → inspector → Blender round-trip changes only the data the inspector is permitted to change.

### 8.4 Out of scope (inspector explicitly does NOT do)

- Adding or removing vertices
- Adding or removing faces
- Re-triangulating, subdividing, or simplifying
- Reassigning materials or remapping palette indices
- Editing normals (always recomputed at load)
- UV editing (there are no UVs)
- Loading any format other than OBJ
- Multi-object editing (one file, one mesh, one session)
- Undo/redo beyond a single in-memory snapshot at load (i.e. `R` to reload is the only "undo")

### 8.5 UI

Use **raygui** (raylib's immediate-mode GUI) for any on-screen controls. Minimal HUD only: filename, vertex count, selected vertex index + position, save state (clean/dirty). No menus, no panels, no toolbars.

---

## 9. Round-trip invariants

These properties MUST hold; they are the implementation's correctness criteria.

1. **Lossless re-export.** Blender → OBJ → load → render produces output that does not vary across re-loads.
2. **Position-only inspector edits.** Blender → OBJ → inspector edit → save → Blender import shows the edited positions and nothing else changed.
3. **Faceted appearance.** Rendered output uses per-face normals computed at load, regardless of `vn` directives in the file.
4. **Palette stability.** Material `cNN` resolves to `kPalette[NN]` consistently across game and inspector.
5. **Fallback safety.** A malformed material name produces a warning and magenta fallback colour, never a crash.

---

## 10. Test criteria

Implement alongside the loader; do not ship the loader without them.

| ID    | Test                                                  | Pass condition                                                         |
|-------|-------------------------------------------------------|------------------------------------------------------------------------|
| T-01  | Load `tests/fixtures/meshes/cube.obj` (8 v, 12 tris) | `Mesh3D` has 8 vertices, 12 face normals, 12 palette indices           |
| T-02  | Load mesh containing quads                            | Quads triangulated to N×2 triangles                                    |
| T-03  | Load mesh with `usemtl c07`                           | All faces under that material map to palette index 7                   |
| T-04  | Load mesh with `usemtl bogus`                         | Faces map to fallback (-1), warning logged                             |
| T-05  | Load mesh with one degenerate face                    | Face skipped, warning logged, mesh still loads with remaining faces    |
| T-06  | Inspector save with no edits                          | File not written; on-disk bytes unchanged                              |
| T-07  | Inspector save with one vertex moved                  | Only the `v` line for that vertex differs; final-line edit-comment appended; all other lines identical |
| T-08  | Round-trip Blender → game → inspector → Blender       | Vertex positions match within 1e-5 tolerance                           |

Fixtures live under `tests/fixtures/meshes/`. Provide at minimum:
- `cube.obj` (canonical 8/12 cube, single material `c01`)
- `cube_quads.obj` (same cube, faces as quads)
- `cube_multimat.obj` (cube with two materials, e.g. top face `c11`, rest `c08`)
- `cube_bogus_mat.obj` (cube with `usemtl not_a_palette_name`)
- `cube_degenerate.obj` (cube plus one zero-area triangle)

---

## 11. Anti-goals

The following are explicitly out of scope. If Claude Code finds itself drifting toward any of these, **stop and escalate.**

- ❌ Custom binary mesh format
- ❌ Asset baking, codegen, or build-time conversion
- ❌ Texture mapping or texture loading
- ❌ Skeletal animation, IK, or rigging (see §12 for the deferred path)
- ❌ UV editing or unwrapping
- ❌ Topology editing in the inspector (add/remove verts or faces)
- ❌ A separate inspector binary
- ❌ A GUI mesh-import wizard
- ❌ Asset hot-reload during gameplay (inspector reload is fine; gameplay reload is yak-shave)
- ❌ Multi-mesh scene files (one mesh per OBJ)
- ❌ Network-loaded assets
- ❌ Compression, encryption, or packing of asset files
- ❌ Pulling in a third-party OBJ parser when the in-house parser is ~200 lines

---

## 12. Future extension points (deferred)

None of these are in scope for the current phase.

- **Animation:** switch to **IQM** (raylib supports it natively; Blender exports via the `iqm-blender-addon`). Keep OBJ for static meshes; introduce IQM only when an animated mesh is actually needed. Do not retrofit existing OBJ assets.
- **More than 16 palette colours:** extend `kPalette[]`; the parser already supports two-digit indices.
- **Per-vertex colours** (Gouraud aesthetic): add a Blender vertex-colour layer convention and extend the loader. Currently anti-goal because it breaks the faceted aesthetic.
- **LOD:** out of scope until meshes routinely exceed 200 tris, which they shouldn't.

---

## 13. Decisions log (provisional D-IDs; renumber on integration)

| ID     | Decision                                          | Rationale                                                            |
|--------|---------------------------------------------------|----------------------------------------------------------------------|
| D-A01  | OBJ is the single source of truth                 | Text, git-friendly, tool-agnostic, native to Blender and raylib      |
| D-A02  | Normals recomputed at load time                   | Faceted aesthetic immune to Blender export state                     |
| D-A03  | MTL files git-ignored                             | Palette is C++ code; MTL is Blender-local convenience only           |
| D-A04  | Palette index encoded in material name            | Avoids per-vertex colour data; preserves flat-shaded aesthetic       |
| D-A05  | Inspector is a CLI mode of the game binary        | Avoids duplicating raylib setup; keeps scope tight                   |
| D-A06  | Inspector edits vertex positions only             | Round-trip preservation guarantee; topology lives in Blender         |
| D-A07  | Animation deferred to IQM, not bolted onto OBJ    | OBJ has no animation; introducing it would muddle the format contract |
| D-A08  | In-house OBJ parser, no third-party dependency    | ~200 lines vs. dependency surface; matches "minimal dependencies" stance |

---

## 14. Handoff notes for Claude Code

- **Start with the loader (§7), not the inspector.** The game needs to render meshes before edits matter.
- **Implement tests T-01 through T-05 alongside the loader.** Don't ship the loader without them.
- **Inspector mode is Phase 2.b**, scheduled after Phase 2.a (basic mesh rendering in-game).
- **No new dependencies.** Everything here is achievable with raylib + raygui + the C++17 standard library. If a third-party OBJ parser is tempting, write the 200 lines instead (per D-A08).
- **When in doubt, choose the option that preserves the round-trip invariants in §9.**
- **Renumber D-A01..D-A08 to the next available D-NNN block in terra-siege's `DECISIONS.md`** on first integration. The "D-A" prefix here is provisional and exists only to avoid collision until that integration happens.

---
