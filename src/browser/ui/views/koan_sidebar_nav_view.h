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
class Label;
}  // namespace views

namespace koan {

// Zen's single-toolbar sidebar header is two pieces:
// - KoanSidebarNavButtons (back / forward / reload) at the trailing end of
//   the vertical tab strip's top row, next to the sidebar toggle;
// - KoanSidebarNavView below it: a URL pill with the site's name, Zen's
//   copy-URL button on hover (pref koan.urlbar.show_copy_url) and the site
//   panel button. Clicking the pill (or Ctrl+L) opens Zen's URL bar
//   (KoanUrlBarController), in the pill or floating; the pill is empty
//   meanwhile.
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

  // The pill, over which the URL bar edits in place.
  views::View* url_pill() const { return url_pill_; }

  // While the URL bar is open the pill is empty.
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
  void OnPillHovered(bool hovered);
  void ShowSitePanel();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::View> url_pill_ = nullptr;
  raw_ptr<views::Label> url_label_ = nullptr;
  raw_ptr<views::Button> copy_button_ = nullptr;
  raw_ptr<views::Button> site_button_ = nullptr;
  bool editing_ = false;
  bool hovered_ = false;
  // The active tab's UI changes (URL, title, favicon), from the same
  // TabUIHelper the tab strip draws from.
  base::CallbackListSubscription tab_ui_subscription_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
