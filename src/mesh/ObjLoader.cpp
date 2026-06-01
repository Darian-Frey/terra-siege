#include "ObjLoader.hpp"
#include "Palette.hpp"
#include "raymath.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace tsmesh {

namespace {

// Warn to stderr in debug builds. Silenced via NDEBUG in release.
[[gnu::format(printf, 1, 2)]]
void warn(const char *fmt, ...) {
#ifndef NDEBUG
  std::va_list args;
  va_start(args, fmt);
  std::fprintf(stderr, "[ObjLoader] ");
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  va_end(args);
#else
  (void)fmt;
#endif
}

// Strip CR (Windows line endings) + trailing whitespace from a string
// in-place. OBJs may come from any editor; we accept all three line
// endings.
void rstrip(std::string &s) {
  while (!s.empty() &&
         (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' ||
          s.back() == '\t')) {
    s.pop_back();
  }
}

// Parse a single face vertex reference. OBJ face vertices can be
// "v", "v/vt", "v//vn", or "v/vt/vn"; we want just the v index.
// 1-based in the file → returns 0-based, or -1 on parse failure.
int parseFaceVertex(const std::string &token) {
  if (token.empty()) return -1;
  // Find the first '/' (or end).
  size_t slash = token.find('/');
  std::string idx = (slash == std::string::npos)
                        ? token
                        : token.substr(0, slash);
  if (idx.empty()) return -1;
  try {
    int n = std::stoi(idx);
    // Negative indices = "from the end" in OBJ. Not used in our exports
    // but harmless to handle.
    return n; // caller resolves negatives
  } catch (...) {
    return -1;
  }
}

} // anonymous namespace

int parsePaletteIndex(std::string_view name) {
  // Accepted forms: "c<NN>" or "palette_<NN>", NN in 0..31.
  // One or two digits.
  std::string_view body;
  if (name.size() >= 2 && name[0] == 'c' && std::isdigit(name[1])) {
    body = name.substr(1);
  } else if (name.size() >= 9 && name.substr(0, 8) == "palette_") {
    body = name.substr(8);
  } else {
    return -1;
  }
  if (body.empty() || body.size() > 2) return -1;
  // Strict digit check — no spaces, no trailing junk.
  for (char c : body) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
  }
  int idx = 0;
  for (char c : body) idx = idx * 10 + (c - '0');
  if (idx < 0 || idx >= 32) return -1;
  return idx;
}

LoadResult loadMesh(const std::filesystem::path &path) {
  LoadResult result;
  result.status = LoadStatus::Ok;

  std::ifstream in(path);
  if (!in.is_open()) {
    warn("file not found: %s", path.string().c_str());
    result.status = LoadStatus::FileNotFound;
    return result;
  }

  std::vector<Vector3> vertices;
  std::vector<int32_t> indices;
  std::vector<int> facePalette;

  // Active material's palette index — updated by each usemtl directive.
  // Starts at -1 (fallback) so faces before any usemtl get the fallback
  // colour rather than silently picking index 0.
  int currentPalette = -1;

  std::string line;
  while (std::getline(in, line)) {
    rstrip(line);
    if (line.empty()) continue;
    if (line[0] == '#') continue; // comment

    std::istringstream iss(line);
    std::string tag;
    iss >> tag;

    if (tag == "v") {
      // Vertex position.
      Vector3 v{};
      iss >> v.x >> v.y >> v.z;
      vertices.push_back(v);
    } else if (tag == "f") {
      // Face — collect all vertex indices then triangulate.
      std::vector<int> faceIdx;
      std::string tok;
      while (iss >> tok) {
        int n = parseFaceVertex(tok);
        if (n == 0) {
          warn("face vertex index 0 (must be 1-based)");
          continue;
        }
        // Resolve 1-based → 0-based, and negative → "from end".
        int resolved =
            (n > 0) ? (n - 1)
                    : (static_cast<int>(vertices.size()) + n);
        if (resolved < 0 ||
            resolved >= static_cast<int>(vertices.size())) {
          warn("vertex index out of range: %d (have %zu)", n,
               vertices.size());
          result.status = LoadStatus::VertexIndexOutOfRange;
          return result;
        }
        faceIdx.push_back(resolved);
      }
      if (faceIdx.size() < 3) {
        warn("face with fewer than 3 vertices, skipping");
        continue;
      }
      // Triangulate fan: (0, 1, 2), (0, 2, 3), (0, 3, 4), ...
      // Each emitted triangle gets the currentPalette tag.
      for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
        int a = faceIdx[0];
        int b = faceIdx[i];
        int c = faceIdx[i + 1];

        // Degenerate-triangle check — zero-area faces are skipped to
        // avoid NaN normals downstream. Magnitude threshold 1e-8 per
        // the spec.
        Vector3 va = vertices[a];
        Vector3 vb = vertices[b];
        Vector3 vc = vertices[c];
        Vector3 edge1 = Vector3Subtract(vb, va);
        Vector3 edge2 = Vector3Subtract(vc, va);
        Vector3 cross = Vector3CrossProduct(edge1, edge2);
        float mag2 =
            cross.x * cross.x + cross.y * cross.y + cross.z * cross.z;
        if (mag2 < 1e-16f) {
          warn("degenerate triangle, skipping");
          continue;
        }
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
        facePalette.push_back(currentPalette);
      }
    } else if (tag == "usemtl") {
      std::string mat;
      iss >> mat;
      int idx = parsePaletteIndex(mat);
      if (idx < 0) {
        warn("material '%s' does not match palette pattern, using "
             "fallback colour",
             mat.c_str());
      }
      currentPalette = idx;
    }
    // Other directives (o, g, vn, vt, mtllib, s) are accepted-and-ignored
    // per §3.3 / §3.4. The inspector preserves them verbatim by reading
    // and writing raw lines (§8.3) — this loader doesn't need them.
  }

  if (indices.empty() || vertices.empty()) {
    warn("empty mesh: %s", path.string().c_str());
    result.status = LoadStatus::Empty;
    return result;
  }

  // Recompute per-face normals (§3.3 invariant — never trust vn from file).
  size_t triCount = indices.size() / 3;
  std::vector<Vector3> normals;
  normals.reserve(triCount);
  for (size_t t = 0; t < triCount; ++t) {
    Vector3 a = vertices[indices[t * 3 + 0]];
    Vector3 b = vertices[indices[t * 3 + 1]];
    Vector3 c = vertices[indices[t * 3 + 2]];
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(c, a)));
    normals.push_back(normal);
  }

  result.mesh.vertices = std::move(vertices);
  result.mesh.faceNormals = std::move(normals);
  result.mesh.indices = std::move(indices);
  result.mesh.facePalette = std::move(facePalette);
  return result;
}

Model uploadModel(const Mesh3D &mesh) {
  // raylib's Mesh is per-vertex (not indexed), so we expand into a
  // triangle soup with per-vertex normals + per-vertex colours. Each
  // triangle's three verts share the face normal (faceted look) and
  // the face's palette colour. Vertex/colour memory is owned by the
  // raylib Mesh and freed via UnloadModel().
  Mesh rlMesh{};
  size_t triCount = mesh.indices.size() / 3;
  rlMesh.triangleCount = static_cast<int>(triCount);
  rlMesh.vertexCount = static_cast<int>(triCount * 3);

  rlMesh.vertices = static_cast<float *>(
      std::malloc(sizeof(float) * 3 * rlMesh.vertexCount));
  rlMesh.normals = static_cast<float *>(
      std::malloc(sizeof(float) * 3 * rlMesh.vertexCount));
  rlMesh.colors = static_cast<unsigned char *>(
      std::malloc(sizeof(unsigned char) * 4 * rlMesh.vertexCount));

  for (size_t t = 0; t < triCount; ++t) {
    Vector3 n = mesh.faceNormals[t];
    int palette = mesh.facePalette[t];
    Color col = (palette >= 0 && palette < 32) ? kPalette[palette]
                                               : kPaletteFallback;
    for (int j = 0; j < 3; ++j) {
      Vector3 v = mesh.vertices[mesh.indices[t * 3 + j]];
      size_t base = (t * 3 + j) * 3;
      rlMesh.vertices[base + 0] = v.x;
      rlMesh.vertices[base + 1] = v.y;
      rlMesh.vertices[base + 2] = v.z;
      rlMesh.normals[base + 0] = n.x;
      rlMesh.normals[base + 1] = n.y;
      rlMesh.normals[base + 2] = n.z;
      size_t cbase = (t * 3 + j) * 4;
      rlMesh.colors[cbase + 0] = col.r;
      rlMesh.colors[cbase + 1] = col.g;
      rlMesh.colors[cbase + 2] = col.b;
      rlMesh.colors[cbase + 3] = col.a;
    }
  }

  UploadMesh(&rlMesh, false); // false = static, no dynamic updates
  Model model = LoadModelFromMesh(rlMesh);
  return model;
}

Model loadModel(const std::filesystem::path &path) {
  LoadResult lr = loadMesh(path);
  if (!lr.ok()) {
    // Return an empty Model — caller should null-check meshCount.
    return Model{};
  }
  return uploadModel(lr.mesh);
}

} // namespace Mesh
