// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TAB_SESSION_DATA_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TAB_SESSION_DATA_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace content {
class NavigationEntry;
class WebContents;
}  // namespace content

// Per-tab data that Koan features keep with the session, so it comes back
// with the tab: after a restart, when a closed tab or window is reopened, and
// when a discard replaces the tab's WebContents. It is stored as Chromium's
// per-tab session extra data, one entry per feature under its own key
// ("koan.<feature>", e.g. "koan.pinned_tab"), so it follows the tab itself,
// not its index.
//
// A feature calls SetTabSessionData() whenever its data changes and reads it
// back with GetTabSessionData(). To act on a tab while it is restored (before
// its pages are applied, e.g. to replace them), add a TabRestoreHook to
// kRestoreHooks in koan_tab_session_data.cc. Upstream calls the hooks at the
// end of this file (patches to session_service_base.cc,
// browser_tabrestore.cc and browser_live_tab_context.cc).
namespace koan {

// The feature's data for `contents` under `key`, or empty.
std::string GetTabSessionData(content::WebContents* contents,
                              std::string_view key);

// Sets the feature's data for `contents` under `key` (empty removes it) and
// writes it to the session.
void SetTabSessionData(content::WebContents* contents,
                       std::string_view key,
                       std::string data);

// A feature's say when a tab with its data is restored: called with that
// data before the tab's `entries` are applied, which it may replace (setting
// `selected_navigation`). `from_session_restore` is false when a closed tab
// or window is reopened.
struct TabRestoreHook {
  const char* key;
  void (*on_restoring)(
      content::WebContents* contents,
      const std::string& data,
      bool from_session_restore,
      std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
      int& selected_navigation);
};

// --- Hooks, called from upstream ---

// Every feature's data for `contents`, by key. Called when the session file
// is rebuilt (SessionServiceBase::BuildCommandsForTab, which otherwise drops
// per-tab extra data) and when a tab is closed into the tab restore service
// (BrowserLiveTabContext::GetExtraDataForTab).
std::map<std::string, std::string> GetTabSessionData(
    content::WebContents* contents);

// Takes Koan's entries of a restored tab's `extra_data` and runs the
// features' TabRestoreHooks. Called from CreateRestoredTab() in
// browser_tabrestore.cc, before `entries` are applied.
void OnRestoringTab(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TAB_SESSION_DATA_H_
