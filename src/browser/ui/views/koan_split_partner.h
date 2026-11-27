// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_PARTNER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_PARTNER_H_

#include <cstddef>
#include <optional>
#include <vector>

namespace koan {

// What the split shortcut (Cmd+Option+V / H) knows of a tab in the window's
// tab strip when it looks for a tab to split the active one with.
struct SplitPartnerCandidate {
  // In the active tab's Space: the same tab group (Spaces are tab groups in
  // one strip), or an Essential (pinned, shown in every Space).
  bool in_active_space = false;
  // Pinned in the tab strip: an Essential. Pinned tabs of a Space aren't.
  bool pinned = false;
  // Not shown in the sidebar: IsHiddenTab() (koan_spaces.h), or in a
  // collapsed folder (Zen's visibleTabs leaves both out).
  bool hidden = false;
  bool split = false;
};

// Zen's toggleShortcut() (ZenViewSplitter.mjs): the tab after the active one
// among the visible tabs, wrapping to the first. Here the visible tabs are
// those of the active tab's Space that aren't in a split, and an Essential
// only pairs with an Essential (as before; the split then pins both, see
// docs/ZEN_PARITY.md, "Pinned tabs in a split view"). A tab of another Space
// is never taken. Returns the index into `tabs`, or nothing when the Space
// has no other such tab.
std::optional<size_t> PickSplitPartner(
    const std::vector<SplitPartnerCandidate>& tabs,
    size_t active);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_PARTNER_H_
