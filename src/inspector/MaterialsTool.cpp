#include "MaterialsTool.hpp"

#include "Inspector.hpp"
#include "InspectorFont.hpp"
#include "MenuBar.hpp"
#include "mesh/ObjLoader.hpp"
#include "mesh/Palette.hpp"
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cstdio>

namespace tsmesh {

namespace {

// 32 swatches laid out in a 4 column × 8 row grid on the right edge.
constexpr int kSwatchCols  = 4;
constexpr int kSwatchRows  = 8;
constexpr int kSwatchSize  = 26;
constexpr int kSwatchGap   = 4;
constexpr int kPanelMargin = 12; // distance from screen right edge

bool pointInRect(Vector2 p, Rectangle r) {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y &&
         p.y <= r.y + r.height;
}

} // anonymous namespace

// ====================================================================
// Geometry helpers
// ====================================================================
Rectangle MaterialsTool::swatchRect(int idx, int screenW, int screenH) const {
  // Bottom-anchored so the grid sits clear of the menu bar at the top.
  int col = idx % kSwatchCols;
  int row = idx / kSwatchCols;
  int panelW = kSwatchCols * (kSwatchSize + kSwatchGap) - kSwatchGap;
  int panelH = kSwatchRows * (kSwatchSize + kSwatchGap) - kSwatchGap;
  int x0 = screenW - panelW - kPanelMargin;
  int y0 = (screenH - panelH) / 2;
  return {static_cast<float>(x0 + col * (kSwatchSize + kSwatchGap)),
          static_cast<float>(y0 + row * (kSwatchSize + kSwatchGap)),
          static_cast<float>(kSwatchSize),
          static_cast<float>(kSwatchSize)};
}

bool MaterialsTool::isUsedInMesh(const Inspector &insp, int paletteIdx) const {
  const auto &fp = insp.mesh().facePalette;
  for (int idx : fp) {
    if (idx == paletteIdx) return true;
  }
  return false;
}

int MaterialsTool::pickFace(const Inspector &insp) const {
  Ray ray = GetMouseRay(GetMousePosition(), insp.camera());
  const auto &m = insp.mesh();
  size_t triCount = m.indices.size() / 3;
  int best = -1;
  float bestDist = 1e30f;
  for (size_t t = 0; t < triCount; ++t) {
    Vector3 a = m.vertices[m.indices[t * 3 + 0]];
    Vector3 b = m.vertices[m.indices[t * 3 + 1]];
    Vector3 c = m.vertices[m.indices[t * 3 + 2]];
    RayCollision hit = GetRayCollisionTriangle(ray, a, b, c);
    if (hit.hit && hit.distance < bestDist) {
      bestDist = hit.distance;
      best = static_cast<int>(t);
    }
  }
  return best;
}

// ====================================================================
// Input
// ====================================================================
void MaterialsTool::handleInput(Inspector &insp) {
  Vector2 mouse = GetMousePosition();
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

  // ---- Swatch hover + click ----
  m_hoverSwatch = -1;
  for (int i = 0; i < 32; ++i) {
    if (pointInRect(mouse, swatchRect(i, sw, sh))) {
      m_hoverSwatch = i;
      break;
    }
  }

  if (m_hoverSwatch >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    m_paintIdx = m_hoverSwatch;
    // If we have a face selection, paint it all to the new colour.
    if (!m_selection.empty()) {
      for (int t : m_selection) {
        if (t >= 0 && t < static_cast<int>(insp.mesh().facePalette.size())) {
          insp.mesh().facePalette[t] = m_paintIdx;
        }
      }
      insp.rebuildModel();
      m_dirty = true;
    }
    return; // consume the click
  }

  // ---- Face hover + click ----
  // Skip face-picking if the mouse is over the swatch panel — clicks
  // there were already handled.
  if (m_hoverSwatch < 0) {
    m_hoverFace = pickFace(insp);

    // E = eyedropper — copy the hovered face's palette to the paint
    // slot without painting anything.
    if (IsKeyPressed(KEY_E) && m_hoverFace >= 0) {
      m_paintIdx = insp.mesh().facePalette[m_hoverFace];
      return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && m_hoverFace >= 0) {
      if (shift) {
        // Add to / remove from selection (toggle).
        auto it = std::find(m_selection.begin(), m_selection.end(),
                            m_hoverFace);
        if (it != m_selection.end()) m_selection.erase(it);
        else m_selection.push_back(m_hoverFace);
      } else {
        // Plain click — paint immediately, clear selection.
        insp.pushUndo();
        insp.mesh().facePalette[m_hoverFace] = m_paintIdx;
        insp.rebuildModel();
        m_dirty = true;
        m_selection.clear();
      }
    }
  } else {
    m_hoverFace = -1;
  }

  // Escape clears selection.
  if (IsKeyPressed(KEY_ESCAPE)) m_selection.clear();
}

// ====================================================================
// 3D render — highlight hover face + selection
// ====================================================================
void MaterialsTool::render3D(const Inspector &insp) const {
  const auto &m = insp.mesh();

  // Selection: thick yellow outline per triangle.
  Color selCol{255, 220, 60, 240};
  for (int t : m_selection) {
    if (t < 0 || t * 3 + 2 >= static_cast<int>(m.indices.size())) continue;
    Vector3 a = m.vertices[m.indices[t * 3 + 0]];
    Vector3 b = m.vertices[m.indices[t * 3 + 1]];
    Vector3 c = m.vertices[m.indices[t * 3 + 2]];
    DrawLine3D(a, b, selCol);
    DrawLine3D(b, c, selCol);
    DrawLine3D(c, a, selCol);
  }

  // Hover: lighter cyan outline.
  if (m_hoverFace >= 0 &&
      m_hoverFace * 3 + 2 < static_cast<int>(m.indices.size())) {
    Color hoverCol{120, 220, 255, 220};
    Vector3 a = m.vertices[m.indices[m_hoverFace * 3 + 0]];
    Vector3 b = m.vertices[m.indices[m_hoverFace * 3 + 1]];
    Vector3 c = m.vertices[m.indices[m_hoverFace * 3 + 2]];
    DrawLine3D(a, b, hoverCol);
    DrawLine3D(b, c, hoverCol);
    DrawLine3D(c, a, hoverCol);
  }
}

// ====================================================================
// HUD — swatch panel on the right, status line below the menu bar
// ====================================================================
void MaterialsTool::renderHud(const Inspector &insp, int &yCursor) const {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();

  // ---- Status line ----
  char buf[256];
  drawText("materials (L-click face = paint, shift = add to selection, "
           "E = eyedropper, ESC clears selection)",
           10, yCursor, 14, {160, 180, 200, 220});
  yCursor += 22;

  std::snprintf(buf, sizeof(buf), "paint = c%02d   |   selected faces: %zu",
                m_paintIdx, m_selection.size());
  drawText(buf, 14, yCursor, 14, {220, 230, 250, 240});
  yCursor += 18;

  if (m_hoverFace >= 0 &&
      m_hoverFace < static_cast<int>(insp.mesh().facePalette.size())) {
    int p = insp.mesh().facePalette[m_hoverFace];
    char hbuf[64];
    if (p < 0) std::snprintf(hbuf, sizeof(hbuf), "hover face: fallback");
    else std::snprintf(hbuf, sizeof(hbuf), "hover face: c%02d", p);
    drawText(hbuf, 14, yCursor, 14, {200, 220, 240, 220});
    yCursor += 18;
  }

  // ---- Swatch panel (right edge, vertically centred) ----
  for (int i = 0; i < 32; ++i) {
    Rectangle r = swatchRect(i, sw, sh);
    Color col = kPalette[i];
    bool used = isUsedInMesh(insp, i);
    if (!used) {
      // Dim unused colours so authors see what's actually referenced.
      col.r = static_cast<unsigned char>(col.r / 3);
      col.g = static_cast<unsigned char>(col.g / 3);
      col.b = static_cast<unsigned char>(col.b / 3);
    }
    DrawRectangleRec(r, col);

    // Selection / hover ring.
    if (i == m_paintIdx) {
      DrawRectangleLinesEx(
          Rectangle{r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 2.0f,
          {255, 220, 60, 255});
    } else if (i == m_hoverSwatch) {
      DrawRectangleLinesEx(r, 1.5f, {255, 255, 255, 240});
    } else {
      DrawRectangleLinesEx(r, 1.0f, {0, 0, 0, 180});
    }

    // Index label (2-digit).
    char lbl[4];
    std::snprintf(lbl, sizeof(lbl), "%02d", i);
    // Pick black or white text based on swatch brightness.
    int brightness = col.r + col.g + col.b;
    Color textCol = (brightness > 380) ? Color{0, 0, 0, 230}
                                       : Color{255, 255, 255, 230};
    int tw = measureText(lbl, 12);
    drawText(lbl, static_cast<int>(r.x + (r.width - tw) / 2),
             static_cast<int>(r.y + (r.height - 12) / 2), 12, textCol);
  }
}

bool MaterialsTool::save(Inspector &insp) {
  if (!saveObjMaterials(insp.path(), insp.mesh().indices,
                        insp.mesh().facePalette, m_dirty)) {
    std::fprintf(stderr, "[MaterialsTool] save failed\n");
    return false;
  }
  m_dirty = false;
  return true;
}

void MaterialsTool::onReload(Inspector & /*insp*/) {
  m_selection.clear();
  m_hoverFace = -1;
  m_hoverSwatch = -1;
  m_dirty = false;
  // m_paintIdx preserved — author's working colour shouldn't reset
  // every time they pick a different mesh.
}

} // namespace tsmesh
