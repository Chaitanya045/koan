// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_

#include <string_view>

namespace views {
class View;
}

// Extensions in Zen's single toolbar (issue #73). Chromium's extensions
// container stays in the toolbar, which Koan hides there; its popups and its
// extensions menu point at what is on screen instead.
namespace koan {

// Where the popup of the extension `action_id` (or, for an empty id, the
// extensions menu) points while Koan hides the toolbar: the extension's
// button in the sidebar's URL pill if it is showing, else what the page's
// bubbles point at (GetBubbleAnchorView() in koan_url_bar.h, the pill's site
// icon). Null where the toolbar is there: the anchor is upstream's.
// `container` is the toolbar's extensions container. Called from
// ExtensionsToolbarDesktop::GetReferenceButtonForPopup(),
// GetExtensionsButtonAnchor() and GetPopupArrow().
views::View* GetExtensionsAnchorView(const views::View* container,
                                     std::string_view action_id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_
