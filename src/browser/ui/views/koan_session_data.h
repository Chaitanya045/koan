// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SESSION_DATA_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SESSION_DATA_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class BrowserWindowInterface;

namespace content {
class NavigationEntry;
class WebContents;
}  // namespace content

// Data that Koan features keep with the session, per tab and per window, so
// it comes back after a restart and when a closed tab or window is reopened.
// Each feature has its own key ("koan.<feature>", e.g. "koan.pinned_tab") and
// stores a string (usually JSON) under it.
//
// - Per tab: Chromium's per-tab session extra data. It follows the tab itself,
//   not its index, and moves with it when a discard replaces its WebContents.
// - Per window: Chromium's per-window session extra data, for state that
//   belongs to the window rather than one tab (e.g. folders: their names,
//   whether they are collapsed, empty ones).
//
// The store keeps a copy of what was set last: a feature calls Set...() after
// every change of its data, and reads it back with Get...(). To act while a
// tab or window is restored, add a TabRestoreHook or WindowRestoreHook to
// kTabRestoreHooks / kWindowRestoreHooks in koan_session_data.cc. Upstream
// calls the hooks at the end of this file (patches to session_service_base.cc,
// session_restore.cc, browser_tabrestore.cc and browser_live_tab_context.cc).
namespace koan {

// --- Per tab ---

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

// --- Per window ---

// The feature's data for `browser` under `key`, or empty.
std::string GetWindowSessionData(const BrowserWindowInterface* browser,
                                 std::string_view key);

// Sets the feature's data for `browser` under `key` (empty removes it) and
// writes it to the session. Kept for windows with Koan's UI (normal browser
// windows); a no-op for others.
void SetWindowSessionData(BrowserWindowInterface* browser,
                          std::string_view key,
                          std::string data);

// A feature's say when a window with its data is restored: called once the
// window exists, before its tabs are added (the data is already readable
// with GetWindowSessionData()). `from_session_restore` is false when a
// closed window is reopened.
struct WindowRestoreHook {
  const char* key;
  void (*on_restoring)(BrowserWindowInterface* browser,
                       const std::string& data,
                       bool from_session_restore);
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

// Every feature's data for `browser`, by key. Called when the session file is
// rebuilt (SessionServiceBase::BuildCommandsForBrowser) and when a window is
// closed into the tab restore service
// (BrowserLiveTabContext::GetExtraDataForWindow).
std::map<std::string, std::string> GetWindowSessionData(
    const BrowserWindowInterface* browser);

// Takes Koan's entries of a restored window's `extra_data` and runs the
// features' WindowRestoreHooks. Called right after the window is created by
// SessionRestore (CreateRestoredBrowser) and by BrowserLiveTabContext::Create
// (a reopened window), before its tabs are restored.
void OnRestoringWindow(BrowserWindowInterface* browser,
                       const std::map<std::string, std::string>& extra_data,
                       bool from_session_restore);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SESSION_DATA_H_
