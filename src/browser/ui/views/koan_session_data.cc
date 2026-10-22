// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_session_data.h"

#include <array>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"

namespace koan {

namespace {

// Keys of Koan's data in a tab's session extra data.
constexpr std::string_view kKeyPrefix = "koan.";

// The features that act on a tab or window while it is restored.
constexpr TabRestoreHook kTabRestoreHooks[] = {
    {kPinnedTabSessionKey, &OnRestoringPinnedTab},
};
// None yet; folders (#60) will be the first.
constexpr std::array<WindowRestoreHook, 0> kWindowRestoreHooks = {};

// Koan's entries of a restored tab's or window's extra data.
std::map<std::string, std::string> KoanEntries(
    const std::map<std::string, std::string>& extra_data) {
  std::map<std::string, std::string> koan_data;
  for (const auto& [key, data] : extra_data) {
    if (base::StartsWith(key, kKeyPrefix) && !data.empty()) {
      koan_data[key] = data;
    }
  }
  return koan_data;
}

// The window's store, or null for a window without Koan's UI.
std::map<std::string, std::string>* WindowStore(
    const BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? &controller->session_data() : nullptr;
}

// Writes one entry of `contents`' data to the session (empty: removes it).
void WriteToSession(content::WebContents* contents,
                    const std::string& key,
                    const std::string& data) {
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  SessionService* session = SessionServiceFactory::GetForProfile(profile);
  const SessionID window_id =
      sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  // Not in a window yet: the session file gets it when the window's
  // commands are built.
  if (!session || !window_id.is_valid() || !tab_id.is_valid()) {
    return;
  }
  session->AddTabExtraData(window_id, tab_id, key.c_str(), data);
}

// The data of one tab. Moves to the new WebContents when a discard replaces
// the tab's.
class KoanTabSessionData
    : public content::WebContentsUserData<KoanTabSessionData>,
      public content::WebContentsObserver {
 public:
  ~KoanTabSessionData() override = default;

  std::map<std::string, std::string>& data() { return data_; }

  // content::WebContentsObserver:
  void AboutToBeDiscarded(content::WebContents* new_contents) override {
    KoanTabSessionData::CreateForWebContents(new_contents);
    KoanTabSessionData::FromWebContents(new_contents)->data_ = data_;
    // Saved under the new tab's id once it is in its window.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<content::WebContents> contents) {
                         if (!contents) {
                           return;
                         }
                         for (const auto& [key, data] :
                              GetTabSessionData(contents.get())) {
                           WriteToSession(contents.get(), key, data);
                         }
                       },
                       new_contents->GetWeakPtr()));
  }

 private:
  friend WebContentsUserData;
  explicit KoanTabSessionData(content::WebContents* contents)
      : WebContentsUserData(*contents), WebContentsObserver(contents) {}

  std::map<std::string, std::string> data_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(KoanTabSessionData);

}  // namespace

std::string GetTabSessionData(content::WebContents* contents,
                              std::string_view key) {
  KoanTabSessionData* store =
      contents ? KoanTabSessionData::FromWebContents(contents) : nullptr;
  if (!store) {
    return std::string();
  }
  auto it = store->data().find(std::string(key));
  return it == store->data().end() ? std::string() : it->second;
}

void SetTabSessionData(content::WebContents* contents,
                       std::string_view key,
                       std::string data) {
  CHECK(base::StartsWith(key, kKeyPrefix));
  KoanTabSessionData::CreateForWebContents(contents);
  std::map<std::string, std::string>& store =
      KoanTabSessionData::FromWebContents(contents)->data();
  const std::string name(key);
  if (data.empty()) {
    store.erase(name);
  } else {
    store[name] = data;
  }
  WriteToSession(contents, name, data);
}

std::map<std::string, std::string> GetTabSessionData(
    content::WebContents* contents) {
  KoanTabSessionData* store =
      contents ? KoanTabSessionData::FromWebContents(contents) : nullptr;
  return store ? store->data() : std::map<std::string, std::string>();
}

void OnRestoringTab(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation) {
  std::map<std::string, std::string> koan_data = KoanEntries(extra_data);
  if (koan_data.empty()) {
    return;
  }
  KoanTabSessionData::CreateForWebContents(contents);
  KoanTabSessionData::FromWebContents(contents)->data() = koan_data;
  for (const TabRestoreHook& hook : kTabRestoreHooks) {
    auto it = koan_data.find(hook.key);
    if (it != koan_data.end()) {
      hook.on_restoring(contents, it->second, from_session_restore, entries,
                        selected_navigation);
    }
  }
}

std::string GetWindowSessionData(const BrowserWindowInterface* browser,
                                 std::string_view key) {
  const std::map<std::string, std::string>* store = WindowStore(browser);
  if (!store) {
    return std::string();
  }
  auto it = store->find(std::string(key));
  return it == store->end() ? std::string() : it->second;
}

void SetWindowSessionData(BrowserWindowInterface* browser,
                          std::string_view key,
                          std::string data) {
  CHECK(base::StartsWith(key, kKeyPrefix));
  std::map<std::string, std::string>* store = WindowStore(browser);
  if (!store) {
    return;
  }
  const std::string name(key);
  if (data.empty()) {
    store->erase(name);
  } else {
    (*store)[name] = data;
  }
  if (SessionService* session =
          SessionServiceFactory::GetForProfile(browser->GetProfile())) {
    session->AddWindowExtraData(browser->GetSessionID(), name.c_str(), data);
  }
}

std::map<std::string, std::string> GetWindowSessionData(
    const BrowserWindowInterface* browser) {
  const std::map<std::string, std::string>* store = WindowStore(browser);
  return store ? *store : std::map<std::string, std::string>();
}

void OnRestoringWindow(BrowserWindowInterface* browser,
                       const std::map<std::string, std::string>& extra_data,
                       bool from_session_restore) {
  std::map<std::string, std::string> koan_data = KoanEntries(extra_data);
  std::map<std::string, std::string>* store = WindowStore(browser);
  if (koan_data.empty() || !store) {
    return;
  }
  *store = koan_data;
  for (const WindowRestoreHook& hook : kWindowRestoreHooks) {
    auto it = koan_data.find(hook.key);
    if (it != koan_data.end()) {
      hook.on_restoring(browser, it->second, from_session_restore);
    }
  }
}

}  // namespace koan
