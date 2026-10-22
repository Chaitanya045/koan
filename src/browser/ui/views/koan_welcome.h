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
class Widget;
}

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
// first window on the first run of a profile: "Welcome to a calmer
// internet", then pages to import from another browser, give the browser a
// colour, pick the search engine, choose Essentials and make Koan the
// default browser. Zen's ad blocker page is left out (see ZEN_PARITY.md).
//
// The screen is a child widget covering the window. It shows once per
// profile (pref koan.welcome_screen.seen) and only on Chromium's first run,
// so --no-first-run skips it and --force-first-run brings it back.
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
  // applies `choices`.
  void Finish(const KoanWelcomeChoices& choices);

  BrowserView* browser_view() const { return browser_view_; }

 private:
  void MaybeShow();
  void Show();
  void Close();
  void LayoutWidget();
  void Apply(const KoanWelcomeChoices& choices);

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;

  base::WeakPtrFactory<KoanWelcome> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WELCOME_H_
