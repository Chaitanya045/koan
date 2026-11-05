// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_command_policy.h"

#include "base/functional/bind.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/actions/chrome_action_properties.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/common/page_zoom.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "ui/actions/actions.h"
#include "ui/base/window_open_disposition.h"

namespace koan {

namespace {

using Upstream = void (*)(BrowserWindowInterface* browser,
                          WindowOpenDisposition disposition);

struct Redirect {
  actions::ActionId action;
  // The browser command, as the Koan code that takes it knows it.
  int command;
  // Upstream's callback (browser_actions.cc), for when Koan lets it through.
  Upstream upstream;
};

// Glance (audit 3 GL2, .agent/notes/upstream/split-glance-library-media.md):
// while a Glance card is open these act on the Glance page, as in Zen, where
// a Glance is a tab: ⌘W closes the Glance, not the page under it.
constexpr Redirect kGlanceRedirects[] = {
    {kActionCloseTab, IDC_CLOSE_TAB,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       base::RecordAction(base::UserMetricsAction("CloseTabByKey"));
       chrome::CloseTab(browser);
     }},
    {kActionReload, IDC_RELOAD,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::Reload(browser, disposition);
     }},
    {kActionReloadBypassingCache, IDC_RELOAD_BYPASSING_CACHE,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::ReloadBypassingCache(browser, disposition);
     }},
    {kActionStop, IDC_STOP,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Stop(browser);
     }},
    {kActionBack, IDC_BACK,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::GoBack(browser, disposition);
     }},
    {kActionForward, IDC_FORWARD,
     [](BrowserWindowInterface* browser, WindowOpenDisposition disposition) {
       chrome::GoForward(browser, disposition);
     }},
    {kActionFocusLocation, IDC_FOCUS_LOCATION,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       if (!BrowserWindow::FromBrowser(browser)->IsLocationBarVisible()) {
         return;
       }
       chrome::FocusLocationBar(browser);
     }},
    {kActionFind, IDC_FIND,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Find(browser);
     }},
    {kActionFindNext, IDC_FIND_NEXT,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::FindNext(browser);
     }},
    {kActionFindPrevious, IDC_FIND_PREVIOUS,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::FindPrevious(browser);
     }},
    {kActionZoomPlus, IDC_ZOOM_PLUS,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_IN);
     }},
    {kActionZoomNormal, IDC_ZOOM_NORMAL,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_RESET);
     }},
    {kActionZoomMinus, IDC_ZOOM_MINUS,
     [](BrowserWindowInterface* browser, WindowOpenDisposition) {
       chrome::Zoom(browser, content::PAGE_ZOOM_OUT);
     }},
};

}  // namespace

bool HandlesBrowserCommand(BrowserWindowInterface* browser, int command_id) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->glance()->HandleBrowserCommand(command_id);
}

void InstallCommandPolicy(BrowserView* browser_view) {
  BrowserWindowInterface* browser = browser_view->browser();
  BrowserActions* actions = BrowserActions::From(browser);
  if (!browser_view->GetIsNormalType() || !actions ||
      !actions->root_action_item()) {
    return;
  }
  for (const Redirect& redirect : kGlanceRedirects) {
    actions::ActionItem* item = actions::ActionManager::Get().FindAction(
        redirect.action, actions->root_action_item());
    if (!item) {
      continue;
    }
    // Like upstream's own callbacks, bound to the window, whose actions die
    // with it.
    item->SetInvokeActionCallback(base::BindRepeating(
        [](BrowserWindowInterface* browser, int command, Upstream upstream,
           actions::ActionItem* item,
           actions::ActionInvocationContext context) {
          if (HandlesBrowserCommand(browser, command)) {
            return;
          }
          upstream(browser, context.GetProperty(chrome::kDispositionKey));
        },
        base::Unretained(browser), redirect.command, redirect.upstream));
  }
}

}  // namespace koan
