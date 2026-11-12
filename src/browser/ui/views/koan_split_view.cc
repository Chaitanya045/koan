// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_view.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "ui/menus/simple_menu_model.h"

namespace koan {

namespace {

// The end of an empty split's URL bar session (OpenEmptySplit()), Zen's
// ZenURLBarClosed handler: a tab opened from the bar (the selected tab now,
// in the same Space, not the empty pane or `original`) replaces the empty
// pane; anything else (closed, Esc, a switch to `original`, to a split, to
// another Space or an Essential) takes the pane away, and the tab is
// selected again if the empty pane still is. `close_pane`: the pane is a
// blank tab made for it (no Spaces), not the Space's empty tab.
void FinishEmptySplit(tabs::TabHandle original_handle,
                      tabs::TabHandle pane_handle,
                      bool close_pane,
                      bool picked) {
  tabs::TabInterface* pane = pane_handle.Get();
  if (!pane) {
    return;
  }
  BrowserWindowInterface* browser = pane->GetBrowserWindowInterface();
  TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
  if (!model) {
    return;
  }
  tabs::TabInterface* original = original_handle.Get();
  tabs::TabInterface* opened = model->GetActiveTab();
  const bool in_split = original && pane->IsSplit() &&
                        original->GetSplit() == pane->GetSplit() &&
                        original->GetBrowserWindowInterface() == browser;
  if (picked && in_split && opened && opened != pane && opened != original &&
      !opened->IsSplit() && !opened->IsPinned() &&
      opened->GetGroup() == original->GetGroup()) {
    // UpdateTabInSplit() wants the split selected; it moves `opened` into
    // the pane's place, keeps the split's layout and ratio, and closes the
    // pane. Zen selects the new page.
    model->ActivateTabAt(model->GetIndexOfTab(original));
    model->UpdateTabInSplit(pane, model->GetIndexOfTab(opened),
                            TabStripModel::SplitUpdateType::kReplace);
    model->ActivateTabAt(model->GetIndexOfTab(opened));
    return;
  }
  if (pane->IsSplit()) {
    model->RemoveSplit(*pane->GetSplit());
  }
  if (original && model->GetActiveTab() == pane &&
      original->GetBrowserWindowInterface() == browser) {
    model->ActivateTabAt(model->GetIndexOfTab(original));
  }
  // A blank tab made for the pane goes again, unless it was navigated since
  // (and so is the user's).
  if (close_pane && model->GetActiveTab() != pane &&
      !pane->GetContents()->GetController().CanGoBack()) {
    model->CloseWebContentsAt(model->GetIndexOfTab(pane),
                              TabCloseTypes::CLOSE_NONE);
  }
}

}  // namespace

void AddZenSplitTabMenuItems(ui::SimpleMenuModel* menu,
                             TabStripModel* tab_strip,
                             int index,
                             int num_tabs) {
  // Zen's tab-zen-split-tabs labels. Upstream keeps "Join N Tabs" disabled
  // past two tabs: Chromium's split views have two.
  if (const std::optional<size_t> add =
          menu->GetIndexOfCommandId(TabStripModel::CommandAddToSplit)) {
    menu->SetLabel(*add,
                   num_tabs > 1
                       ? u"Join " + base::NumberToString16(num_tabs) + u" Tabs"
                       : u"Add Split View...");
  }
  if (const std::optional<size_t> arrange =
          menu->GetIndexOfCommandId(TabStripModel::CommandArrangeSplit);
      arrange && CanSplitOutTab(tab_strip, index)) {
    menu->InsertItemAt(*arrange, kSplitOutTabCommandId, u"Split Out Tab");
  }
}

bool CanSplitOutTab(const TabStripModel* tab_strip, int index) {
  return tab_strip->ContainsIndex(index) &&
         tab_strip->GetTabAtIndex(index)->IsSplit();
}

void SplitOutTab(TabStripModel* tab_strip, int index) {
  if (CanSplitOutTab(tab_strip, index)) {
    tab_strip->RemoveSplit(*tab_strip->GetTabAtIndex(index)->GetSplit());
  }
}

void ShowSplitLimitToast(BrowserView* browser_view) {
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view)) {
    controller->toast()->Show(u"Can't add more panels to the split view!");
  }
}

bool OpenEmptySplit(BrowserWindowInterface* browser,
                    split_tabs::SplitTabLayout layout,
                    split_tabs::SplitTabCreatedSource source) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (!controller || !controller->url_bar()) {
    return false;
  }
  TabStripModel* model = browser->GetTabStripModel();
  tabs::TabInterface* original = model->GetActiveTab();
  // Zen disables cmd_zenNewEmptySplit on the empty tab and in a split.
  if (!original || original->IsSplit() || original->IsPinned() ||
      IsEmptyTab(original)) {
    return true;
  }
  // The Space's empty tab (a new one right of the tab if it has none yet),
  // or, without Spaces, a blank tab there.
  const int index = model->GetIndexOfTab(original);
  tabs::TabInterface* pane =
      EnsureEmptyTab(browser, original->GetGroup(), index + 1,
                     /*foreground=*/false);
  if (!pane || pane == original || pane->IsSplit()) {
    return true;
  }
  const bool close_pane = !IsEmptyTab(pane);
  model->AddToNewSplit({model->GetIndexOfTab(pane)},
                       split_tabs::SplitTabVisualData(layout), source);
  if (!pane->IsSplit()) {
    return true;
  }
  // Zen's default side: the empty pane on the right (or below).
  if (model->GetSplitData(*pane->GetSplit())->ListTabs().front() == pane) {
    model->ReverseTabsInSplit(*pane->GetSplit());
  }
  model->ActivateTabAt(model->GetIndexOfTab(pane));
  base::OnceCallback<void(bool)> finish = base::BindOnce(
      &FinishEmptySplit, original->GetHandle(), pane->GetHandle(), close_pane);
  if (!controller->url_bar()->OpenForEmptySplit(std::move(finish))) {
    // Zen: no new-tab bar (zen.urlbar.replace-newtab off, a fullscreen
    // page), no empty split.
    FinishEmptySplit(original->GetHandle(), pane->GetHandle(), close_pane,
                     /*picked=*/false);
  }
  return true;
}

bool HidesSplitMiniToolbar() {
  return true;
}

}  // namespace koan
