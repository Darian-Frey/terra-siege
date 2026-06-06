#include "MenuBar.hpp"

#include "Inspector.hpp"
#include "InspectorConfig.hpp"

#include <algorithm>
#include <cstdio>

namespace tsmesh {

namespace {

constexpr Color BAR_BG       = {32, 38, 50, 255};
constexpr Color BAR_BORDER   = {60, 80, 110, 255};
constexpr Color BAR_HOVER_BG = {60, 90, 130, 220};
constexpr Color BAR_OPEN_BG  = {60, 90, 130, 255};
constexpr Color BAR_TEXT     = {220, 230, 250, 240};
constexpr Color BAR_DIM      = {130, 150, 180, 220};
constexpr Color DD_BG        = {38, 46, 60, 240};
constexpr Color DD_BORDER    = {100, 130, 170, 255};
constexpr Color DD_ROW_HOVER = {70, 100, 140, 220};
constexpr Color DD_SHORTCUT  = {170, 190, 220, 220};
constexpr Color DD_SEPARATOR = {80, 100, 130, 200};
constexpr Color DD_CHECK     = {180, 230, 180, 255};
constexpr Color DD_SUBARROW  = {180, 200, 230, 230};

// Treat any item whose enabled predicate is null as enabled.
bool itemEnabled(const MenuBar::Item &it, const Inspector &insp) {
  if (it.separator) return false;
  return !it.enabled || it.enabled(insp);
}

// Width that an individual dropdown row occupies horizontally —
// label + shortcut + paddings. Submenu arrow adds a small extra
// gutter.
int rowWidth(const MenuBar::Item &it) {
  int lw = MeasureText(it.label.c_str(), 13);
  int sw = it.shortcut.empty() ? 0 : MeasureText(it.shortcut.c_str(), 13);
  int extra = (it.submenu.empty() && !it.dynamicSubmenu) ? 0 : 14;
  return lw + (sw > 0 ? 22 + sw : 0) + extra;
}

} // anonymous namespace

MenuBar::MenuBar() { buildDefault(); }

void MenuBar::buildDefault() {
  m_menus.clear();

  Menu file;
  file.label = "File";
  file.items.push_back({
      "Open...", "O",
      [](Inspector &i) { i.actionOpenDialog(); },
      nullptr, nullptr, {}, false, nullptr});
  file.items.push_back({
      "Open Recent", "", nullptr, nullptr, nullptr, {}, false,
      // Dynamic submenu — rebuilt each frame from the live MRU list
      // so newly-loaded files show up immediately.
      [](const Inspector &i) -> std::vector<Item> {
        std::vector<Item> out;
        const auto &recents = i.cfg().recentFiles;
        if (recents.empty()) {
          Item empty;
          empty.label = "(no recent files)";
          empty.enabled = [](const Inspector &) { return false; };
          out.push_back(std::move(empty));
        } else {
          for (const auto &p : recents) {
            std::filesystem::path captured = p;
            Item it;
            it.label = p.filename().string();
            it.action = [captured](Inspector &insp) {
              insp.actionOpenPath(captured);
            };
            out.push_back(std::move(it));
          }
        }
        return out;
      }});
  file.items.push_back({"", "", nullptr, nullptr, nullptr, {}, true, nullptr});
  file.items.push_back({
      "Save", "Ctrl+S",
      [](Inspector &i) { i.actionSave(); },
      [](const Inspector &i) { return i.hasMesh() && i.anyDirty(); },
      nullptr, {}, false, nullptr});
  file.items.push_back({
      "Save As...", "Ctrl+Shift+S",
      [](Inspector &i) { i.actionSaveAsDialog(); },
      [](const Inspector &i) { return i.hasMesh(); },
      nullptr, {}, false, nullptr});
  file.items.push_back({
      "Close", "Ctrl+W",
      [](Inspector &i) { i.actionClose(); },
      [](const Inspector &i) { return i.hasMesh(); },
      nullptr, {}, false, nullptr});
  file.items.push_back({"", "", nullptr, nullptr, nullptr, {}, true, nullptr});
  file.items.push_back({
      "Quit", "Q",
      [](Inspector &i) { i.actionQuit(); },
      nullptr, nullptr, {}, false, nullptr});
  m_menus.push_back(std::move(file));

  Menu view;
  view.label = "View";
  view.items.push_back({
      "Frame View", "F",
      [](Inspector &i) { i.actionFrameView(); },
      [](const Inspector &i) { return i.hasMesh(); },
      nullptr, {}, false, nullptr});
  m_menus.push_back(std::move(view));

  Menu tool;
  tool.label = "Tool";
  // Tool items are radio-style. We can't capture the tool index in a
  // static literal because tools are registered at runtime, but
  // Inspector exposes toolCount/toolNameAt — we build the rows here
  // since the registration is fixed across the inspector lifetime
  // (no add/remove of tools after ctor).
  // The Inspector hasn't constructed yet when buildDefault runs, so
  // we use a dynamicSubmenu to rebuild each frame. That also future-
  // proofs against tool-list mutation.
  tool.items.push_back({
      "Switch", "TAB", nullptr, nullptr, nullptr, {}, false,
      [](const Inspector &i) -> std::vector<Item> {
        std::vector<Item> out;
        for (size_t k = 0; k < i.toolCount(); ++k) {
          size_t idx = k;
          Item row;
          row.label = i.toolNameAt(k);
          row.action = [idx](Inspector &insp) {
            insp.actionSwitchTool(idx);
          };
          row.checked = [idx](const Inspector &insp) {
            return insp.currentToolIndex() == idx;
          };
          out.push_back(std::move(row));
        }
        return out;
      }});
  m_menus.push_back(std::move(tool));

  Menu help;
  help.label = "Help";
  help.items.push_back({
      "Controls", "",
      [](Inspector &i) { i.actionToggleControlsHelp(); },
      nullptr, nullptr, {}, false, nullptr});
  m_menus.push_back(std::move(help));
}

Rectangle MenuBar::topRect(int i) const {
  if (i < 0 || i >= static_cast<int>(m_topX.size())) return {0, 0, 0, 0};
  return {m_topX[i], 0.0f, m_topW[i], static_cast<float>(kBarHeight)};
}

Rectangle MenuBar::dropdownRect(int menuIdx,
                                const std::vector<Item> &items) const {
  if (menuIdx < 0 || menuIdx >= static_cast<int>(m_menus.size()))
    return {0, 0, 0, 0};
  int w = 80;
  for (const auto &it : items) {
    if (it.separator) continue;
    int rw = rowWidth(it) + 2 * kRowPadX;
    if (rw > w) w = rw;
  }
  int rows = 0;
  int h = 0;
  for (const auto &it : items) {
    h += it.separator ? 8 : kRowH;
    if (!it.separator) ++rows;
  }
  (void)rows;
  return {m_topX[menuIdx], static_cast<float>(kBarHeight),
          static_cast<float>(w), static_cast<float>(h + 6)};
}

Rectangle MenuBar::submenuRect(Rectangle parentRow,
                               const std::vector<Item> &subItems) const {
  int w = 80;
  for (const auto &it : subItems) {
    if (it.separator) continue;
    int rw = rowWidth(it) + 2 * kRowPadX;
    if (rw > w) w = rw;
  }
  int h = 0;
  for (const auto &it : subItems)
    h += it.separator ? 8 : kRowH;
  return {parentRow.x + parentRow.width, parentRow.y,
          static_cast<float>(w), static_cast<float>(h + 6)};
}

std::vector<MenuBar::Item>
MenuBar::resolveItems(int menuIdx, const Inspector &insp) const {
  if (menuIdx < 0 || menuIdx >= static_cast<int>(m_menus.size())) return {};
  // Top-level static list is used as-is. Dynamic submenus on rows
  // are resolved at render time.
  (void)insp;
  return m_menus[menuIdx].items;
}

std::vector<MenuBar::Item>
MenuBar::resolveSubItems(const Item &parent, const Inspector &insp) const {
  if (parent.dynamicSubmenu) return parent.dynamicSubmenu(insp);
  return parent.submenu;
}

bool MenuBar::handle(Inspector &insp) {
  Vector2 mp = GetMousePosition();
  bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

  // Esc closes any open menu.
  if (m_open >= 0 && IsKeyPressed(KEY_ESCAPE)) {
    m_open = -1; m_openSubRow = -1; m_hoverRow = -1; m_hoverSubRow = -1;
    return true;
  }

  // Refresh m_topX / m_topW each call so the click hit-tests match
  // what the render path drew last frame. Idempotent on size.
  if (m_topX.size() != m_menus.size()) {
    m_topX.assign(m_menus.size(), 0.0f);
    m_topW.assign(m_menus.size(), 0.0f);
  }
  float x = 0.0f;
  for (size_t i = 0; i < m_menus.size(); ++i) {
    int lw = MeasureText(m_menus[i].label.c_str(), kBarTextSize);
    m_topX[i] = x;
    m_topW[i] = static_cast<float>(lw + 2 * kBarPadX);
    x += m_topW[i];
  }

  // Hit-test top-level labels.
  int hoverTop = -1;
  for (size_t i = 0; i < m_menus.size(); ++i) {
    if (CheckCollisionPointRec(mp, topRect(static_cast<int>(i)))) {
      hoverTop = static_cast<int>(i);
      break;
    }
  }

  if (clicked) {
    if (hoverTop >= 0) {
      // Toggle: clicking the open menu's label closes it; clicking
      // another opens that one.
      m_open = (m_open == hoverTop) ? -1 : hoverTop;
      m_openSubRow = -1;
      m_hoverRow = -1;
      m_hoverSubRow = -1;
      return true;
    }
  }

  // If a menu is open, swap-open on hover (Windows / Linux convention
  // — once any dropdown is open, sliding the cursor to a sibling
  // opens that one).
  if (m_open >= 0 && hoverTop >= 0 && hoverTop != m_open) {
    m_open = hoverTop;
    m_openSubRow = -1;
    m_hoverRow = -1;
    m_hoverSubRow = -1;
  }

  if (m_open < 0) {
    // Bar pass-through: click ON the bar but not on a label still
    // consumes the click (otherwise it falls through to the 3D
    // scene and the vertex picker may fire).
    return CheckCollisionPointRec(mp, barRect(GetScreenWidth()));
  }

  // ---------- Dropdown active ----------
  auto items = resolveItems(m_open, insp);
  Rectangle dd = dropdownRect(m_open, items);

  // Find which row the cursor is over.
  m_hoverRow = -1;
  if (CheckCollisionPointRec(mp, dd)) {
    float y = dd.y + 3.0f;
    int rowIdx = 0;
    for (const auto &it : items) {
      float h = it.separator ? 8.0f : static_cast<float>(kRowH);
      if (!it.separator) {
        if (mp.y >= y && mp.y < y + h) {
          m_hoverRow = rowIdx;
          break;
        }
        ++rowIdx;
      } else {
        // separator advances no row counter
      }
      y += h;
    }
  }

  // Hover into a submenu parent opens it; hovering off keeps the
  // last submenu visible only if the cursor is over IT.
  if (m_hoverRow >= 0) {
    // Walk the items list, mapping row index → item index (skipping
    // separators), so we can address the hovered Item.
    int seen = 0;
    int itemIdx = -1;
    for (int k = 0; k < static_cast<int>(items.size()); ++k) {
      if (items[k].separator) continue;
      if (seen == m_hoverRow) { itemIdx = k; break; }
      ++seen;
    }
    if (itemIdx >= 0) {
      const Item &it = items[itemIdx];
      bool hasSub = !it.submenu.empty() || (bool)it.dynamicSubmenu;
      if (hasSub) {
        m_openSubRow = m_hoverRow;
      } else if (m_openSubRow >= 0) {
        // Cursor moved to a sibling without a submenu — close any
        // open submenu so it doesn't linger.
        m_openSubRow = -1;
      }
    }
  }

  // Submenu hover + hit-test, if a submenu is open.
  int subItemIdx = -1;
  std::vector<Item> subItems;
  Rectangle subRect{0, 0, 0, 0};
  if (m_openSubRow >= 0) {
    // Locate the parent row by counting non-separator rows.
    int seen = 0;
    int parentIdx = -1;
    for (int k = 0; k < static_cast<int>(items.size()); ++k) {
      if (items[k].separator) continue;
      if (seen == m_openSubRow) { parentIdx = k; break; }
      ++seen;
    }
    if (parentIdx >= 0) {
      // Compute the row rect to anchor the submenu against.
      float y = dd.y + 3.0f;
      int seen2 = 0;
      Rectangle parentRow{0, 0, 0, 0};
      for (int k = 0; k < parentIdx; ++k) {
        y += items[k].separator ? 8.0f : kRowH;
        if (!items[k].separator) ++seen2;
      }
      parentRow = {dd.x, y, dd.width, static_cast<float>(kRowH)};
      subItems = resolveSubItems(items[parentIdx], insp);
      subRect = submenuRect(parentRow, subItems);

      // Stay-open guard — if the cursor is over the submenu area,
      // do NOT collapse it from the parent-side hover-off logic.
      if (CheckCollisionPointRec(mp, subRect) ||
          CheckCollisionPointRec(mp, parentRow)) {
        // Keep open. Hit-test rows.
        m_hoverSubRow = -1;
        float ys = subRect.y + 3.0f;
        int rowIdx = 0;
        for (const auto &it : subItems) {
          float h = it.separator ? 8.0f : static_cast<float>(kRowH);
          if (!it.separator) {
            if (mp.y >= ys && mp.y < ys + h) {
              m_hoverSubRow = rowIdx;
              subItemIdx = rowIdx;
              break;
            }
            ++rowIdx;
          }
          ys += h;
        }
      }
    }
  }

  if (clicked) {
    // Click outside dropdown + submenu → close everything.
    bool inDD = CheckCollisionPointRec(mp, dd);
    bool inSub = (m_openSubRow >= 0) &&
                 CheckCollisionPointRec(mp, subRect);
    bool inBar = CheckCollisionPointRec(mp, barRect(GetScreenWidth()));
    if (!inDD && !inSub && !inBar) {
      m_open = -1;
      m_openSubRow = -1;
      m_hoverRow = -1;
      m_hoverSubRow = -1;
      return true; // consume — we ate the close click
    }

    // Click on a submenu row → fire its action.
    if (inSub && subItemIdx >= 0) {
      int seen = 0;
      for (int k = 0; k < static_cast<int>(subItems.size()); ++k) {
        if (subItems[k].separator) continue;
        if (seen == subItemIdx) {
          if (itemEnabled(subItems[k], insp) && subItems[k].action) {
            subItems[k].action(insp);
          }
          m_open = -1;
          m_openSubRow = -1;
          m_hoverRow = -1;
          m_hoverSubRow = -1;
          return true;
        }
        ++seen;
      }
    }

    // Click on a dropdown leaf row → fire and close. Submenu-parent
    // rows don't fire (they only host the submenu).
    if (inDD && m_hoverRow >= 0) {
      int seen = 0;
      for (int k = 0; k < static_cast<int>(items.size()); ++k) {
        if (items[k].separator) continue;
        if (seen == m_hoverRow) {
          const Item &it = items[k];
          bool hasSub = !it.submenu.empty() || (bool)it.dynamicSubmenu;
          if (!hasSub && itemEnabled(it, insp) && it.action) {
            it.action(insp);
            m_open = -1;
            m_openSubRow = -1;
            m_hoverRow = -1;
            m_hoverSubRow = -1;
            return true;
          }
          break;
        }
        ++seen;
      }
    }
  }

  // Menu is open → we own all input this frame (except orbit camera
  // which uses RMB and doesn't conflict).
  return true;
}

void MenuBar::render(const Inspector &insp) {
  int sw = GetScreenWidth();

  // Bar background + bottom border.
  DrawRectangle(0, 0, sw, kBarHeight, BAR_BG);
  DrawRectangle(0, kBarHeight, sw, 1, BAR_BORDER);

  // Top-level labels.
  float x = 0.0f;
  for (size_t i = 0; i < m_menus.size(); ++i) {
    int lw = MeasureText(m_menus[i].label.c_str(), kBarTextSize);
    int w = lw + 2 * kBarPadX;
    bool open = (static_cast<int>(i) == m_open);
    Vector2 mp = GetMousePosition();
    Rectangle r{x, 0.0f, static_cast<float>(w),
                static_cast<float>(kBarHeight)};
    bool hover = CheckCollisionPointRec(mp, r);
    if (open) DrawRectangleRec(r, BAR_OPEN_BG);
    else if (hover) DrawRectangleRec(r, BAR_HOVER_BG);
    DrawText(m_menus[i].label.c_str(),
             static_cast<int>(x) + kBarPadX,
             (kBarHeight - kBarTextSize) / 2 + 1, kBarTextSize, BAR_TEXT);
    x += w;
  }

  if (m_open < 0) return;

  // Dropdown panel.
  std::vector<Item> items = resolveItems(m_open, insp);
  Rectangle dd = dropdownRect(m_open, items);
  DrawRectangleRec(dd, DD_BG);
  DrawRectangleLinesEx(dd, 1, DD_BORDER);

  float yRow = dd.y + 3.0f;
  int rowIdx = 0;
  for (size_t k = 0; k < items.size(); ++k) {
    const Item &it = items[k];
    if (it.separator) {
      DrawLine(static_cast<int>(dd.x) + 6,
               static_cast<int>(yRow + 3),
               static_cast<int>(dd.x + dd.width) - 6,
               static_cast<int>(yRow + 3), DD_SEPARATOR);
      yRow += 8;
      continue;
    }
    bool en = itemEnabled(it, insp);
    bool hovered = (rowIdx == m_hoverRow);
    if (hovered && en) {
      DrawRectangle(static_cast<int>(dd.x) + 2,
                    static_cast<int>(yRow),
                    static_cast<int>(dd.width) - 4, kRowH, DD_ROW_HOVER);
    }
    // ✓ marker for radio/checked items.
    if (it.checked && it.checked(insp)) {
      DrawText("✓", static_cast<int>(dd.x) + 6,
               static_cast<int>(yRow) + 4, kRowTextSize, DD_CHECK);
    }
    // Label.
    Color textCol = en ? BAR_TEXT : BAR_DIM;
    DrawText(it.label.c_str(),
             static_cast<int>(dd.x) + kRowPadX + 12,
             static_cast<int>(yRow) + 4, kRowTextSize, textCol);
    // Submenu arrow.
    bool hasSub = !it.submenu.empty() || (bool)it.dynamicSubmenu;
    if (hasSub) {
      DrawText(">",
               static_cast<int>(dd.x + dd.width) - 14,
               static_cast<int>(yRow) + 4, kRowTextSize, DD_SUBARROW);
    } else if (!it.shortcut.empty()) {
      int sw_ = MeasureText(it.shortcut.c_str(), kRowTextSize);
      DrawText(it.shortcut.c_str(),
               static_cast<int>(dd.x + dd.width) - sw_ - kRowPadX,
               static_cast<int>(yRow) + 4, kRowTextSize, DD_SHORTCUT);
    }
    yRow += kRowH;
    ++rowIdx;
  }

  // Submenu panel.
  if (m_openSubRow >= 0) {
    int seen = 0;
    int parentIdx = -1;
    for (int k = 0; k < static_cast<int>(items.size()); ++k) {
      if (items[k].separator) continue;
      if (seen == m_openSubRow) { parentIdx = k; break; }
      ++seen;
    }
    if (parentIdx >= 0) {
      float y = dd.y + 3.0f;
      for (int k = 0; k < parentIdx; ++k) {
        y += items[k].separator ? 8.0f : kRowH;
      }
      Rectangle parentRow{dd.x, y, dd.width, static_cast<float>(kRowH)};
      std::vector<Item> subItems = resolveSubItems(items[parentIdx], insp);
      Rectangle subR = submenuRect(parentRow, subItems);
      DrawRectangleRec(subR, DD_BG);
      DrawRectangleLinesEx(subR, 1, DD_BORDER);

      float ys = subR.y + 3.0f;
      int sRowIdx = 0;
      for (const auto &it : subItems) {
        if (it.separator) {
          DrawLine(static_cast<int>(subR.x) + 6,
                   static_cast<int>(ys + 3),
                   static_cast<int>(subR.x + subR.width) - 6,
                   static_cast<int>(ys + 3), DD_SEPARATOR);
          ys += 8;
          continue;
        }
        bool en = itemEnabled(it, insp);
        bool hovered = (sRowIdx == m_hoverSubRow);
        if (hovered && en) {
          DrawRectangle(static_cast<int>(subR.x) + 2,
                        static_cast<int>(ys),
                        static_cast<int>(subR.width) - 4, kRowH,
                        DD_ROW_HOVER);
        }
        if (it.checked && it.checked(insp)) {
          DrawText("✓", static_cast<int>(subR.x) + 6,
                   static_cast<int>(ys) + 4, kRowTextSize, DD_CHECK);
        }
        Color textCol = en ? BAR_TEXT : BAR_DIM;
        DrawText(it.label.c_str(),
                 static_cast<int>(subR.x) + kRowPadX + 12,
                 static_cast<int>(ys) + 4, kRowTextSize, textCol);
        ys += kRowH;
        ++sRowIdx;
      }
    }
  }
}

} // namespace tsmesh
