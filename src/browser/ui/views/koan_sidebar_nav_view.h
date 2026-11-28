// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "chrome/browser/command_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;

namespace views {
class Button;
class ImageView;
class Label;
}  // namespace views

namespace koan {

class KoanSidebarExtensionsView;
class KoanSpaceExtensionsContextMenu;
class KoanSiteButton;

// Zen's single-toolbar sidebar header is two pieces:
// - KoanSidebarNavButtons (back / forward / reload; on Linux first Zen's
//   app menu button, KoanAppMenuButton) at the trailing end of the vertical
//   tab strip's top row, next to the sidebar toggle;
// - KoanSidebarNavView below it: a URL pill with the site identity button
//   (the favicon, which opens the site panel), the site's name, then, while
//   the sidebar is hovered, the pinned extensions, Zen's copy-URL button
//   (pref koan.urlbar.show_copy_url) and Zen's Extensions button (the
//   extensions menu). Clicking the pill (or Ctrl+L) opens Zen's URL bar
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

class KoanSidebarNavView : public views::View,
                           public TabStripModelObserver,
                           public views::ViewObserver,
                           public views::WidgetObserver {
  METADATA_HEADER(KoanSidebarNavView, views::View)

 public:
  explicit KoanSidebarNavView(BrowserView* browser_view);
  KoanSidebarNavView(const KoanSidebarNavView&) = delete;
  KoanSidebarNavView& operator=(const KoanSidebarNavView&) = delete;
  ~KoanSidebarNavView() override;

  // The pill, over which the URL bar edits in place.
  views::View* url_pill() const { return url_pill_; }

  // Zen's extension buttons in the pill (#73).
  KoanSidebarExtensionsView* extensions() const { return extensions_; }

  // Zen's Extensions button (#zen-site-data-icon-button), at the pill's end.
  views::Button* extensions_button() const { return extensions_button_; }

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

  // views::ViewObserver (the pill buttons' focus, the location bar's in-use
  // indicator):
  void OnViewFocused(views::View* observed_view) override;
  void OnViewBlurred(views::View* observed_view) override;
  void OnViewVisibilityChanged(views::View* observed_view,
                               views::View* starting_view,
                               bool visible) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // views::WidgetObserver (the extensions menu):
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void ObserveActiveTab();
  void UpdateUrl();
  void FocusOmnibox();
  void OnPillHovered(bool hovered);
  // Which of the copy-URL and Extensions buttons are in the pill, and whether
  // they are faded in (koan_url_pill_buttons.h). Faded out, they keep their
  // place, so Tab and screen readers reach them.
  void UpdatePillButtons();
  // Zen's Extensions button: Chromium's extensions menu at the button, or
  // closes it.
  void OnExtensionsButtonPressed();
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
  raw_ptr<views::Button> extensions_button_ = nullptr;
  // Its context menu: Extensions in This Space... (#473).
  std::unique_ptr<KoanSpaceExtensionsContextMenu> extensions_context_menu_;
  raw_ptr<KoanSidebarExtensionsView> extensions_ = nullptr;
  raw_ptr<KoanSiteButton> site_button_ = nullptr;
  bool editing_ = false;
  // Whether the pill shows a page (not an empty tab).
  bool has_page_ = false;
  bool hovered_ = false;
  // The active tab's UI changes (URL, title, favicon), from the same
  // TabUIHelper the tab strip draws from.
  base::CallbackListSubscription tab_ui_subscription_;
  // The copy-URL and Extensions buttons (their focus).
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      pill_buttons_observation_{this};
  // The pointer over the sidebar (from the extensions view, which watches it).
  base::CallbackListSubscription sidebar_hover_subscription_;
  // The extensions menu the Extensions button opened, while it is open.
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      menu_observation_{this};
  raw_ptr<views::ImageView> in_use_ = nullptr;
  // The location bar's permission indicator and request chips (hidden with
  // the toolbar).
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      chips_observation_{this};
  // The copy button's setting (kUrlBarShowCopyUrl), so a change shows at
  // once in every window.
  PrefChangeRegistrar pref_change_registrar_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
