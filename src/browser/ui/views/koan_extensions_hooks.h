// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_

#include <string_view>

#include "base/functional/callback_forward.h"

class BrowserWindowInterface;

namespace content {
class RenderFrameHost;
struct ContextMenuParams;
}  // namespace content

namespace extensions {
class MenuItem;
}

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

// A popup of an extension closed: the sidebar's buttons that it kept shown
// may hide (there is no observer for it, and polling for it wakes the
// browser up while it is idle). Called from
// ExtensionsToolbarDesktop::OnPopupClosed(), after the popup's state is gone.
void OnExtensionPopupClosed(const views::View* container);

// Whether the toolbar shows no button for the extension `action_id` in
// `browser`: the active tab's Space profile has it off (#473,
// koan_container_extensions.h). Called from
// ExtensionsToolbarDesktop::UpdateIconVisibility(), which UpdateAllIcons()
// runs for every icon on each tab switch. Defined in koan_space_extensions.cc.
bool HidesExtensionAction(BrowserWindowInterface* browser,
                          std::string_view action_id);

// The filter of the page context menu's extension items: upstream's
// extensions::context_menu_helpers::MenuItemMatchesParams(`params`), and no
// item of an extension that is off in `frame`'s Space profile (#473), so it
// gets no onClicked with the page's address, selection or tab, and no
// activeTab. The list is taken now, for the menu's lifetime. Called from
// RenderViewContextMenu's constructor. Defined in koan_space_extensions.cc.
base::RepeatingCallback<bool(const extensions::MenuItem*)>
ExtensionMenuItemFilter(content::RenderFrameHost& frame,
                        const content::ContextMenuParams& params);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_EXTENSIONS_HOOKS_H_
