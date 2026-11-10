// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_

#include <set>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "koan/browser/window_sync/koan_window_sync_model.h"

class BrowserView;
class Profile;

namespace koan {

// Zen's window sync for one profile: the one owner of the synced state (the
// Spaces, the Essentials, the pinned tabs and which window has each synced
// tab's page, window_sync::WindowSyncModel), which the profile's synced
// windows observe. The design is in .agent/notes/upstream/window-sync.md.
//
// A window never changes another window directly. It reports what the user
// did to its own state (Report...()); the model takes it, and the other
// windows are reconciled to the model later, in a task of their own
// (Flush()), one at a time. So no window is changed from inside another
// window's TabStripModel notification (TabStripModel has a release CHECK
// against changes from its own notifications), and a window that goes away
// meanwhile is simply not reconciled.
class KoanWindowSync : public base::SupportsUserData::Data {
 public:
  using WindowId = window_sync::WindowId;

  // A synced window, as the sync sees it (KoanWindowSyncClient).
  class Window {
   public:
    virtual ~Window() = default;
    virtual WindowId window_id() const = 0;
    // Makes the window show `spaces` / `items` (the model's), in order. Only
    // called from Flush(), never from a notification of the window's
    // models. What the window changes on the way, it doesn't report.
    virtual void ApplySpaces(
        const std::vector<window_sync::SpaceRecord>& spaces) {}
    virtual void ApplyItems(const std::vector<window_sync::ItemRecord>& items) {
    }
  };

  explicit KoanWindowSync(Profile* profile);
  KoanWindowSync(const KoanWindowSync&) = delete;
  KoanWindowSync& operator=(const KoanWindowSync&) = delete;
  ~KoanWindowSync() override;

  // The profile's sync, made on first use. Null for an off-the-record
  // profile (private windows are never synced, like Zen's).
  static KoanWindowSync* ForProfile(Profile* profile);
  // Whether a window of `browser_view`'s kind takes part: a normal window of
  // a regular profile, with Zen's zen.window-sync.enabled (prefs::
  // kWindowSyncEnabled). Popups, app windows, devtools and private windows
  // never do.
  static bool ShouldSync(BrowserView* browser_view);

  // A synced window opened. If the model has state (other windows, or a
  // past session), the window is reconciled to it soon and its reports
  // count only after that; otherwise its first reports seed the model.
  void AddWindow(Window* window);
  // The window is going away; after this it is never called.
  void RemoveWindow(Window* window);
  // The window became the active one (for OnWindowClosing()'s order: Zen
  // hands a closing window's pages to the most recent window).
  void OnWindowActivated(Window* window);

  // What `window` shows after the user changed it. Ignored while the window
  // is out of date (a reconcile is pending: what it reports is stale) or
  // being reconciled (its changes are the sync's own).
  void ReportSpaces(Window* window,
                    std::vector<window_sync::SpaceRecord> spaces);
  void ReportItems(Window* window, std::vector<window_sync::ItemRecord> items);

  // Whether `window` is being reconciled right now: its controllers leave
  // alone what the sync is doing (it isn't the user's).
  bool IsApplying(const Window* window) const {
    return applying_.get() == window;
  }
  // Whether `window`'s reports count (it is in sync with the model).
  bool IsInSync(const Window* window) const;

  // The synced window used most recently other than `except`, or null.
  Window* MostRecentWindow(const Window* except) const;
  Window* FindWindow(WindowId id) const;

  window_sync::WindowSyncModel& model() { return model_; }
  const window_sync::WindowSyncModel& model() const { return model_; }

 private:
  // Every other window gets the model's state soon.
  void MarkOthersOutOfDate(Window* origin, bool spaces, bool items);
  void ScheduleFlush();
  void Flush();
  void Store();

  // Owns this (as user data), so outlives it.
  const raw_ptr<Profile> profile_;
  window_sync::WindowSyncModel model_;
  // The synced windows, most recently active first.
  std::vector<raw_ptr<Window>> windows_;
  // Windows to reconcile, by what.
  std::set<raw_ptr<Window>> stale_spaces_;
  std::set<raw_ptr<Window>> stale_items_;
  raw_ptr<Window> applying_ = nullptr;
  bool flush_scheduled_ = false;
  base::WeakPtrFactory<KoanWindowSync> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_
