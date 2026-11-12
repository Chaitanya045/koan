// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class WebView;
}  // namespace views

namespace koan {

// Koan's welcome on Chromium's first run (onboarding v2, #181; a deliberate
// divergence from Zen's welcome): chrome://koan-welcome (koan_welcome_ui.h)
// over the whole first window, in shadcn's look. "The only browser you
// need", then the shortcut set, import from another browser and the
// search engine (Brave Search chosen); every step can be skipped, and Skip
// setup or Esc leaves at any point.
//
// The page is a WebView, the last child of the BrowserView over everything
// else in the window, in the window itself and never a child widget: on
// macOS a child widget is a second NSWindow, and the window's keyboard and
// clicks can't both reach a page in it (c174d/c174e, see koan_welcome.cc).
// It stays the last child while it shows, and its area is client area for
// the window's hit test (IsWindowCoveredAt()), so nothing in the window
// draws over it or drags the window from under it. This object is the page's
// KoanWelcomeHost (what the page asks of the window). It shows once (pref
// koan.welcome_screen.seen, set once the screen is on screen) and only on
// Chromium's first run, i.e. of a new user data directory (not for each
// profile in it), so --no-first-run skips it and --force-first-run brings it
// back.
class KoanWelcome : public content::WebContentsObserver,
                    public ui::AcceleratorTarget,
                    public views::FocusChangeListener,
                    public views::ViewObserver {
 public:
  explicit KoanWelcome(BrowserView* browser_view);
  KoanWelcome(const KoanWelcome&) = delete;
  KoanWelcome& operator=(const KoanWelcome&) = delete;
  ~KoanWelcome() override;

  bool is_showing() const { return web_view_ != nullptr; }
  // Showing, or about to show once the window is up (from OnAddedToWidget()
  // to the end): what else would open over the window waits for it.
  bool is_pending() const { return pending_; }
  // Whether the screen covers `point`, in the BrowserView's parent's
  // coordinates (BrowserView::NonClientHitTest()).
  bool CoversPoint(const gfx::Point& point) const;

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

 private:
  // content::WebContentsObserver (the page): its renderer gone, or its load
  // failed.
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void DidFailLoad(content::RenderFrameHost* render_frame_host,
                   const GURL& validated_url,
                   int error_code) override;

  // ui::AcceleratorTarget: Esc when the keyboard isn't in the page (the page
  // handles its own).
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // views::FocusChangeListener (the window's): the keyboard moved to another
  // page in the window, under this one. BrowserView gives the startup tab's
  // page the focus on the window's first activation (RestoreFocus()), which
  // on macOS can come after the page showed (a window that becomes key
  // late); so does a tab activated from outside (an extension, DevTools).
  // While the page covers the window, the keyboard comes back to it; what
  // shows over it (the floating URL bar) keeps the keyboard.
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;
  // The keyboard on the page (its first button or choice has the focus).
  void FocusPage();
  // Whether another page in the window (a tab's) has the keyboard.
  bool OtherPageHasFocus() const;

  // views::ViewObserver (the BrowserView): a view added or moved above the
  // page goes back under it.
  void OnChildViewAdded(views::View* observed_view,
                        views::View* child) override;
  void OnChildViewReordered(views::View* observed_view,
                            views::View* child) override;
  void KeepOnTop();

  // Whether this window shows the welcome: the first normal window of a
  // regular profile on Chromium's first run, if it hasn't been seen and no
  // other window has it pending.
  bool ShouldShow() const;
  // The welcome is pending or showing in this window (koan_welcome_state.h).
  void SetPending(bool pending);
  void MaybeShow();
  void Show();
  // "Start browsing": closes the screen, then opens the import dialog if the
  // import step picked a browser.
  void Finish();
  // Skip setup, Esc, the page's renderer gone or the page never loading:
  // closes the screen, applying nothing more.
  void Skip();
  // `remove_page`: take the page out of the window (not while the window is
  // being destroyed, which takes it along).
  void Close(bool remove_page = true);
  // Whether the window is closing or destroyed.
  bool IsWindowClosing() const;
  // After the screen closes: the keyboard on the window's page, if the window
  // is the active one (a crash or a timeout while another app is in front
  // doesn't take the focus).
  void FocusBrowser();
  // The space around the card drags the window, as a title bar would;
  // EndWindowDrag() after AppKit's drag.
  void StartWindowDrag();
  void EndWindowDrag();
  void LayoutWebView();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  // Where Esc is registered while the screen shows.
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  bool pending_ = false;
  // The import step picked a browser: its import opens after the screen.
  bool import_ = false;
  // Finish() ran: it applies once.
  bool finishing_ = false;
  // Gives up on a page that never asks for its data.
  base::OneShotTimer load_timer_;
  base::ScopedObservation<views::View, views::ViewObserver>
      browser_view_observation_{this};
  base::ScopedObservation<views::FocusManager, views::FocusChangeListener>
      focus_observation_{this};

  base::WeakPtrFactory<KoanWelcome> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
