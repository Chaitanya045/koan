// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_delegate.h"

class BrowserView;

namespace views {
class Widget;
}

namespace koan {

class KoanGlanceView;

// Zen-style Glance for one window: a link opened as a floating card over
// the current page, with the page dimmed behind it. The card can be closed
// (Esc, the close button, or a click on the dimmed page), expanded into a
// normal tab, or opened in a split with the current tab.
//
// The overlay is a child widget covering the contents area, so it has its
// own native window above the page's: the dimmed area and the buttons get
// mouse events, and the page underneath gets none while Glance is open. The
// card hosts a WebContents owned here until it is promoted into a tab.
class KoanGlanceController : public content::WebContentsDelegate,
                             public TabStripModelObserver {
 public:
  explicit KoanGlanceController(BrowserView* browser_view);
  KoanGlanceController(const KoanGlanceController&) = delete;
  KoanGlanceController& operator=(const KoanGlanceController&) = delete;
  ~KoanGlanceController() override;

  bool is_open() const { return contents_ != nullptr; }
  content::WebContents* contents() const { return contents_.get(); }

  // Runs `action` (Close, ExpandToTab, OpenInSplit) after the current event
  // is done: they destroy the overlay widget, which must not happen inside
  // one of its own buttons' click handlers.
  void PostAction(void (KoanGlanceController::*action)());

  // Opens `params.url` in Glance, over `source` (the active tab).
  void Open(const content::OpenURLParams& params, content::WebContents* source);
  void Close();
  void ExpandToTab();
  void OpenInSplit();

  // Called by KoanBrowserViewController.
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void CloseContents(content::WebContents* source) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  // Releases the overlay and returns the Glance WebContents (detached from
  // the card) so it can be inserted into the tab strip.
  std::unique_ptr<content::WebContents> TakeContents();
  void DestroyWidget();
  void LayoutWidget();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanGlanceView> view_ = nullptr;
  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<content::WebContents> source_ = nullptr;

  base::WeakPtrFactory<KoanGlanceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
