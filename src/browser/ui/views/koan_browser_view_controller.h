// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;

namespace koan {

class KoanAgentIndicator;
class AiStarterPackFilter;
class KoanCommandPalette;
class KoanCompactModeController;
class KoanDownloadAnimation;
class KoanGlanceController;
class KoanPinnedTabController;
class KoanLibraryController;
class KoanMediaController;
class KoanShortcutController;
class KoanSidebarTabsController;
class KoanTabActivationTracker;
class KoanToast;
class KoanUrlBarController;
class KoanVerticalTabsPolicy;
class KoanWelcome;
class KoanWindowDragController;
class KoanWorkspaceController;

// Per-window Koan state and behaviour. Owned by its BrowserView through a
// ui::ClassProperty; created and destroyed by the hooks in
// koan_browser_view_hooks.h. Compact mode and sidebar styling hang off this.
//
// Teardown runs once, at the first of:
// - the window's widget being destroyed (OnWidgetDestroying()), the usual
//   case: the widget, the widgets it owns (Koan's overlays, bubbles) and the
//   browser's window features are all still there;
// - ~BrowserView (DestroyFor()), for a widget deleted without its native
//   window closing first. By then BrowserView::DeleteBrowserWindow() has
//   nulled browser_widget(), the owned widgets are closed and the browser's
//   window features are torn down (BrowserWidget::~BrowserWidget()): the #153
//   crash came from UI work at that point.
class KoanBrowserViewController : public views::ViewObserver,
                                  public views::WidgetObserver {
 public:
  explicit KoanBrowserViewController(BrowserView* browser_view);
  KoanBrowserViewController(const KoanBrowserViewController&) = delete;
  KoanBrowserViewController& operator=(const KoanBrowserViewController&) =
      delete;
  ~KoanBrowserViewController() override;

  static void CreateFor(BrowserView* browser_view);
  static void DestroyFor(BrowserView* browser_view);

  // Returns null for a BrowserView that has no controller.
  static KoanBrowserViewController* From(const BrowserView* browser_view);

  BrowserView* browser_view() const { return browser_view_; }
  KoanCompactModeController* compact_mode() const {
    return compact_mode_.get();
  }
  // What the sidebar's state machine may do in this window.
  KoanVerticalTabsPolicy* vertical_tabs() const { return vertical_tabs_.get(); }
  KoanToast* toast() const { return toast_.get(); }
  KoanUrlBarController* url_bar() const { return url_bar_.get(); }
  KoanWorkspaceController* workspaces() const { return workspaces_.get(); }
  KoanSidebarTabsController* sidebar_tabs() const {
    return sidebar_tabs_.get();
  }
  KoanPinnedTabController* pinned_tabs() const { return pinned_tabs_.get(); }
  KoanGlanceController* glance() const { return glance_.get(); }
  KoanLibraryController* library() const { return library_.get(); }
  KoanShortcutController* shortcuts() const { return shortcuts_.get(); }
  KoanMediaController* media() const { return media_.get(); }
  KoanDownloadAnimation* download_animation() const {
    return download_animation_.get();
  }
  KoanWelcome* welcome() const { return welcome_.get(); }
  KoanCommandPalette* command_palette() const { return command_palette_.get(); }
  // Koan features' data for this window's session (koan_session_data.h).
  std::map<std::string, std::string>& session_data() { return session_data_; }

  base::WeakPtr<KoanBrowserViewController> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Number of live controllers, i.e. open browser windows.
  static int live_count();

 private:
  // views::ViewObserver:
  void OnViewAddedToWidget(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // Lets every controller go of the window's UI, once. Nothing is forwarded
  // to the controllers afterwards.
  void TearDown();

  // Rounds the content card's corners when it is shown, restores upstream's
  // corners otherwise. Runs after each layout of the contents area, since the
  // card depends on tab strip mode and fullscreen state.
  void UpdateContentCard();

  const raw_ptr<BrowserView> browser_view_;
  // First: compact mode and the rest ask it to change the sidebar's state.
  std::unique_ptr<KoanVerticalTabsPolicy> vertical_tabs_;
  std::unique_ptr<KoanCompactModeController> compact_mode_;
  std::unique_ptr<KoanToast> toast_;
  std::unique_ptr<KoanSidebarTabsController> sidebar_tabs_;
  // After compact_mode_, which it works with.
  std::unique_ptr<KoanUrlBarController> url_bar_;
  std::unique_ptr<KoanWorkspaceController> workspaces_;
  std::unique_ptr<KoanPinnedTabController> pinned_tabs_;
  std::unique_ptr<KoanGlanceController> glance_;
  std::unique_ptr<KoanLibraryController> library_;
  std::unique_ptr<KoanTabActivationTracker> tab_activation_;
  std::unique_ptr<KoanMediaController> media_;
  std::unique_ptr<KoanDownloadAnimation> download_animation_;
  std::unique_ptr<KoanWindowDragController> window_drag_;
  std::unique_ptr<KoanWelcome> welcome_;
  // "An agent is connected"; it also starts agent control with the first
  // window (koan/browser/agent/koan_agent_service.h).
  std::unique_ptr<KoanAgentIndicator> agent_indicator_;
  // Last: it calls into the controllers above.
  std::unique_ptr<KoanShortcutController> shortcuts_;
  // Its commands run the shortcuts above.
  std::unique_ptr<KoanCommandPalette> command_palette_;
  std::map<std::string, std::string> session_data_;
  bool torn_down_ = false;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
  // The window's widget. BrowserView observes it from its AddedToWidget(),
  // before Koan does, so its OnWidgetDestroying() (which closes the
  // remaining tabs) runs before Koan's teardown.
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  // Keeps Chromium's AI search shortcuts off (see the constructor).
  std::unique_ptr<AiStarterPackFilter> ai_starter_pack_filter_;
  base::WeakPtrFactory<KoanBrowserViewController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_
