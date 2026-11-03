// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

class BrowserView;

namespace views {
class View;
class Widget;
}  // namespace views

namespace koan {

// What the user chose on the welcome pages that applies once they close.
struct KoanWelcomeChoices {
  KoanWelcomeChoices();
  KoanWelcomeChoices(const KoanWelcomeChoices&);
  KoanWelcomeChoices& operator=(const KoanWelcomeChoices&);
  ~KoanWelcomeChoices();

  bool import = false;
  // Sites to pin as Essentials.
  std::vector<GURL> essentials;
};

// Zen's welcome screen (ZenWelcome.mjs, zen-welcome.ftl), shown over the
// first window on Chromium's first run: "Welcome to a calmer
// internet", then pages to import from another browser, give the browser a
// colour, pick the search engine, choose Essentials and make Koan the
// default browser. Zen's ad blocker page is left out (see ZEN_PARITY.md).
//
// The screen is a child widget over the window, below the window's controls
// (so the window still moves and closes), and takes the keyboard: Tab moves
// between its buttons, arrows between choices, Esc skips it. It shows once
// (pref koan.welcome_screen.seen, set once the screen is on screen) and only
// on Chromium's first run, i.e. of a new user data directory (not for each
// profile in it), so --no-first-run skips it and --force-first-run brings
// it back.
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

  // Called by the screen when the user is done: closes it (with a fade) and
  // applies `choices`, once (a second "Dive in!" during the fade does
  // nothing).
  void Finish(const KoanWelcomeChoices& choices);

  // Called when the user leaves the screen with Esc: closes it, applying
  // nothing more.
  void Skip();

  BrowserView* browser_view() const { return browser_view_; }

 private:
  void MaybeShow();
  void Show();
  // koan-welcome-webui: the screen is chrome://koan-welcome.
  void ShowWebUI();
  void Close();
  // After the screen closes: the window active, the keyboard on its page.
  void FocusBrowser();
  void LayoutWidget();
  // macOS: the traffic lights' area in BrowserView coordinates (empty when
  // the frame shows none, e.g. in fullscreen).
  gfx::Rect TrafficLightsBounds() const;
  void Apply(const KoanWelcomeChoices& choices);

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  // macOS: the title bar row is covered too, in two parts around the
  // traffic lights, which stay above it and usable. Beside them, over the
  // page, a child widget; behind them, a view in BrowserView, which AppKit's
  // buttons draw over.
  std::unique_ptr<views::Widget> title_bar_widget_;
  raw_ptr<views::View> title_bar_corner_ = nullptr;
  // Finish() ran: the screen is fading out, its choices about to apply.
  bool finishing_ = false;
  // The screen is chrome://koan-welcome (koan-welcome-webui).
  bool web_ui_ = false;

  base::WeakPtrFactory<KoanWelcome> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
