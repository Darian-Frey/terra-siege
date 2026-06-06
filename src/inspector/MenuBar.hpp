#pragma once

#include "raylib.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tsmesh {

class Inspector;

// ====================================================================
// MenuBar — hand-rolled top-of-screen menu strip + dropdowns.
//
// No external GUI library yet (per CLAUDE.md). Built entirely from
// raylib DrawRectangle + DrawText. Each item is a callable plus
// optional enabled/checked predicates so the menu reflects live
// inspector state (e.g. "Save" greys out when nothing's dirty,
// "Vertex" gets a ✓ when it's the active tool).
//
// Interaction model:
//   * Click a top-level label → open its dropdown
//   * Hover an item with a submenu (e.g. "Open Recent") → submenu
//     appears to the right
//   * Click a leaf item → run its action + close all panels
//   * Click outside the bar/dropdown OR press Esc → close
//
// While a menu is open, the inspector skips normal hotkey + tool +
// camera input so a click on a dropdown row doesn't also fire the
// vertex picker behind it.
// ====================================================================
class MenuBar {
public:
  static constexpr int kBarHeight = 24;

  // Callback signatures. Each receives the inspector pointer so the
  // closure can mutate state. enabled/checked are pure-read predicates.
  using Action = std::function<void(Inspector &)>;
  using BoolPred = std::function<bool(const Inspector &)>;

  // Per-row item. Three flavours sharing one struct:
  //   * leaf     — action set, submenu empty
  //   * submenu  — submenu non-empty (action ignored)
  //   * separator — `separator = true`, no row drawn beyond a line
  struct Item {
    std::string label;
    std::string shortcut;
    Action action;
    BoolPred enabled;       // null = always enabled
    BoolPred checked;       // null = no ✓ rendered
    std::vector<Item> submenu;
    bool separator = false;
    // Submenu builder — called each frame instead of using `submenu`
    // when set. Lets the Recent-Files list refresh from cfg state.
    std::function<std::vector<Item>(const Inspector &)> dynamicSubmenu;
  };

  struct Menu {
    std::string label;
    std::vector<Item> items;
  };

  MenuBar();

  // Build the standard inspector menu set (File / View / Tool / Help).
  // Called once by Inspector's ctor; rebuilding is supported but not
  // expected.
  void buildDefault();

  // Per-frame entry points. Order matters: handle() FIRST so a click
  // on the bar gets consumed before tool input sees it.
  // Returns true if the menu consumed input this frame (caller should
  // skip the rest of its normal input handling).
  bool handle(Inspector &insp);
  void render(const Inspector &insp);

  // True if a dropdown is currently visible; suppresses pass-through
  // hotkeys / tool / camera input.
  bool isOpen() const { return m_open >= 0; }

  // The pointer-capture rectangle for the bar — used by Inspector to
  // gate mouse-on-bar clicks from reaching the 3D scene.
  Rectangle barRect(int screenWidth) const {
    return {0.0f, 0.0f, static_cast<float>(screenWidth),
            static_cast<float>(kBarHeight)};
  }

private:
  // Visual constants — kept small and tunable. Widths are computed
  // from MeasureText at render time so menu sizes adapt to label
  // length and shortcut length without hardcoding.
  static constexpr int kBarTextSize = 14;
  static constexpr int kBarPadX = 14;     // padding around top-level labels
  static constexpr int kRowH = 22;
  static constexpr int kRowTextSize = 13;
  static constexpr int kRowPadX = 12;
  static constexpr int kRowGap = 22;      // gap between label + shortcut

  // Compute the on-screen rect for top-level menu `i` given the bar
  // y-origin. Used in both hit-testing and rendering so they stay in
  // lockstep.
  Rectangle topRect(int i) const;

  // Compute the dropdown panel rect for the open menu. Returns
  // `{0,0,0,0}` if no menu is open. `items` is the (possibly
  // dynamically-built) list to size against.
  Rectangle dropdownRect(int menuIdx, const std::vector<Item> &items) const;
  Rectangle submenuRect(Rectangle parentRow,
                        const std::vector<Item> &subItems) const;

  // Resolve the items vector for the current open menu — either the
  // static `items` or the result of `dynamicSubmenu` for the
  // hovered-with-submenu row. Always returns a fresh vector so the
  // caller can hold it across the frame.
  std::vector<Item> resolveItems(int menuIdx, const Inspector &insp) const;
  std::vector<Item> resolveSubItems(const Item &parent,
                                    const Inspector &insp) const;

  std::vector<Menu> m_menus;
  int m_open = -1;        // top-level menu index, -1 = none
  int m_hoverRow = -1;    // row index in the open dropdown
  int m_openSubRow = -1;  // row in the dropdown whose submenu is open
  int m_hoverSubRow = -1; // row index in the open submenu

  // x-positions of each top-level label, refreshed each render so
  // the hit-test in handle() uses the same geometry.
  std::vector<float> m_topX;
  std::vector<float> m_topW;
};

} // namespace tsmesh
