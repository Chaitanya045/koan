// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/command_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class Button;
class LabelButton;
}  // namespace views

namespace koan {

// Zen's single-toolbar sidebar header is two pieces:
// - KoanSidebarNavButtons (back / forward / reload) at the trailing end of
//   the vertical tab strip's top row, next to the sidebar toggle;
// - KoanSidebarNavView below it: a URL pill with the site's icon and host.
// Clicking the pill (or Ctrl+L) focuses the omnibox, which
// KoanCompactModeController shows as Zen's floating URL bar, centred over
// the window, until focus leaves it. The pill is empty meanwhile, like Zen.

class KoanSidebarNavButtons : public views::BoxLayoutView,
                              public CommandObserver {
  METADATA_HEADER(KoanSidebarNavButtons, views::BoxLayoutView)

 public:
  explicit KoanSidebarNavButtons(BrowserView* browser_view);
  KoanSidebarNavButtons(const KoanSidebarNavButtons&) = delete;
  KoanSidebarNavButtons& operator=(const KoanSidebarNavButtons&) = delete;
  ~KoanSidebarNavButtons() override;

  // CommandObserver:
  void EnabledStateChangedForCommand(int id, bool enabled) override;

 private:
  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::Button> back_ = nullptr;
  raw_ptr<views::Button> forward_ = nullptr;
};

class KoanSidebarNavView : public views::View, public TabStripModelObserver {
  METADATA_HEADER(KoanSidebarNavView, views::View)

 public:
  explicit KoanSidebarNavView(BrowserView* browser_view);
  KoanSidebarNavView(const KoanSidebarNavView&) = delete;
  KoanSidebarNavView& operator=(const KoanSidebarNavView&) = delete;
  ~KoanSidebarNavView() override;

  // While the floating URL bar is open the pill is empty.
  void SetEditing(bool editing);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void ObserveActiveTab();
  void UpdateUrl();
  void FocusOmnibox();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::LabelButton> url_pill_ = nullptr;
  bool editing_ = false;
  // The active tab's UI changes (URL, title, favicon), from the same
  // TabUIHelper the tab strip draws from.
  base::CallbackListSubscription tab_ui_subscription_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
