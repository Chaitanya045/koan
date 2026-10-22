// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class BrowserWindowInterface;

namespace views {
class SizeBounds;
class View;
struct ProposedLayout;
}  // namespace views

namespace koan {

class KoanFolderController;

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

// Session hooks: the folders of `browser` as window extra data, written when
// the session file is rebuilt; and restoring them once session restore added
// the window's tabs (from `first_tab_index` on).
std::vector<std::pair<std::string, std::string>> GetFolderSessionData(
    BrowserWindowInterface* browser);
void RestoreFolderSessionData(
    BrowserWindowInterface* browser,
    int first_tab_index,
    const std::map<std::string, std::string>& extra_data);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_FOLDERS_H_
