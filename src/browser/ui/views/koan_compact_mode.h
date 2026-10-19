// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_

namespace views {
class View;
}

namespace koan {

// Whether compact mode is applied to the window of `browser_view` (a
// BrowserView). When true, the vertical tab strip takes no layout space (it
// only appears as a hover overlay) and the toolbar floats above the page.
// Called from BrowserViewTabbedLayoutImpl.
bool IsCompactModeActive(const views::View* browser_view);

// Whether the top toolbar is hidden (compact mode or single toolbar), so the
// page card reaches the top of the window.
bool IsToolbarHidden(const views::View* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COMPACT_MODE_H_
