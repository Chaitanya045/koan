// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TAB_VIEWS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TAB_VIEWS_H_

namespace tabs {
class TabInterface;
}

namespace views {
class View;
}

namespace koan {

// The sidebar row of `tab`: refreshes Zen's reset button ("Back to pinned
// url") after the tab's pinned state or URL changed. No-op without a row.
void UpdateTabRow(const tabs::TabInterface* tab);

// Whether `tab`'s row shows a label to rename: not an Essential, not in a
// collapsed sidebar.
bool CanRenameTabRow(const tabs::TabInterface* tab);
// Starts editing `tab`'s label in its row (Zen's inline rename). Returns
// false if the row can't.
bool StartRenamingTabRow(const tabs::TabInterface* tab);

// The view of `tab`'s row, to anchor bubbles to; null without a row.
views::View* GetTabRowView(const tabs::TabInterface* tab);

// Shows Zen's icon picker for `tab` (a grid of emoji and a field for any
// other one), anchored to `anchor`.
void ShowTabIconPicker(views::View* anchor, tabs::TabInterface* tab);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TAB_VIEWS_H_
