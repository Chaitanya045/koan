// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/test/browser/koan_browser_test_utils.h"

#include "base/test/run_until.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_group.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"

namespace koan {

namespace {

KoanBrowserViewController* ControllerOf(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  return browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
}

}  // namespace

KoanWorkspaceController* GetSpaces(BrowserWindowInterface* browser) {
  KoanBrowserViewController* controller = ControllerOf(browser);
  return controller ? controller->workspaces() : nullptr;
}

KoanPinnedTabController* GetPinnedTabs(BrowserWindowInterface* browser) {
  KoanBrowserViewController* controller = ControllerOf(browser);
  return controller ? controller->pinned_tabs() : nullptr;
}

bool WaitForSpaceCount(BrowserWindowInterface* browser, size_t count) {
  return base::test::RunUntil([&] {
    KoanWorkspaceController* spaces = GetSpaces(browser);
    return spaces && spaces->GetSpaces().size() == count;
  });
}

std::vector<std::u16string> SpaceNames(BrowserWindowInterface* browser) {
  std::vector<std::u16string> names;
  if (KoanWorkspaceController* spaces = GetSpaces(browser)) {
    for (const KoanWorkspaceController::SpaceInfo& space :
         spaces->GetSpaces()) {
      names.push_back(space.name);
    }
  }
  return names;
}

void FlushSession(BrowserWindowInterface* browser) {
  SessionServiceTestHelper(browser->GetProfile()).SaveNow();
}

std::vector<int> TabsOfSpace(TabStripModel* tab_strip,
                             const tab_groups::TabGroupId& space) {
  std::vector<int> indices;
  const gfx::Range range =
      tab_strip->group_model()->GetTabGroup(space)->ListTabs();
  for (size_t i = range.start(); i < range.end(); ++i) {
    indices.push_back(static_cast<int>(i));
  }
  return indices;
}

}  // namespace koan
