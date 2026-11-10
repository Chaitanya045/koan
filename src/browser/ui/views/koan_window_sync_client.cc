// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_sync_client.h"

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/core/session_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/views/widget/widget.h"

namespace koan {

KoanWindowSyncClient::KoanWindowSyncClient(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanWindowSyncClient::~KoanWindowSyncClient() {
  OnBrowserViewDestroying();
}

// static
KoanWindowSyncClient* KoanWindowSyncClient::From(
    const BrowserView* browser_view) {
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  KoanWindowSyncClient* client =
      controller ? controller->window_sync() : nullptr;
  return client && client->is_synced() ? client : nullptr;
}

void KoanWindowSyncClient::OnAddedToWidget() {
  if (sync_) {
    return;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  // Zen syncs windows with Spaces only (gZenWorkspaces.
  // privateWindowOrDisabled is never synced).
  if (!controller || !controller->workspaces()->IsEnabled() ||
      !KoanWindowSync::ShouldSync(browser_view_)) {
    return;
  }
  sync_ = KoanWindowSync::ForProfile(browser_view_->GetProfile());
  if (!sync_) {
    return;
  }
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget_observation_.Observe(widget);
  }
  sync_->AddWindow(this);
  if (widget_observation_.IsObserving() &&
      widget_observation_.GetSource()->IsActive()) {
    sync_->OnWindowActivated(this);
  }
}

void KoanWindowSyncClient::OnBrowserViewDestroying() {
  widget_observation_.Reset();
  if (sync_) {
    sync_->RemoveWindow(this);
    sync_ = nullptr;
  }
}

bool KoanWindowSyncClient::IsApplying() const {
  return sync_ && sync_->IsApplying(this);
}

KoanWindowSync::WindowId KoanWindowSyncClient::window_id() const {
  return browser_view_->browser()->GetSessionID().id();
}

void KoanWindowSyncClient::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  if (active && sync_) {
    sync_->OnWindowActivated(this);
  }
}

void KoanWindowSyncClient::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

}  // namespace koan
