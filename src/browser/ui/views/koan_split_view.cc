// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_view.h"

#include <optional>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "ui/menus/simple_menu_model.h"

namespace koan {

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

}  // namespace koan
