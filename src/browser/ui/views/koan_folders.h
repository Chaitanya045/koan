// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_

#include <memory>
#include <string>

#include "base/memory/raw_ref.h"

class BrowserWindowInterface;
class TabStripModel;

namespace views {
class SizeBounds;
class View;
struct ProposedLayout;
}  // namespace views

namespace koan {

struct TabMenuContext;

// Hooks for Zen-style tab folders (see koan_folder_controller.h). Folders are
// Koan's own model; upstream draws the tabs, Koan adds a row per folder.

// The one hook in a Space's vertical layout (TabGroupViewVerticalLayout):
// Koan's rows among the group's tabs, in Zen's order. The Space's pinned
// section is its pinned tabs, then its folders (a row each, before the
// folder's tabs, which are pinned too); then the separator with Clear and
// the "+ New Tab" row (koan_sidebar_tabs.h); then the normal tabs. Walk the
// group's children in order: Place() adds the rows that go before `child`
// to `layout` at `y` (advancing it) and says how `child` is shown; Finish()
// adds the rows left over (empty folders, the separator when every tab is
// pinned). Folder tabs are indented 14px per level and hidden while their
// folder is collapsed.
class SpaceGroupRows {
 public:
  struct Slot {
    // Extra leading inset: 14px per folder level, like Zen.
    int indent = 0;
    // Inside a collapsed folder: lay the child out with zero height.
    bool hidden = false;
  };

  // `collapsed`: upstream's strip is collapsed (compact mode hides the
  // sidebar), for the separator rows. `tab_padding`: the gap after each tab.
  SpaceGroupRows(const views::View& tab_group_view,
                 bool collapsed,
                 int tab_padding);
  SpaceGroupRows(const SpaceGroupRows&) = delete;
  SpaceGroupRows& operator=(const SpaceGroupRows&) = delete;
  ~SpaceGroupRows();

  Slot Place(const views::View& child,
             int x,
             const views::SizeBounds& size_bounds,
             int& y,
             views::ProposedLayout& layout);
  // At the x of the last child placed.
  void Finish(const views::SizeBounds& size_bounds,
              int& y,
              views::ProposedLayout& layout);

  // The visibility for a child with `slot`, given what upstream wants.
  // Hidden children stay visible while they animate closed.
  bool IsVisible(const Slot& slot, bool upstream_visible) const;

 private:
  class FolderRows;

  const raw_ref<const views::View> tab_group_view_;
  const bool collapsed_;
  const int tab_padding_;
  int x_ = 0;
  std::unique_ptr<FolderRows> folders_;
};

// Whether `view` is a folder's row, part of its Space's pinned section.
bool IsFolderRowView(const views::View* view);

// The tab context menu's "Move to Folder" (a TabMenuFeature, see
// koan_tab_menu.h).
void AddFolderTabMenuItems(TabMenuContext& context);
bool IsFolderTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                   int index,
                                   int command_id);
bool ExecuteFolderTabMenuCommand(TabStripModel* tab_strip,
                                 int index,
                                 int command_id);

// Folders' key in the session data (koan_session_data.h): per window, the
// folders (names, collapsed, nesting, Space); per tab, its folder. And their
// WindowRestoreHook, which brings the window's folders back before its tabs
// come back and join them.
inline constexpr char kFolderSessionKey[] = "koan.folders";
void OnRestoringFolders(BrowserWindowInterface* browser,
                        const std::string& data,
                        bool from_session_restore);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_
