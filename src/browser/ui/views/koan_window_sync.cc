// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_sync.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

namespace {

const void* const kWindowSyncKey = &kWindowSyncKey;

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
         !profile->IsOffTheRecord() &&
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
                                 std::vector<window_sync::ItemRecord> items) {
  if (!IsInSync(window) || !model_.seeded()) {
    return;
  }
  if (model_.SetItems(std::move(items))) {
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

void KoanWindowSync::ScheduleFlush() {
  if (flush_scheduled_ || (stale_spaces_.empty() && stale_items_.empty())) {
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
  // Spaces first: a window's pinned tabs need their Spaces.
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

}  // namespace koan
