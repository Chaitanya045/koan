// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_browser_view_controller.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "base/functional/bind.h"

#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/fullscreen_util_mac.h"
#endif
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_observer.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_agent_indicator.h"
#include "koan/browser/ui/views/koan_command_palette.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_download_animation.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_media_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_tabs_controller.h"
#include "koan/browser/ui/views/koan_tab_activation.h"
#include "koan/browser/ui/views/koan_theme_controller.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/views/koan_welcome.h"
#include "koan/browser/ui/views/koan_window_drag.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/class_property.h"
#include "ui/views/widget/widget.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(koan::KoanBrowserViewController*)

namespace koan {

// Zen has no Google AI: Chromium's @gemini and @aimode search shortcuts (the
// starter pack) are kept switched off, once the search engines have loaded
// and whenever they change, so Settings > Search engine can't switch them back
// on. (Audit O6, .agent/notes/upstream/omnibox-lockdown.md.)
class AiStarterPackFilter : public TemplateURLServiceObserver {
 public:
  explicit AiStarterPackFilter(TemplateURLService* service)
      : service_(service) {
    observation_.Observe(service);
    if (service->loaded()) {
      ScheduleApply();
    }
  }

  // TemplateURLServiceObserver (also called once the service has loaded):
  void OnTemplateURLServiceChanged() override { ScheduleApply(); }

 private:
  // Not from the notification itself: deactivating a search engine runs the
  // service's own Update(), which notifies and touches other observer lists
  // that forbid re-entrancy (a CHECK failure at startup when done inside the
  // "loaded" notification).
  void ScheduleApply() {
    if (apply_scheduled_) {
      return;
    }
    apply_scheduled_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&AiStarterPackFilter::Apply,
                                  weak_factory_.GetWeakPtr()));
  }

  void Apply() {
    apply_scheduled_ = false;
    using template_url_starter_pack_data::StarterPackId;
    // Deactivating one notifies the observers again, which schedules another
    // pass; that pass finds nothing left to do.
    std::vector<raw_ptr<TemplateURL>> to_deactivate;
    for (TemplateURL* url : service_->GetTemplateURLs()) {
      const StarterPackId id = url->starter_pack_id();
      if ((id == StarterPackId::kGemini || id == StarterPackId::kAiMode) &&
          url->is_active() != TemplateURLData::ActiveStatus::kFalse) {
        to_deactivate.emplace_back(url);
      }
    }
    for (TemplateURL* url : to_deactivate) {
      service_->SetIsActiveTemplateURL(url, /*is_active=*/false);
    }
  }

  const raw_ptr<TemplateURLService> service_;
  bool apply_scheduled_ = false;
  base::ScopedObservation<TemplateURLService, TemplateURLServiceObserver>
      observation_{this};
  base::WeakPtrFactory<AiStarterPackFilter> weak_factory_{this};
};

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(KoanBrowserViewController,
                                   kKoanBrowserViewControllerKey)

int g_live_count = 0;

}  // namespace

KoanBrowserViewController::KoanBrowserViewController(BrowserView* browser_view)
    : browser_view_(browser_view),
      vertical_tabs_(std::make_unique<KoanVerticalTabsPolicy>(browser_view)),
      compact_mode_(std::make_unique<KoanCompactModeController>(browser_view)),
      toast_(std::make_unique<KoanToast>(browser_view)),
      sidebar_tabs_(std::make_unique<KoanSidebarTabsController>(browser_view)),
      url_bar_(std::make_unique<KoanUrlBarController>(browser_view,
                                                      compact_mode_.get())),
      workspaces_(std::make_unique<KoanWorkspaceController>(browser_view)),
      pinned_tabs_(std::make_unique<KoanPinnedTabController>(browser_view)),
      theme_(std::make_unique<KoanThemeController>(browser_view)),
      glance_(std::make_unique<KoanGlanceController>(browser_view)),
      library_(std::make_unique<KoanLibraryController>(browser_view)),
      tab_activation_(std::make_unique<KoanTabActivationTracker>(
          browser_view->browser()->GetTabStripModel())),
      media_(std::make_unique<KoanMediaController>(browser_view)),
      download_animation_(
          std::make_unique<KoanDownloadAnimation>(browser_view)),
      // Page presses are where downloads fly from. Unretained: the
      // animation outlives the drag controller (declared before it).
      window_drag_(std::make_unique<KoanWindowDragController>(
          browser_view,
          base::BindRepeating(&KoanDownloadAnimation::OnPagePressed,
                              base::Unretained(download_animation_.get())))),
      welcome_(std::make_unique<KoanWelcome>(browser_view)),
      agent_indicator_(std::make_unique<KoanAgentIndicator>(browser_view)),
      shortcuts_(std::make_unique<KoanShortcutController>(browser_view, this)),
      command_palette_(std::make_unique<KoanCommandPalette>(this)) {
  ++g_live_count;
  if (TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(
          browser_view->browser()->GetProfile())) {
    ai_starter_pack_filter_ = std::make_unique<AiStarterPackFilter>(service);
  }
  observations_.AddObservation(browser_view);
  if (MultiContentsView* contents = browser_view->multi_contents_view()) {
    observations_.AddObservation(contents);
  }
  VLOG(1) << "Koan: window created (" << g_live_count << " open)";
  SetUpTestWindowData(this);
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
    controller->TearDown();
  }
  browser_view->ClearProperty(kKoanBrowserViewControllerKey);
}

void KoanBrowserViewController::TearDown() {
  if (torn_down_) {
    return;
  }
  torn_down_ = true;
  VLOG(1) << "Koan: window torn down"
          << (browser_view_->browser_widget() ? "" : " (widget gone)");
  widget_observation_.Reset();
  observations_.RemoveAllObservations();
  // First: from here on nothing touches the sidebar's state.
  vertical_tabs_->OnBrowserViewDestroying();
  command_palette_->OnBrowserViewDestroying();
  shortcuts_->OnBrowserViewDestroying();
  compact_mode_->OnBrowserViewDestroying();
  sidebar_tabs_->OnBrowserViewDestroying();
  workspaces_->OnBrowserViewDestroying();
  pinned_tabs_->OnBrowserViewDestroying();
  theme_->OnBrowserViewDestroying();
  glance_->OnBrowserViewDestroying();
  library_->OnBrowserViewDestroying();
  toast_->Close();
  media_->OnBrowserViewDestroying();
  download_animation_->OnBrowserViewDestroying();
  window_drag_->OnBrowserViewDestroying();
  welcome_->OnBrowserViewDestroying();
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
    if (views::Widget* widget = browser_view_->GetWidget();
        widget && !widget_observation_.IsObserving()) {
      widget_observation_.Observe(widget);
    }
    vertical_tabs_->OnAddedToWidget();
    compact_mode_->OnAddedToWidget();
    sidebar_tabs_->OnAddedToWidget();
    workspaces_->OnAddedToWidget();
    theme_->OnAddedToWidget();
    shortcuts_->OnAddedToWidget();
    command_palette_->OnAddedToWidget();
    // After the workspace switcher: the media cards go above it.
    media_->OnAddedToWidget();
    download_animation_->OnAddedToWidget();
    welcome_->OnAddedToWidget();
    // After the welcome: both post a task, and the URL bar's must run after
    // the welcome's (FIFO) so it sees whether the welcome is showing.
    url_bar_->OnAddedToWidget();
  }
}

void KoanBrowserViewController::OnViewBoundsChanged(
    views::View* observed_view) {
  if (observed_view == browser_view_->multi_contents_view()) {
    UpdateContentCard();
  }
  compact_mode_->OnBrowserViewLayout();
  glance_->OnBrowserViewLayout();
  library_->OnBrowserViewLayout();
  toast_->OnBrowserViewLayout();
  media_->OnBrowserViewLayout();
  download_animation_->OnBrowserViewLayout();
  welcome_->OnBrowserViewLayout();
  agent_indicator_->OnBrowserViewLayout();
}

void KoanBrowserViewController::OnWidgetDestroying(views::Widget* widget) {
  TearDown();
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

bool IsWindowFullscreen(const BrowserView* browser_view) {
  return browser_view && browser_view->browser_widget() &&
         browser_view->IsFullscreen();
}

bool IsContentFullscreen(const BrowserView* browser_view) {
#if BUILDFLAG(IS_MAC)
  return IsWindowFullscreen(browser_view) &&
         fullscreen_utils::IsInContentFullscreen(browser_view->browser());
#else
  return false;
#endif
}

bool IsCompactModeActive(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->hides_sidebar();
}

bool KeepsSidebarNav(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->keeps_sidebar_nav();
}

bool IsToolbarHidden(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->hides_toolbar();
}

bool HidesFullscreenTopChrome(const views::View* browser_view) {
#if BUILDFLAG(IS_MAC)
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  return view && IsWindowFullscreen(view) && IsToolbarHidden(view);
#else
  return false;
#endif
}

bool IsCaptionStripCollapsed(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  return controller && controller->compact_mode()->caption_strip_collapsed();
}

bool IsSidebarOnRight(const views::View* view) {
  // Also takes a view inside the window (e.g. in the sidebar).
  const auto* browser_view = views::AsViewClass<BrowserView>(view);
  if (!browser_view && view && view->GetWidget()) {
    browser_view =
        views::AsViewClass<BrowserView>(view->GetWidget()->client_view());
  }
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  // Only a normal window with Koan's sidebar (vertical tabs) has a side to
  // be on; others (horizontal tabs, popups, apps) aren't mirrored.
  return controller && controller->compact_mode()->tabs_on_right() &&
         features::IsSidebarEnabled() && browser_view->GetIsNormalType() &&
         browser_view->ShouldDrawVerticalTabStrip();
}

int GetHiddenToolbarRowHeight(int caption_buttons_height) {
#if BUILDFLAG(IS_MAC)
  return std::max(caption_buttons_height, kMacSidebarTopRowHeight);
#else
  return caption_buttons_height;
#endif
}

int GetRevealedToolbarSidebarInset(const views::View* browser_view) {
  const auto* view = views::AsViewClass<BrowserView>(browser_view);
  const KoanBrowserViewController* controller =
      view ? KoanBrowserViewController::From(view) : nullptr;
  const views::View* region = view ? view->tab_strip_view() : nullptr;
  return controller &&
                 controller->compact_mode()->reveals_toolbar_beside_sidebar() &&
                 region && region->GetVisible()
             ? region->width()
             : 0;
}

}  // namespace koan
