// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_INDICATOR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_INDICATOR_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/ui/views/koan_loading_bar.h"
#include "ui/compositor/compositor_animation_observer.h"

class BrowserView;

namespace ui {
class Compositor;
}

namespace views {
class Widget;
}

namespace koan {

class KoanLoadingBarView;

// Zen's page-load progress bar for one window (ZenProgressBar.sys.mjs, Zen's
// zen.view.enable-loading-indicator, Koan's prefs::kLoadingIndicator): while
// the selected tab loads, a pill at the top middle of its page (in the gap
// above the page card, or just inside the page where there is no gap), which
// turns into the "long load" sweep after 3 s (KoanLoadingBar). Not in the
// page's own fullscreen. A child widget, so that it can sit over the page,
// which takes no events.
class KoanLoadingIndicator : public TabStripModelObserver,
                             public content::WebContentsObserver,
                             public ui::CompositorAnimationObserver {
 public:
  explicit KoanLoadingIndicator(BrowserView* browser_view);
  KoanLoadingIndicator(const KoanLoadingIndicator&) = delete;
  KoanLoadingIndicator& operator=(const KoanLoadingIndicator&) = delete;
  ~KoanLoadingIndicator() override;

  void OnAddedToWidget();
  // Keeps the bar over the selected page.
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

 private:
  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidStartLoading() override;
  void DidStopLoading() override;

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

  // Shows or hides the bar for the selected tab's state.
  void Update();
  void OnLongLoad();
  // Draws the current frame, and stops the frames once the bar is gone.
  void Step(base::TimeTicks now);
  void StartFrames();
  void StopFrames();
  void CloseWidget();

  raw_ptr<BrowserView> browser_view_;
  KoanLoadingBar bar_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanLoadingBarView> view_ = nullptr;
  raw_ptr<ui::Compositor> compositor_ = nullptr;
  base::OneShotTimer long_load_timer_;
  PrefChangeRegistrar pref_change_registrar_;
  base::CallbackListSubscription fullscreen_subscription_;
  bool destroying_ = false;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LOADING_INDICATOR_H_
