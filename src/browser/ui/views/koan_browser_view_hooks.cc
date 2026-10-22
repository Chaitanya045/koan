// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_browser_view_hooks.h"

#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_download_animation.h"

namespace koan {

void OnBrowserViewCreated(BrowserView* browser_view) {
  KoanBrowserViewController::CreateFor(browser_view);
}

void OnBrowserViewDestroying(BrowserView* browser_view) {
  KoanBrowserViewController::DestroyFor(browser_view);
}

void OnSidebarDragCollapsed(BrowserView* browser_view) {
  if (auto* controller = KoanBrowserViewController::From(browser_view)) {
    controller->compact_mode()->OnSidebarDragCollapsed();
  }
}

bool ShowsDownloadAnimation(BrowserView* browser_view) {
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->download_animation()->IsEnabled();
}

}  // namespace koan
