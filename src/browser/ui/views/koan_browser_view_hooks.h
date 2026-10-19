// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_HOOKS_H_
