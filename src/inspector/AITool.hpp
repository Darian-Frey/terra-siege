#pragma once

#include "Tool.hpp"

namespace tsmesh {

// F.4 mini-tool — AI profile + ranges + target preference + infection
// block. One tool because the fields all live in adjacent sidecar
// sections (`ai` + `infection`) and authors usually tune them
// together. `N` adds an ai block if one's absent; once present, the
// form fields edit live values. Profile + targetPref are enum
// pickers (↑/↓ cycles); ranges + infection numerics use the standard
// ./, cycle + ↑/↓ step (Shift = coarse). canBeInfected is a single
// SPACE/Enter toggle when focused.
//
// 3D viz: two wireframe rings on the XZ plane around the pivot —
// orange for detectionRange, red for attackRange. Drawn only while
// this tool is active so the viewport stays uncluttered.
class AITool : public Tool {
public:
  const char *name() const override { return "ai"; }
  void handleInput(Inspector &insp) override;
  void render3D(const Inspector &insp) const override;
  void renderHud(const Inspector &insp, int &yCursor) const override;
  bool save(Inspector &insp) override;
  void onReload(Inspector &insp) override;
  bool isDirty() const override { return m_dirty; }

private:
  // 0=profile, 1=targetPref, 2=detection, 3=attack, 4=evade,
  // 5=retreat, 6=canBeInfected, 7=rebootDuration, 8=speedPenaltyAfter
  int m_focus = 0;
  bool m_dirty = false;
};

} // namespace tsmesh
