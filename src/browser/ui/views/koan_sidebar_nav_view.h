// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/command_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class Button;
class LabelButton;
}  // namespace views

namespace koan {

// Zen's "single toolbar" header, at the top of the sidebar: back / forward /
// reload (plus window controls when the frame's are hidden) and a URL pill
// showing the current site. Clicking the pill (or Ctrl+L) focuses the
// omnibox, which slides the hidden toolbar down over the page as a floating
// URL bar; it slides away again once focus leaves it.
class KoanSidebarNavView : public views::View,
                           public CommandObserver,
                           public TabStripModelObserver,
                           public content::WebContentsObserver {
  METADATA_HEADER(KoanSidebarNavView, views::View)

 public:
  explicit KoanSidebarNavView(BrowserView* browser_view);
  KoanSidebarNavView(const KoanSidebarNavView&) = delete;
  KoanSidebarNavView& operator=(const KoanSidebarNavView&) = delete;
  ~KoanSidebarNavView() override;

  // CommandObserver:
  void EnabledStateChangedForCommand(int id, bool enabled) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void TitleWasSet(content::NavigationEntry* entry) override;

 private:
  void UpdateUrl();
  void FocusOmnibox();

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::Button> back_ = nullptr;
  raw_ptr<views::Button> forward_ = nullptr;
  raw_ptr<views::LabelButton> url_pill_ = nullptr;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_NAV_VIEW_H_
