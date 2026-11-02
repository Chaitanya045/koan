// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_

class BrowserView;

namespace koan {

// Called at the end of the BrowserView constructor, once all child views
// exist. The widget is not attached yet.
void OnBrowserViewCreated(BrowserView* browser_view);

// Called first thing in the BrowserView destructor, while child views and
// the Browser are still alive.
void OnBrowserViewDestroying(BrowserView* browser_view);

// Called when a drag of the vertical tab strip's resize area collapsed an
// expanded strip. Like Zen, Koan hides the sidebar in compact mode instead,
// with a one-time toast telling how to bring it back.
void OnSidebarDragCollapsed(BrowserView* browser_view);

// Whether Koan plays Zen's download animation in this window; Chrome's own
// "download started" animation is skipped then (it would fly from the
// toolbar's downloads button, which the single toolbar hides).
bool ShowsDownloadAnimation(BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_
