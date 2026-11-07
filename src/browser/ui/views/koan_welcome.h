// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;

namespace views {
class WebView;
}  // namespace views

namespace koan {

// Koan's welcome on Chromium's first run (onboarding v2, #181; a deliberate
// divergence from Zen's welcome): chrome://koan-welcome (koan_welcome_ui.h)
// over the whole first window, in shadcn's look. "Welcome to a calmer
// internet", then the shortcut set, import from another browser and the
// search engine (Brave Search chosen); every step can be skipped, and Skip
// setup or Esc leaves at any point.
//
// The page is a WebView in a child widget over the window (on macOS a child
// NSWindow, made key so that the page has the keyboard); this object is its
// KoanWelcomeHost (what the page asks of the window). It shows once
// (pref koan.welcome_screen.seen, set once the screen is on screen) and only
// on Chromium's first run, i.e. of a new user data directory (not for each
// profile in it), so --no-first-run skips it and --force-first-run brings
// it back.
class KoanWelcome : public content::WebContentsObserver,
                    public views::WidgetObserver {
 public:
  explicit KoanWelcome(BrowserView* browser_view);
  KoanWelcome(const KoanWelcome&) = delete;
  KoanWelcome& operator=(const KoanWelcome&) = delete;
  ~KoanWelcome() override;

  bool is_showing() const { return widget_ != nullptr; }

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

  // views::WidgetObserver (the screen's widget): closed by something else
  // than Close(), e.g. Cmd+W or Close Window while it is the key window.
  void OnWidgetClosing(views::Widget* widget) override;

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
  void Close();
  // Whether the window is closing or destroyed.
  bool IsWindowClosing() const;
  // After the screen closes: the window active, the keyboard on its page.
  // Only if the screen was the active window (`was_active`): a crash or a
  // timeout while another app is in front doesn't take the focus.
  void FocusBrowser(bool was_active);
  void LayoutWidget();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool pending_ = false;
  // The import step picked a browser: its import opens after the screen.
  bool import_ = false;
  // Finish() ran: it applies once.
  bool finishing_ = false;
  // Gives up on a page that never asks for its data.
  base::OneShotTimer load_timer_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};

  base::WeakPtrFactory<KoanWelcome> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
