// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_browser_view_controller.h"

#include <memory>

#include "base/logging.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "koan/browser/ui/views/koan_command_palette.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_sidebar_tabs_controller.h"
#include "koan/browser/ui/views/koan_tab_activation.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/class_property.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::KoanBrowserViewController*)

namespace koan {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(KoanBrowserViewController,
                                   kKoanBrowserViewControllerKey)

int g_live_count = 0;

}  // namespace

KoanBrowserViewController::KoanBrowserViewController(BrowserView* browser_view)
    : browser_view_(browser_view),
      compact_mode_(std::make_unique<KoanCompactModeController>(browser_view)),
      toast_(std::make_unique<KoanToast>(browser_view)),
      sidebar_tabs_(std::make_unique<KoanSidebarTabsController>(browser_view)),
      workspaces_(std::make_unique<KoanWorkspaceController>(browser_view)),
      glance_(std::make_unique<KoanGlanceController>(browser_view)),
      tab_activation_(std::make_unique<KoanTabActivationTracker>(
          browser_view->browser()->GetTabStripModel())),
      shortcuts_(std::make_unique<KoanShortcutController>(browser_view, this)),
      command_palette_(std::make_unique<KoanCommandPalette>(this)) {
  ++g_live_count;
  observations_.AddObservation(browser_view);
  if (MultiContentsView* contents = browser_view->multi_contents_view()) {
    observations_.AddObservation(contents);
  }
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
  if (KoanBrowserViewController* controller = From(browser_view)) {
    controller->command_palette_->OnBrowserViewDestroying();
    controller->shortcuts_->OnBrowserViewDestroying();
    controller->compact_mode_->OnBrowserViewDestroying();
    controller->sidebar_tabs_->OnBrowserViewDestroying();
    controller->workspaces_->OnBrowserViewDestroying();
    controller->glance_->OnBrowserViewDestroying();
    controller->toast_->Close();
  }
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
  if (observed_view == browser_view_) {
    VLOG(1) << "Koan: window attached to widget";
    compact_mode_->OnAddedToWidget();
    sidebar_tabs_->OnAddedToWidget();
    workspaces_->OnAddedToWidget();
    shortcuts_->OnAddedToWidget();
    command_palette_->OnAddedToWidget();
  }
}

void KoanBrowserViewController::OnViewBoundsChanged(
    views::View* observed_view) {
  if (observed_view == browser_view_->multi_contents_view()) {
    UpdateContentCard();
  }
  compact_mode_->OnBrowserViewLayout();
  glance_->OnBrowserViewLayout();
  toast_->OnBrowserViewLayout();
}

void KoanBrowserViewController::UpdateContentCard() {
  MultiContentsView* contents = browser_view_->multi_contents_view();
  if (!contents || contents->IsInSplitView()) {
    return;
  }
  // MultiContentsView applies the same radii on tab and split changes (see
  // the hooks in multi_contents_view.cc); this covers layout-only changes
  // such as fullscreen or switching the tab strip mode.
  contents->GetActiveContentsContainerView()->SetRoundedCorners(
      ActiveContentsRadii(browser_view_, contents->GetBackgroundRadii()));
}

bool IsCompactModeActive(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->hides_sidebar();
}

bool IsToolbarHidden(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->hides_toolbar();
}

bool IsCaptionStripCollapsed(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->caption_strip_collapsed();
}

}  // namespace koan
