#pragma once

namespace tsmesh {

class Inspector;

// One pluggable "mode" inside the inspector. Vertex editing is the
// first; future tools (forward-axis, weapon mount points, collision
// capsules…) implement this interface so adding them is a localized
// change rather than an edit to Inspector itself.
//
// Lifecycle hooks fire only for the *active* tool. TAB cycles the
// active tool; S/R/Q + camera orbit stay on Inspector and apply
// regardless. yCursor in renderHud() is the next 2D y-position to
// draw at — advance it past whatever you draw.
class Tool {
public:
  virtual ~Tool() = default;
  virtual const char *name() const = 0;
  virtual void handleInput(Inspector &insp) = 0;
  virtual void render3D(const Inspector &insp) const = 0;
  virtual void renderHud(const Inspector &insp, int &yCursor) const = 0;

  virtual bool save(Inspector &insp) {
    (void)insp;
    return true;
  }
  virtual void onActivate(Inspector &insp) { (void)insp; }
  virtual void onDeactivate(Inspector &insp) { (void)insp; }
  virtual void onReload(Inspector &insp) { (void)insp; }
  virtual bool isDirty() const { return false; }
};

} // namespace tsmesh
