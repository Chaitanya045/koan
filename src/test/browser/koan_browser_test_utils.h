// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_
#define KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_

#include <optional>
#include <string>
#include <vector>

#include "components/tab_groups/tab_group_id.h"

class Browser;
class TabStripModel;

namespace koan {

class KoanWorkspaceController;
class KoanPinnedTabController;

// Koan's controllers of `browser`'s window, or null if it has none (a
// window without Koan's UI).
KoanWorkspaceController* GetSpaces(Browser* browser);
KoanPinnedTabController* GetPinnedTabs(Browser* browser);

// Waits until the window has `count` Spaces: a window always shows one, which
// Koan creates once the window is set up. Returns false on a timeout.
[[nodiscard]] bool WaitForSpaceCount(Browser* browser, size_t count);

// The names of the window's Spaces in sidebar order.
std::vector<std::u16string> SpaceNames(Browser* browser);

// The indices of the tabs of `space` in the tab strip.
std::vector<int> TabsOfSpace(TabStripModel* tab_strip,
                             const tab_groups::TabGroupId& space);

}  // namespace koan

#endif  // KOAN_TEST_BROWSER_KOAN_BROWSER_TEST_UTILS_H_
