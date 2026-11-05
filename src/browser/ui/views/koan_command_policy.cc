// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_command_policy.h"

#include <iterator>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/actions/chrome_action_properties.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/common/page_zoom.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_vertical_tabs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/actions/actions.h"
#include "ui/base/window_open_disposition.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/browser_commands_mac.h"
#endif

namespace koan {

namespace {

using Upstream = void (*)(BrowserWindowInterface* browser,
                          WindowOpenDisposition disposition);

// Who in Koan may take a row's command.
enum class Owner {
  // KoanGlanceController::HandleBrowserCommand(): while a Glance card is
  // open, the page commands act on the Glance page, as on a tab in Zen
  // (audit 3 GL2, .agent/notes/upstream/split-glance-library-media.md).
  kGlance,
  // KoanWorkspaceController::RunGroupCommand(): a Spaces window runs
  // upstream's tab group commands its own way (Spaces lockdown G4). Only in
  // a Spaces window, and nothing of upstream's runs there.
  kSpaces,
  // MaybeOpenUrlBarForNewTab(): Zen's replace-newtab opens the URL bar in
  // place of a new tab.
  kNewTab,
  // IgnoresFullscreenToolbarToggle(): no "Always Show Toolbar in Full
  // Screen" while Koan's toolbar is hidden (fullscreen F3).
  kFullscreenToolbar,
};

struct Row {
  actions::ActionId action;
  // The browser command (IDC_*) the action runs; 0 for actions without one
  // (menus).
  int command;
  Owner owner;
  // Upstream's callback (browser_actions.cc), for when Koan lets the command
  // through. Null: Koan's alone where the row is installed.
  Upstream upstream;
  // Hidden from the menus that list actions (the app menu), where the row is
  // installed.
  bool hidden = false;
};

// The table. An action id appears once (checked in InstallCommandPolicy()).
constexpr Row kRows[] = {
    // Glance: the page commands.
    {kActionCloseTab, IDC_CLOSE_TAB, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       base::RecordAction(base::UserMetricsAction("CloseTabByKey"));
       chrome::CloseTab(browser);
     }},
    {kActionReload, IDC_RELOAD, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::Reload(browser, disposition);
     }},
    {kActionReloadBypassingCache, IDC_RELOAD_BYPASSING_CACHE, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::ReloadBypassingCache(browser, disposition);
     }},
    {kActionStop, IDC_STOP, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Stop(browser);
     }},
    {kActionBack, IDC_BACK, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::GoBack(browser, disposition);
     }},
    {kActionForward, IDC_FORWARD, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::GoForward(browser, disposition);
     }},
    {kActionFocusLocation, IDC_FOCUS_LOCATION, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       if (!BrowserWindow::FromBrowser(browser)->IsLocationBarVisible()) {
         return;
       }
       chrome::FocusLocationBar(browser);
     }},
    {kActionFind, IDC_FIND, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Find(browser);
     }},
    {kActionFindNext, IDC_FIND_NEXT, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::FindNext(browser);
     }},
    {kActionFindPrevious, IDC_FIND_PREVIOUS, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::FindPrevious(browser);
     }},
    {kActionZoomPlus, IDC_ZOOM_PLUS, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_IN);
     }},
    {kActionZoomNormal, IDC_ZOOM_NORMAL, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_RESET);
     }},
    {kActionZoomMinus, IDC_ZOOM_MINUS, Owner::kGlance,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_OUT);
     }},

    // The URL bar: ⌘T, File > New Tab, the sidebar's "+ New Tab" row.
    {kActionNewTab, IDC_NEW_TAB, Owner::kNewTab,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::NewTab(browser, NewTabTypes::kNewTabCommand);
     }},

    // Spaces: the ⌘⌃ group keys, the Tab menu and the app menu's "Tab
    // groups". The hidden ones have no place with Spaces (a tab is always in
    // a Space; Spaces aren't saved groups).
    {kActionCreateNewTabGroup, IDC_CREATE_NEW_TAB_GROUP, Owner::kSpaces},
    {kActionCloseTabGroup, IDC_CLOSE_TAB_GROUP, Owner::kSpaces},
    {kActionFocusNextTabGroup, IDC_FOCUS_NEXT_TAB_GROUP, Owner::kSpaces},
    {kActionFocusPrevTabGroup, IDC_FOCUS_PREV_TAB_GROUP, Owner::kSpaces},
    {kActionAddNewTabToGroup, IDC_ADD_NEW_TAB_TO_GROUP, Owner::kSpaces},
    {kActionAddNewTabRecentGroup, IDC_ADD_NEW_TAB_RECENT_GROUP, Owner::kSpaces},
    {kActionWindowGroupTab, IDC_WINDOW_GROUP_TAB, Owner::kSpaces, nullptr,
     /*hidden=*/true},
    {kActionGroupTargetTab, IDC_GROUP_TARGET_TAB, Owner::kSpaces, nullptr,
     /*hidden=*/true},
    {kActionGroupUngroupedTabs, IDC_GROUP_UNGROUPED_TABS, Owner::kSpaces,
     nullptr, /*hidden=*/true},
    {kActionTabGroupsMenu, 0, Owner::kSpaces, nullptr, /*hidden=*/true},
    {kActionSavedTabGroupsSubmenu, 0, Owner::kSpaces, nullptr,
     /*hidden=*/true},

#if BUILDFLAG(IS_MAC)
    // Fullscreen: View > Always Show Toolbar in Full Screen, ⇧⌘F.
    {kActionToggleFullscreenToolbar, IDC_TOGGLE_FULLSCREEN_TOOLBAR,
     Owner::kFullscreenToolbar,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::ToggleAlwaysShowToolbarInFullscreen(browser);
     }},
#endif
};

KoanBrowserViewController* ControllerOf(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
}

// Whether `owner`'s rows go into `browser_view`'s window at all.
bool Installs(Owner owner, BrowserView* browser_view) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  switch (owner) {
    case Owner::kGlance:
      return browser_view->GetIsNormalType();
    case Owner::kSpaces:
      return controller && controller->workspaces()->IsEnabled();
    case Owner::kNewTab:
      return features::IsUrlBarEnabled() && browser_view->GetIsNormalType();
    case Owner::kFullscreenToolbar:
      return true;
  }
}

// Whether Koan takes `row`'s command now, in `browser`.
bool Takes(const Row& row, BrowserWindowInterface* browser) {
  switch (row.owner) {
    case Owner::kGlance: {
      KoanBrowserViewController* controller = ControllerOf(browser);
      return controller &&
             controller->glance()->HandleBrowserCommand(row.command);
    }
    case Owner::kSpaces: {
      // The window is closing (no controller): nothing, as before.
      KoanBrowserViewController* controller = ControllerOf(browser);
      if (controller && row.command) {
        controller->workspaces()->RunGroupCommand(row.command);
      }
      return true;
    }
    case Owner::kNewTab:
      return MaybeOpenUrlBarForNewTab(browser);
    case Owner::kFullscreenToolbar:
      return IgnoresFullscreenToolbarToggle(browser);
  }
}

void Run(const Row* row,
         BrowserWindowInterface* browser,
         actions::ActionItem* item,
         actions::ActionInvocationContext context) {
  if (Takes(*row, browser) || !row->upstream) {
    return;
  }
  row->upstream(browser, context.GetProperty(chrome::kDispositionKey));
}

}  // namespace

void InstallCommandPolicy(BrowserView* browser_view) {
  BrowserWindowInterface* browser = browser_view->browser();
  BrowserActions* actions = BrowserActions::From(browser);
  if (!actions || !actions->root_action_item()) {
    return;
  }
#if DCHECK_IS_ON()
  for (size_t a = 0; a < std::size(kRows); ++a) {
    for (size_t b = a + 1; b < std::size(kRows); ++b) {
      DCHECK_NE(kRows[a].action, kRows[b].action) << "an action in two rows";
    }
  }
#endif
  for (const Row& row : kRows) {
    if (!Installs(row.owner, browser_view)) {
      continue;
    }
    actions::ActionItem* item = actions::ActionManager::Get().FindAction(
        row.action, actions->root_action_item());
    if (!item) {
      continue;
    }
    // Like upstream's own callbacks, bound to the window, whose actions die
    // with it. (Enabled states stay the command controller's; where Koan
    // changes them, e.g. for a Glance page, the owner sets them.)
    item->SetInvokeActionCallback(base::BindRepeating(
        &Run, base::Unretained(&row), base::Unretained(browser)));
    if (row.hidden) {
      item->SetVisible(false);
    }
  }
}

bool HandlesBrowserCommand(BrowserWindowInterface* browser, int command_id) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (!browser_view || !command_id) {
    return false;
  }
  for (const Row& row : kRows) {
    if (row.command == command_id && Installs(row.owner, browser_view)) {
      return Takes(row, browser);
    }
  }
  return false;
}

std::optional<bool> HidesMenuCommand(BrowserView* browser_view,
                                     int command_id) {
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  switch (command_id) {
    // Upstream's tab group items (Tab > Group Tab, its Option alternate): a
    // tab is always in a Space, "Move To" moves it to another.
    case IDC_WINDOW_GROUP_TAB:
    case IDC_GROUP_TARGET_TAB:
      return controller && controller->workspaces()->IsEnabled();
    // View > Always Show Toolbar in Full Screen: with Koan's toolbar hidden
    // (single toolbar) fullscreen reveals nothing at the top edge, whatever
    // the setting (Chromium's, synced) says, so it isn't offered, neither
    // greyed out nor there. (The command itself is ignored there: the
    // kFullscreenToolbar row.)
    case IDC_TOGGLE_FULLSCREEN_TOOLBAR:
      return browser_view && IsToolbarHidden(browser_view);
    // View > "Show Tabs Vertically": Koan owns the sidebar, which is always
    // vertical (#154). Upstream's validation sets this item's hidden state
    // itself (hidden only in immersive fullscreen), so a hide at menu build
    // time doesn't hold.
    // (Not IDC_TOGGLE_VERTICAL_TABS_COLLAPSE: it stays, titled "Toggle
    // Sidebar's Width" by the menu bar, and runs compact mode; its check mark
    // follows the collapse state, i.e. compact mode hiding the sidebar. It has
    // no key: the sidebar action keeps none, Shift+Cmd+L is Koan's inspector.)
    case IDC_TOGGLE_VERTICAL_TABS:
      // With Koan's sidebar off nothing here overrides upstream's own state
      // (nullopt, not false).
      if (OwnsSidebar()) {
        return true;
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

}  // namespace koan
