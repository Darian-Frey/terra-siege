#pragma once

#include "raylib.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// ====================================================================
// OBJ mesh loader (3d_assets.md §7).
//
// Reads vertex positions + face indices + per-face material reference
// from a Wavefront .obj. Normals are ALWAYS recomputed per-face at
// load time so the faceted appearance survives any Blender export
// state. UVs (vt), vertex normals (vn), and the MTL sidecar are
// intentionally ignored.
//
// Two return paths:
//   loadMesh()     — engine-native Mesh3D for the inspector to edit.
//   uploadModel()  — direct upload to a raylib Model for runtime use.
//
// The runtime render path uses uploadModel() so we skip the
// Mesh3D allocation when the inspector isn't involved.
// ====================================================================
namespace tsmesh {

// CPU-side mesh representation. One face_normal and one face_palette
// entry per triangle in `indices`. `vertices` holds unique positions;
// `indices` is a flat triangle list of size 3 × triangle_count.
struct Mesh3D {
  std::vector<Vector3> vertices;
  std::vector<Vector3> faceNormals;
  std::vector<int32_t> indices; // flat triangle list (3 per face)
  std::vector<int> facePalette; // palette index per triangle (-1 = fallback)
};

// Status returned by the loader. Mirrors §7.3 validation rules.
enum class LoadStatus : uint8_t {
  Ok = 0,
  FileNotFound,
  Empty,         // file parsed but contained zero usable triangles
  VertexIndexOutOfRange,
  IoError,
};

// Result wrapper — explicit pass/fail with the mesh inline so callers
// don't have to predeclare. Move-only because Mesh3D is heavy.
struct LoadResult {
  LoadStatus status = LoadStatus::FileNotFound;
  Mesh3D mesh{};
  bool ok() const { return status == LoadStatus::Ok; }
};

// Parse the given OBJ file. Degenerate triangles (zero-area) are
// skipped with a warning; out-of-range vertex indices abort the load.
// Material names not matching the palette pattern fall back to
// kPaletteFallback with a warning. Warnings go to stderr in debug;
// silenced when NDEBUG is set.
LoadResult loadMesh(const std::filesystem::path &path);

// Upload a CPU-side Mesh3D into a raylib Model for rendering. The
// Model owns its GPU buffers — caller is responsible for
// UnloadModel() at shutdown.
Model uploadModel(const Mesh3D &mesh);

// Fast-path runtime loader. Equivalent to loadMesh() + uploadModel()
// minus the intermediate Mesh3D allocation. Used by MeshRegistry at
// startup; the inspector uses loadMesh() so it has the CPU-side data
// to edit.
Model loadModel(const std::filesystem::path &path);

// ====================================================================
// Inspector save support (3d_assets.md §8.3 round-trip preservation).
//
// The inspector edits vertex positions only — never topology or
// materials. To preserve the rest of the file byte-exactly we read
// the original line-by-line and rewrite only `v ` directives.
// Everything else (comments, blank lines, vn/vt/f/o/g/usemtl) is
// kept verbatim, in original order.
// ====================================================================

// Read every line of a file verbatim. CR stripping is the only
// normalisation. Returns empty vector if the file can't be opened.
std::vector<std::string> readObjLines(const std::filesystem::path &path);

// Save vertices back to an OBJ. The N-th `v ` line in the original
// is rewritten with newVerts[N]; everything else is preserved
// exactly. An "edited by terra-siege inspector <timestamp>" comment
// is appended (or updated in place if a previous one exists).
//
// If `isDirty` is false the file is NOT written at all — this is
// the T-06 contract (no edits → no save → on-disk bytes unchanged).
//
// Returns true on success.
bool saveObjVertices(const std::filesystem::path &path,
                     const std::vector<Vector3> &newVerts, bool isDirty);

// Save material assignments back to an OBJ (Inspector Phase C).
// Unlike saveObjVertices, this REPLACES the entire face section
// (from the first usemtl/f line through the last f line), regenerating
// triangle lines + usemtl directives from the in-memory state.
// Everything BEFORE the face section (comments, verts, vn, vt, o, g, s)
// and everything AFTER the last face line is preserved verbatim.
//
// Triangles are emitted in mesh order (matches load order), so the
// original file ordering is preserved up to face-group labels (which
// are lost — the rebuilder doesn't preserve `o`/`g` lines that
// intermix with face lines).
//
// If `isDirty` is false the file is NOT written. T-06-equivalent
// contract for the materials path.
bool saveObjMaterials(const std::filesystem::path &path,
                      const std::vector<int32_t> &indices,
                      const std::vector<int> &facePalette,
                      bool isDirty);

// Save the full mesh — verts + face section — for cases where topology
// changed (vertex add/delete, face add/delete). Inspector Phase D.
//
// Layout of the resulting OBJ:
//   * Any leading header lines from the original (comments, o, g, s
//     directives that appeared BEFORE any geometry) are preserved
//     verbatim. If the file is empty / doesn't exist (start-from-
//     primitive flow), a minimal generated header is emitted instead.
//   * Then ALL vertices are written as `v x y z` from the in-memory
//     state — count may differ from the original.
//   * Then the face section: `usemtl cNN` at palette boundaries +
//     `f a b c` lines in mesh order.
//   * Any trailing lines from the original AFTER the last f line are
//     preserved verbatim with edit-marker dedupe.
//
// Vn/vt directives from the original are dropped — the loader
// recomputes face normals every time, so preserved vn data would have
// been stale anyway. Tex coords (vt) we never preserved.
//
// If `isDirty` is false the file is NOT written.
bool saveObjMesh(const std::filesystem::path &path,
                 const std::vector<Vector3> &vertices,
                 const std::vector<int32_t> &indices,
                 const std::vector<int> &facePalette, bool isDirty);

} // namespace tsmesh
