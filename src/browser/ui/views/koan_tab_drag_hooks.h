// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_HOOKS_H_

// Hooks for upstream's tab drag (TabDragController), called from
// chrome/browser/ui/views/tabs/dragging/ (see patches/). Declarations only,
// so upstream targets can depend on them cheaply; the definitions live in
// //koan/browser/ui/views:impl (koan_tab_drag.cc), linked through
// //chrome/browser:browser.

namespace views {
class Widget;
}  // namespace views

namespace koan {

// Whether tabs dragged in the window of `widget` stay in it when the pointer
// finds no tab strip to take them (out of the sidebar, over the page, out of
// the window), rather than tearing off into a new window: true for Koan's
// sidebar (Zen's tabs never leave for a window of their own in Koan, by the
// user's choice). Dragged over another window's sidebar they still move
// there, and a drag of every tab of the window still moves the window.
// Called from TabDragController::ContinueDragging().
bool KeepsTabDragInWindow(views::Widget* widget);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_DRAG_HOOKS_H_
