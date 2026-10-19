// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;

namespace koan {

class KoanCompactModeController;
class KoanGlanceController;
class KoanShortcutController;
class KoanWorkspaceController;

// Per-window Koan state and behaviour. Owned by its BrowserView through a
// ui::ClassProperty; created and destroyed by the hooks in
// koan_browser_view_hooks.h. Compact mode and sidebar styling hang off this.
class KoanBrowserViewController : public views::ViewObserver {
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
  KoanWorkspaceController* workspaces() const { return workspaces_.get(); }
  KoanGlanceController* glance() const { return glance_.get(); }
  KoanShortcutController* shortcuts() const { return shortcuts_.get(); }

  // Number of live controllers, i.e. open browser windows.
  static int live_count();

 private:
  // views::ViewObserver:
  void OnViewAddedToWidget(views::View* observed_view) override;
  void OnViewBoundsChanged(views::View* observed_view) override;

  // Rounds the content card's corners when it is shown, restores upstream's
  // corners otherwise. Runs after each layout of the contents area, since the
  // card depends on tab strip mode and fullscreen state.
  void UpdateContentCard();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<KoanCompactModeController> compact_mode_;
  std::unique_ptr<KoanWorkspaceController> workspaces_;
  std::unique_ptr<KoanGlanceController> glance_;
  // Last: it calls into the controllers above.
  std::unique_ptr<KoanShortcutController> shortcuts_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observations_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BROWSER_VIEW_CONTROLLER_H_
