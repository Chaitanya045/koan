// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_browser_view_controller.h"

#include <memory>

#include "base/logging.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/base/class_property.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::KoanBrowserViewController*)

namespace koan {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(KoanBrowserViewController,
                                   kKoanBrowserViewControllerKey)

int g_live_count = 0;

}  // namespace

KoanBrowserViewController::KoanBrowserViewController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  ++g_live_count;
  observation_.Observe(browser_view);
  VLOG(1) << "Koan: window created (" << g_live_count << " open)";
}

KoanBrowserViewController::~KoanBrowserViewController() {
  --g_live_count;
  VLOG(1) << "Koan: window destroyed (" << g_live_count << " open)";
}

// static
void KoanBrowserViewController::CreateFor(BrowserView* browser_view) {
  CHECK(!From(browser_view));
  browser_view->SetProperty(
      kKoanBrowserViewControllerKey,
      std::make_unique<KoanBrowserViewController>(browser_view));
}

// static
void KoanBrowserViewController::DestroyFor(BrowserView* browser_view) {
  browser_view->ClearProperty(kKoanBrowserViewControllerKey);
}

// static
KoanBrowserViewController* KoanBrowserViewController::From(
    const BrowserView* browser_view) {
  return browser_view->GetProperty(kKoanBrowserViewControllerKey);
}

// static
int KoanBrowserViewController::live_count() {
  return g_live_count;
}

void KoanBrowserViewController::OnViewAddedToWidget(
    views::View* observed_view) {
  VLOG(1) << "Koan: window attached to widget";
}

}  // namespace koan
