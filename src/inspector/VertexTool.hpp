#pragma once

#include "Tool.hpp"
#include "raylib.h"

#include <string>
#include <vector>

namespace tsmesh {

// Inspector Phase B — vertex editing depth. Vertex picker + drag plus:
//
//   * Multi-select: plain click sets selection, Shift+click toggles a
//     vertex, click on empty space clears, Ctrl+LMB drag boxes select.
//   * Translate selection: one drag moves every selected vertex by the
//     same delta.
//   * Hover highlight: the vertex under the cursor is brightened before
//     you click so picking is unambiguous.
//   * Undo / redo: ring buffer of 32 pre-edit vertex-array snapshots
//     (Ctrl+Z / Ctrl+Y).
//   * Snap-to-grid: configurable step (0.05 / 0.1 / 0.25 / 0.5 / 1.0)
//     cycled with `[` / `]`; hold Shift to suspend snap mid-drag.
//   * Snap-to-vertex: while dragging, if cursor is near another vertex
//     in screen space the drag target snaps onto it.
//   * Numeric X/Y/Z input: with a single vertex focused, press X / Y /
//     Z to type an exact value for that coordinate; Enter commits.
//   * Stats overlay: vert / face / selection counts at the top of the
//     tool HUD.
//
// `R` (Inspector-level reload-from-disk) still works as a hard reset
// and clears the undo stack.
class VertexTool : public Tool {
public:
  const char *name() const override { return "vertex"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // Selection model — sorted vector for stable iteration; small sizes
  // (typically <30) make set<>'s O(log n) overhead not worth it.
  bool selectionContains(int idx) const;
  void selectionAdd(int idx);
  void selectionToggle(int idx);
  void selectionClear();

  // Closest vertex to the current mouse ray (uses sphere intersection,
  // same code path as the original picker). Returns -1 if no vertex is
  // within the sphere radius.
  int pickVertexUnderMouse(const Inspector &insp) const;

  // Project a world-space point to screen space — used by box-select
  // and snap-to-vertex.
  Vector2 projectToScreen(Vector3 worldPos, const Inspector &insp) const;

  // Drag helpers.
  void beginDrag(Inspector &insp, int axisLock);
  void updateDrag(Inspector &insp, int axisLock);
  void endDrag(Inspector &insp);

  // Snap helpers.
  Vector3 applySnapGrid(Vector3 pos) const;
  // Returns the index of a vertex within the screen-space threshold of
  // the cursor (excluding any vertex in the selection so we don't snap
  // onto ourselves), or -1 if none.
  int snapVertexUnderCursor(const Inspector &insp) const;

  // Undo snapshot helper — defers to Inspector's shared stack so
  // PrimitivesTool insertions land on the same history. Wraps the
  // numeric-input cancel that used to live in undo().
  void pushUndoSnapshot(Inspector &insp);

  // Numeric input.
  void commitNumericInput(Inspector &insp);
  void cancelNumericInput();

  // -------- State --------
  std::vector<int> m_selection;
  int m_hover = -1;       // vertex under cursor when not dragging; -1 = none

  bool m_dragging = false;
  int m_axisLock = -1;    // 0=X 1=Y 2=Z; -1 = camera plane
  // Per-selected-vertex drag-start positions. Parallel to m_selection
  // when a drag is in progress so the delta is computed once and applied
  // to every selected vertex.
  std::vector<Vector3> m_dragStartPositions;
  // Anchor vertex's drag-start position — used as the reference point
  // for the cursor projection (so all selected verts translate by the
  // same delta as the dragged anchor).
  int m_dragAnchor = -1;
  Vector3 m_dragAnchorStart{};

  // Box-select.
  bool m_boxSelecting = false;
  Vector2 m_boxStart{};

  // Snap.
  static constexpr float kSnapSteps[5] = {0.05f, 0.1f, 0.25f, 0.5f, 1.0f};
  int m_snapStepIdx = 1;  // default 0.1
  bool m_snapEnabled = false;
  // Pixel threshold for snap-to-vertex during drag.
  static constexpr float kVertexSnapPx = 14.0f;
  // Set during a drag if a snap-to-vertex target is active. Used by
  // render3D to highlight the snap target with a ring.
  int m_snapTargetIdx = -1;

  // Numeric input — active when the user has typed X/Y/Z to begin
  // editing the focused single-vertex coord. Axis: 0/1/2 for X/Y/Z;
  // -1 = no input mode.
  int m_inputAxis = -1;
  std::string m_inputBuf;

  bool m_dirty = false;
};

} // namespace tsmesh
