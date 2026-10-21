// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class BrowserView;

namespace views {
class Widget;
}

namespace koan {

class KoanGlanceView;

// Zen-style Glance for one window: a link opened as a floating card over
// the current page, with the page dimmed behind it. The card grows out of
// the click that opened it and shrinks back into it when it is closed (Esc,
// the close button, or a click on the dimmed page). Beside the card: expand
// it into a normal tab, or open it in a split with the current tab. An
// activation click on a link in the Glance page replaces the card's page.
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
  // While the card shrinks away after a close.
  bool is_closing() const { return closing_; }
  content::WebContents* contents() const { return contents_.get(); }

  // Runs `action` (Close, ExpandToTab, OpenInSplit) after the current event
  // is done: they destroy the overlay widget, which must not happen inside
  // one of its own buttons' click handlers.
  void PostAction(void (KoanGlanceController::*action)());

  // Opens `params.url` in Glance, over `source` (the active tab), or loads
  // it in the open card.
  void Open(const content::OpenURLParams& params, content::WebContents* source);
  // Lets the page's beforeunload handlers ask to stay, then shrinks the card
  // back into the click it grew out of and drops it.
  void Close();
  // Esc, in the page or on the buttons. Like Zen, while the page has an
  // editable field focused the first Esc only turns the close button into
  // "Confirm" for 3s; a second one closes.
  void OnEscape();
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
  void BeforeUnloadFired(content::WebContents* source,
                         bool proceed,
                         bool* proceed_to_fire_unload) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  // Watches a WebContents for what Glance needs from its input: the last
  // mouse press, in screen coordinates (the card grows out of it; Zen grows
  // it out of the clicked link's rect, which only its content process
  // knows; a key press forgets it, so a Glance opened from the keyboard
  // grows out of the card's centre), and whether an editable field has
  // focus (for Esc).
  class InputTracker : public content::WebContentsObserver {
   public:
    explicit InputTracker(content::WebContents* contents);

    struct Press {
      gfx::Point point;
      base::TimeTicks time;
    };
    const std::optional<Press>& press() const { return press_; }
    bool editing() const { return editing_; }

    // content::WebContentsObserver:
    void DidGetUserInteraction(const blink::WebInputEvent& event) override;
    void PrimaryPageChanged(content::Page& page) override;
    void OnFocusChangedInPage(
        const content::FocusedNodeDetails& details) override;

   private:
    std::optional<Press> press_;
    bool editing_ = false;
  };

  // Tracks presses in the active tab (also after a navigation gave it a new
  // WebContents).
  void TrackActiveTab();

  // The rect the card grows out of, in the overlay's coordinates: a small
  // card around the latest press in the tab or the Glance page, or in the
  // card's centre.
  gfx::Rect GrowRect() const;

  // Shrinks the card away (after beforeunload let it go) and drops it.
  void CloseNow();
  // Ends the close button's "Confirm" state.
  void EndConfirm();
  // Resets the page's dialogs and detaches it from this delegate.
  void ReleaseContents();
  // Drops the overlay and the Glance page: `contents_` is deleted once the
  // current task is done (this can run inside its delegate callbacks).
  void TearDown();
  // Drops the overlay and returns the Glance WebContents (detached from the
  // card, its animations finished) so it can be inserted into the tab strip.
  std::unique_ptr<content::WebContents> TakeContents();
  void DestroyWidget();
  void LayoutWidget();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanGlanceView> view_ = nullptr;
  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<content::WebContents> source_ = nullptr;
  // Where the card grew out of (Zen's originalPosition): it shrinks back
  // there on close.
  gfx::Rect grow_rect_;
  // Set while the card shrinks away; `close_timer_` drops it afterwards.
  bool closing_ = false;
  base::OneShotTimer close_timer_;
  // Set while the page's beforeunload handlers run.
  bool awaiting_beforeunload_ = false;
  // Runs while the close button asks to confirm.
  base::OneShotTimer confirm_timer_;

  std::unique_ptr<InputTracker> tab_input_;
  std::unique_ptr<InputTracker> glance_input_;

  base::WeakPtrFactory<KoanGlanceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
