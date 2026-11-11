// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_sync.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_window_sync_client.h"
#include "koan/browser/ui/views/koan_window_sync_hooks.h"
#include "ui/base/class_property.h"

namespace koan {

namespace {

const void* const kWindowSyncKey = &kWindowSyncKey;

DEFINE_UI_CLASS_PROPERTY_KEY(bool, kKoanBlankWindowKey, false)

// The profile whose next window is a blank one, while it is being created.
Profile* g_blank_window_profile = nullptr;

// Zen's prefer-unsynced-windows for `profile`'s windows.
bool PrefersBlankWindows(Profile* profile) {
  return features::IsWorkspacesEnabled() &&
         profile->GetOriginalProfile()->GetPrefs()->GetBoolean(
             prefs::kWindowSyncPreferBlankWindows);
}

// A new normal window of `profile`'s regular profile, blank or synced.
void OpenWindow(Profile* profile, bool blank) {
  // A private window's New Blank Window opens a regular one, as Zen's.
  Profile* regular = profile->GetOriginalProfile();
  base::AutoReset<Profile*> request(&g_blank_window_profile,
                                    blank ? regular : nullptr);
  chrome::NewEmptyWindow(regular, /*should_trigger_session_restore=*/false);
}

}  // namespace

KoanWindowSync::KoanWindowSync(Profile* profile) : profile_(profile) {
  model_.FromValue(profile_->GetPrefs()->GetDict(prefs::kWindowSyncState));
}

KoanWindowSync::~KoanWindowSync() = default;

// static
KoanWindowSync* KoanWindowSync::ForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  auto* sync =
      static_cast<KoanWindowSync*>(profile->GetUserData(kWindowSyncKey));
  if (!sync) {
    auto owned = std::make_unique<KoanWindowSync>(profile);
    sync = owned.get();
    profile->SetUserData(kWindowSyncKey, std::move(owned));
  }
  return sync;
}

// static
bool KoanWindowSync::ShouldSync(BrowserView* browser_view) {
  Profile* profile = browser_view->GetProfile();
  return browser_view->GetIsNormalType() && profile &&
         !profile->IsOffTheRecord() && !IsBlankWindow(browser_view) &&
         profile->GetPrefs()->GetBoolean(prefs::kWindowSyncEnabled);
}

void KoanWindowSync::AddWindow(Window* window) {
  CHECK(window);
  if (std::ranges::find(windows_, window) != windows_.end()) {
    return;
  }
  // Behind the windows the user has used: it becomes the most recent when
  // it is activated.
  windows_.push_back(window);
  if (model_.seeded()) {
    stale_spaces_.insert(window);
    stale_items_.insert(window);
    ScheduleFlush();
  }
}

void KoanWindowSync::RemoveWindow(Window* window) {
  std::erase(windows_, window);
  stale_spaces_.erase(window);
  stale_items_.erase(window);
  if (applying_.get() == window) {
    applying_ = nullptr;
  }
  model_.OnWindowClosed(window->window_id());
}

void KoanWindowSync::OnWindowActivated(Window* window) {
  auto it = std::ranges::find(windows_, window);
  if (it == windows_.end()) {
    return;
  }
  std::rotate(windows_.begin(), it, it + 1);
}

bool KoanWindowSync::IsInSync(const Window* window) const {
  // Out of date windows can't be found by a const pointer in a set of
  // non-const ones without a cast; the lists are short.
  const auto is = [window](const raw_ptr<Window>& other) {
    return other.get() == window;
  };
  return std::ranges::any_of(windows_, is) &&
         std::ranges::none_of(stale_spaces_, is) &&
         std::ranges::none_of(stale_items_, is) && applying_.get() != window;
}

void KoanWindowSync::ReportSpaces(
    Window* window,
    std::vector<window_sync::SpaceRecord> spaces) {
  if (!IsInSync(window)) {
    return;
  }
  if (model_.SetSpaces(std::move(spaces))) {
    // Deleting a Space drops its pinned tabs, so the items change too.
    MarkOthersOutOfDate(window, /*spaces=*/true, /*items=*/true);
  }
}

void KoanWindowSync::ReportItems(Window* window,
                                 std::vector<window_sync::ItemRecord> items,
                                 const std::set<std::string>& closed) {
  if (!IsInSync(window) || !model_.seeded()) {
    return;
  }
  if (model_.SetItems(std::move(items), closed)) {
    MarkOthersOutOfDate(window, /*spaces=*/false, /*items=*/true);
  }
}

void KoanWindowSync::ReportFolders(
    Window* window,
    std::vector<window_sync::FolderRecord> folders) {
  if (!IsInSync(window) || !model_.seeded()) {
    return;
  }
  if (model_.SetFolders(std::move(folders))) {
    MarkOthersOutOfDate(window, /*spaces=*/false, /*items=*/true);
  }
}

KoanWindowSync::Window* KoanWindowSync::MostRecentWindow(
    const Window* except) const {
  for (const raw_ptr<Window>& window : windows_) {
    if (window.get() != except) {
      return window.get();
    }
  }
  return nullptr;
}

KoanWindowSync::Window* KoanWindowSync::FindWindow(WindowId id) const {
  for (const raw_ptr<Window>& window : windows_) {
    if (window->window_id() == id) {
      return window.get();
    }
  }
  return nullptr;
}

void KoanWindowSync::MarkOthersOutOfDate(Window* origin,
                                         bool spaces,
                                         bool items) {
  Store();
  for (const raw_ptr<Window>& window : windows_) {
    if (window.get() == origin) {
      continue;
    }
    if (spaces) {
      stale_spaces_.insert(window);
    }
    if (items) {
      stale_items_.insert(window);
    }
  }
  ScheduleFlush();
}

void KoanWindowSync::RequestShow(Window* window, const std::string& item) {
  if (!IsInSync(window)) {
    return;
  }
  pending_shows_[item] = window->window_id();
  ScheduleFlush();
}

void KoanWindowSync::SetLive(Window* window, const std::string& item) {
  const WindowId had = model_.LiveWindow(item);
  if (had == window->window_id()) {
    return;
  }
  model_.SetLive(item, window->window_id());
  if (had != window_sync::kNoWindow && FindWindow(had)) {
    pending_drops_[item] = had;
    ScheduleFlush();
  }
}

content::WebContents* KoanWindowSync::LivePage(const std::string& item) const {
  Window* window = FindWindow(model_.LiveWindow(item));
  return window ? window->PageOf(item) : nullptr;
}

void KoanWindowSync::OnPageChanged(Window* window, const std::string& item) {
  if (model_.LiveWindow(item) != window->window_id()) {
    return;
  }
  // Only schedules a repaint of the other windows' rows (no tab strip
  // changes), so it may run inside `window`'s notification.
  for (const raw_ptr<Window>& other : windows_) {
    if (other.get() != window) {
      other->RefreshPlaceholder(item);
    }
  }
}

void KoanWindowSync::OnWindowClosing(Window* window) {
  if (browser_shutdown::IsTryingToQuit() || applying_) {
    return;
  }
  Window* rescue = MostRecentWindow(window);
  if (!rescue) {
    return;
  }
  // A move needs both tab strips out of their notifications: true here
  // (UnloadController runs this before CloseAllTabs()).
  for (const std::string& item :
       model_.OnWindowClosing(window->window_id(), rescue->window_id())) {
    if (!window->CanGivePage(item) || !rescue->CanTakePage(item)) {
      continue;
    }
    applying_ = rescue;
    if (std::unique_ptr<content::WebContents> page = window->TakePage(item)) {
      rescue->PutPage(item, std::move(page));
      model_.SetLive(item, rescue->window_id());
    }
    applying_ = nullptr;
  }
}

void KoanWindowSync::MovePage(const std::string& item, Window* to) {
  const window_sync::ShowDecision decision =
      model_.RequestShow(item, to->window_id());
  if (decision.kind == window_sync::ShowDecision::Kind::kNone ||
      !to->CanTakePage(item)) {
    return;
  }
  Window* from = decision.kind == window_sync::ShowDecision::Kind::kMove
                     ? FindWindow(decision.from)
                     : nullptr;
  // The window with the page can't give it (the tab is in a split view
  // there): it stays, and this window keeps showing the placeholder.
  if (from && !from->CanGivePage(item)) {
    return;
  }
  applying_ = to;
  if (from) {
    if (std::unique_ptr<content::WebContents> page = from->TakePage(item)) {
      to->PutPage(item, std::move(page));
      model_.SetLive(item, to->window_id());
      // The new placeholder's row was made while the page was still there.
      from->RefreshPlaceholder(item);
    }
  } else {
    // No window has the page (the one that had it is gone): it loads here.
    for (const window_sync::ItemRecord& record : model_.items()) {
      if (record.id == item) {
        to->LoadPage(record);
        model_.SetLive(item, to->window_id());
        break;
      }
    }
  }
  applying_ = nullptr;
}

void KoanWindowSync::ScheduleFlush() {
  if (flush_scheduled_ || (stale_spaces_.empty() && stale_items_.empty() &&
                           pending_shows_.empty() && pending_drops_.empty())) {
    return;
  }
  flush_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanWindowSync::Flush, weak_factory_.GetWeakPtr()));
}

void KoanWindowSync::Flush() {
  flush_scheduled_ = false;
  // One window at a time, in a fresh task each, re-reading the lists: a
  // window may close, or another report come in, while one is reconciled.
  // Page moves first (the user is looking), then Spaces (a window's pinned
  // tabs need their Spaces), then the synced tabs.
  if (!pending_shows_.empty()) {
    auto [item, id] = *pending_shows_.begin();
    pending_shows_.erase(pending_shows_.begin());
    if (Window* to = FindWindow(id)) {
      MovePage(item, to);
    }
    ScheduleFlush();
    return;
  }
  if (!pending_drops_.empty()) {
    auto [item, id] = *pending_drops_.begin();
    pending_drops_.erase(pending_drops_.begin());
    Window* window = FindWindow(id);
    if (window && model_.LiveWindow(item) != id && window->CanGivePage(item)) {
      applying_ = window;
      // The duplicate page goes (after this task: it may still be in use
      // by the notifications of its replace).
      if (std::unique_ptr<content::WebContents> page = window->TakePage(item)) {
        base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
            FROM_HERE, std::move(page));
      }
      applying_ = nullptr;
    }
    ScheduleFlush();
    return;
  }
  Window* window = nullptr;
  bool spaces = false;
  if (!stale_spaces_.empty()) {
    window = stale_spaces_.begin()->get();
    stale_spaces_.erase(stale_spaces_.begin());
    spaces = true;
  } else if (!stale_items_.empty()) {
    window = stale_items_.begin()->get();
    stale_items_.erase(stale_items_.begin());
  }
  if (window) {
    applying_ = window;
    if (spaces) {
      window->ApplySpaces(model_.spaces());
    } else {
      window->ApplyItems(model_.items());
    }
    // The window may have gone while it was reconciled (RemoveWindow()
    // cleared `applying_` then).
    applying_ = nullptr;
  }
  ScheduleFlush();
}

void KoanWindowSync::Store() {
  profile_->GetPrefs()->SetDict(prefs::kWindowSyncState, model_.ToValue());
}

void OpenBlankWindow(Profile* profile) {
  if (profile) {
    OpenWindow(profile, /*blank=*/!PrefersBlankWindows(profile));
  }
}

bool HandleNewWindowCommand(BrowserWindowInterface* browser) {
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  if (!profile || !PrefersBlankWindows(profile)) {
    return false;
  }
  OpenWindow(profile, /*blank=*/true);
  return true;
}

bool IsBlankWindow(const BrowserView* browser_view) {
  return browser_view && browser_view->GetProperty(kKoanBlankWindowKey);
}

void MarkBlankWindowIfRequested(BrowserView* browser_view) {
  if (g_blank_window_profile &&
      browser_view->GetProfile() == g_blank_window_profile &&
      browser_view->GetIsNormalType()) {
    browser_view->SetProperty(kKoanBlankWindowKey, true);
    // One window per request.
    g_blank_window_profile = nullptr;
  }
}

base::ScopedClosureRunner WillOpenWindowForNavigation(Profile* profile) {
  // Zen's zen.window-sync.open-link-in-new-unsynced-window: a link opened in
  // a new window while synced windows exist opens a blank one.
  KoanWindowSync* sync = KoanWindowSync::ForProfile(profile);
  if (!sync || !sync->MostRecentWindow(nullptr) ||
      !profile->GetPrefs()->GetBoolean(
          prefs::kWindowSyncOpenLinksInBlankWindow)) {
    return base::ScopedClosureRunner();
  }
  g_blank_window_profile = profile;
  return base::ScopedClosureRunner(
      base::BindOnce([] { g_blank_window_profile = nullptr; }));
}

base::ScopedClosureRunner WillRestoreWindow(
    Profile* profile,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kBlankWindowSessionKey);
  if (!profile || profile->IsOffTheRecord() || it == extra_data.end() ||
      it->second.empty()) {
    return base::ScopedClosureRunner();
  }
  g_blank_window_profile = profile;
  return base::ScopedClosureRunner(
      base::BindOnce([] { g_blank_window_profile = nullptr; }));
}

void OnWindowClosing(BrowserWindowInterface* browser) {
  KoanWindowSyncClient* client = KoanWindowSyncClient::ForBrowser(browser);
  if (KoanWindowSync* sync = client ? client->sync() : nullptr) {
    sync->OnWindowClosing(client);
  }
}

}  // namespace koan
