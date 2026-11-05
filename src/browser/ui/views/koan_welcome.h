// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

class BrowserView;

namespace views {
class WebView;
class Widget;
}  // namespace views

namespace koan {

class KoanThemeController;

// Zen's welcome (ZenWelcome.mjs, zen-welcome.css, zen-welcome.ftl) on
// Chromium's first run: chrome://koan-welcome (koan_welcome_ui.h) fills the
// first window, which it sizes to Zen's 1200 x 720 at most, centres and
// locks while it shows, with the traffic lights hidden. "Welcome to a calmer
// internet", then pages to import from another browser, colour the Space,
// pick the search engine, choose Essentials and make Koan the default
// browser. Zen's ad blocker page is left out (see ZEN_PARITY.md).
//
// The page is a WebView in a child widget over the whole window; this object
// is its KoanWelcomeHost (what the page asks of the window). It shows once
// (pref koan.welcome_screen.seen, set once the screen is on screen, as Zen
// sets zen.welcome-screen.seen at the start) and only on Chromium's first
// run, i.e. of a new user data directory (not for each profile in it), so
// --no-first-run skips it and --force-first-run brings it back.
class KoanWelcome {
 public:
  explicit KoanWelcome(BrowserView* browser_view);
  KoanWelcome(const KoanWelcome&) = delete;
  KoanWelcome& operator=(const KoanWelcome&) = delete;
  ~KoanWelcome();

  bool is_showing() const { return widget_ != nullptr; }

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

 private:
  void MaybeShow();
  void Show();
  // Zen's window while the welcome shows: the size it was given, locked
  // (`locked`), or as it was before (`!locked`).
  void LockWindow(bool locked);
  // "Dive in!": the page has faded out; it closes, then the Essentials are
  // pinned and, if the import page picked a browser, the import dialog opens.
  void Finish(std::vector<GURL> essentials);
  // Esc: closes the screen, applying nothing more.
  void Skip();
  // `restore_window`: the window's buttons and size lock as they were (not
  // while the window is being destroyed).
  void Close(bool restore_window = true);
  // After the screen closes: the window active, the keyboard on its page.
  void FocusBrowser();
  // The surface outside the card drags the window (Zen's
  // -moz-window-dragging: drag); EndWindowDrag() after AppKit's drag.
  void StartWindowDrag();
  void EndWindowDrag();
  // Closes the theme picker the colours page opened, if it did (#41).
  void CloseThemePicker();
#if __has_include("koan/browser/ui/views/koan_theme_controller.h")
  KoanThemeController* ThemeController() const;
#endif
  void LayoutWidget();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  // The window's resize and maximize before LockWindow(true).
  bool could_resize_ = true;
  bool could_maximize_ = true;
  bool window_locked_ = false;
  // The import page picked a browser: its import opens after the screen.
  bool import_ = false;
  // Finish() ran: its choices apply once.
  bool finishing_ = false;
  // The colours page opened the theme picker (#41) over itself.
  bool theme_picker_open_ = false;

  base::WeakPtrFactory<KoanWelcome> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
