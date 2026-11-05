// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "chrome/browser/command_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class BrowserView;

namespace views {
class Button;
class ImageButton;
class ImageView;
class Label;
}  // namespace views

namespace koan {

// Zen's single-toolbar sidebar header is two pieces:
// - KoanSidebarNavButtons (back / forward / reload) at the trailing end of
//   the vertical tab strip's top row, next to the sidebar toggle;
// - KoanSidebarNavView below it: a URL pill with the site identity button
//   (the favicon, which opens the site panel), the site's name and Zen's
//   copy-URL button on hover (pref koan.urlbar.show_copy_url). Clicking the
//   pill (or Ctrl+L) opens Zen's URL bar (KoanUrlBarController), in the pill or
//   floating; the pill is empty meanwhile.
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

class KoanSidebarNavView : public views::View,
                           public TabStripModelObserver,
                           public views::ViewObserver {
  METADATA_HEADER(KoanSidebarNavView, views::View)

 public:
  explicit KoanSidebarNavView(BrowserView* browser_view);
  KoanSidebarNavView(const KoanSidebarNavView&) = delete;
  KoanSidebarNavView& operator=(const KoanSidebarNavView&) = delete;
  ~KoanSidebarNavView() override;

  // The pill, over which the URL bar edits in place.
  views::View* url_pill() const { return url_pill_; }

  // What a bubble about the page (permissions, page info, the bookmark
  // bubble ...) points at: the site icon, else the pill. Null while neither
  // is on screen (the sidebar is hidden).
  views::View* bubble_anchor() const;

  // While the URL bar is open the pill is empty.
  void SetEditing(bool editing);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;

  // views::ViewObserver (the copy button's focus, the location bar's in-use
  // indicator):
  void OnViewFocused(views::View* observed_view) override;
  void OnViewBlurred(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  void ObserveActiveTab();
  void UpdateUrl();
  void FocusOmnibox();
  void OnPillHovered(bool hovered);
  // Zen's copy button is there for keyboard and screen readers, and shown
  // while the pill is hovered or the button has focus.
  void UpdateCopyButtonOpacity();
  void ShowSitePanel();
  // The camera / microphone (or location ...) indicator, and a permission
  // request in its quiet form, are in the hidden toolbar: the chip's icon
  // next to the site icon (Zen shows the permission icons in the identity
  // box), red while something is in use, with the chip's tooltip. Pressing
  // the site icon opens page info, with the permissions.
  void UpdateInUse();
  // The identity button: the site panel for a page, the URL bar on an empty
  // tab.
  void OnSiteButtonPressed();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::View> url_pill_ = nullptr;
  raw_ptr<views::Label> url_label_ = nullptr;
  raw_ptr<views::Button> copy_button_ = nullptr;
  raw_ptr<views::ImageButton> site_button_ = nullptr;
  bool editing_ = false;
  // Whether the pill shows a page (not an empty tab).
  bool has_page_ = false;
  bool hovered_ = false;
  // The active tab's UI changes (URL, title, favicon), from the same
  // TabUIHelper the tab strip draws from.
  base::CallbackListSubscription tab_ui_subscription_;
  base::ScopedObservation<views::View, views::ViewObserver>
      copy_button_observation_{this};
  raw_ptr<views::ImageView> in_use_ = nullptr;
  // The location bar's permission indicator and request chips (hidden with
  // the toolbar).
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      chips_observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
