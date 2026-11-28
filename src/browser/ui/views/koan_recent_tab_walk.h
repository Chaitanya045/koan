// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_RECENT_TAB_WALK_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_RECENT_TAB_WALK_H_

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace koan {

// The walk of Ctrl+Tab in recently used order, as Firefox's ctrlTab that Zen
// keeps (browser-ctrlTab.js): the list as it was at the walk's first press,
// and where in it the walk is. Each further press while Ctrl is held steps on
// through the same list, and letting go of Ctrl ends the walk, so the tab it
// reached becomes the most recent one for the next. `Tab` is a tab's handle
// (tabs::TabHandle in KoanShortcutController); pure, for the unit tests.
template <typename Tab>
class RecentTabWalk {
 public:
  // Whether a press continues the walk: one is going, Ctrl has been held
  // since its last press, and the tab it reached is still the active one.
  bool Continues(const std::optional<Tab>& active, bool control_down) const {
    return !tabs_.empty() && control_down && active && tabs_[index_] == *active;
  }

  // Starts a walk over `tabs`, most recently used first: the active tab first.
  void Start(std::vector<Tab> tabs) {
    tabs_ = std::move(tabs);
    index_ = 0;
  }

  // Steps once round the list, forward (to less recently used tabs) or back,
  // passing over the tabs for which `exists(tab)` is false (closed since the
  // walk began), and returns the tab reached; the first one of the list
  // comes after the last. Nothing when no tab of the list is left but the one
  // the walk is at.
  template <typename Exists>
  std::optional<Tab> Step(bool forward, Exists exists) {
    const size_t count = tabs_.size();
    for (size_t step = 0; step + 1 < count; ++step) {
      index_ = (index_ + (forward ? 1 : count - 1)) % count;
      if (exists(tabs_[index_])) {
        return tabs_[index_];
      }
    }
    return std::nullopt;
  }

  // Ctrl was let go of.
  void End() {
    tabs_.clear();
    index_ = 0;
  }

  bool IsWalking() const { return !tabs_.empty(); }

 private:
  std::vector<Tab> tabs_;
  size_t index_ = 0;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_RECENT_TAB_WALK_H_
