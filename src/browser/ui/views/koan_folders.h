// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_

#include <memory>
#include <string>

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

// Used by the vertical layouts of a Space (TabGroupViewLayout) and of the
// ungrouped tabs (UnpinnedTabContainerViewLayout). Walk the container's
// children in order: Place() adds the rows of the folders that go before
// `child` to `layout` at `y` (advancing it) and says how `child` is shown;
// Finish() adds the rows left over (empty folders at the end). Does nothing
// in containers without folders.
class FolderRows {
 public:
  struct Slot {
    // Extra leading inset: 14px per folder level, like Zen.
    int indent = 0;
    // Inside a collapsed folder: lay the child out with zero height.
    bool hidden = false;
  };

  explicit FolderRows(const views::View& container);
  FolderRows(const FolderRows&) = delete;
  FolderRows& operator=(const FolderRows&) = delete;
  ~FolderRows();

  Slot Place(const views::View& child,
             int x,
             const views::SizeBounds& size_bounds,
             int& y,
             views::ProposedLayout& layout);
  void Finish(int x,
              const views::SizeBounds& size_bounds,
              int& y,
              views::ProposedLayout& layout);

  // The visibility for a child with `slot`, given what upstream wants.
  // Hidden children stay visible while they animate closed.
  bool IsVisible(const Slot& slot, bool upstream_visible) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

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
