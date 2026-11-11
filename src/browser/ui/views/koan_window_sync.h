// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "koan/browser/window_sync/koan_window_sync_model.h"

class BrowserView;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}

namespace tabs {
class TabInterface;
struct TabData;
}  // namespace tabs

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

    // --- The page of a synced tab (KoanWindowSync::MovePage()) ---

    // Whether the window has the page of `item` and can hand it over (the
    // tab is there, it isn't a placeholder, nor in a split view).
    virtual bool CanGivePage(const std::string& item) const = 0;
    // Whether the window has a placeholder for `item` to take a page into.
    virtual bool CanTakePage(const std::string& item) const = 0;
    // Replaces the page of `item` by a placeholder (in the page's
    // container) and returns the page. Only after CanGivePage().
    virtual std::unique_ptr<content::WebContents> TakePage(
        const std::string& item) = 0;
    // Puts `page` in place of the placeholder of `item`. Only after
    // CanTakePage().
    virtual void PutPage(const std::string& item,
                         std::unique_ptr<content::WebContents> page) = 0;
    // No window has the page: the placeholder of `item` loads its URL.
    virtual void LoadPage(const window_sync::ItemRecord& item) = 0;
    // The page of `item` in this window (its tab's contents, placeholder or
    // not), or null.
    virtual content::WebContents* PageOf(const std::string& item) const = 0;
    // The page of `item`, in another window, changed its title or favicon:
    // the row of the placeholder here shows them (AdjustPlaceholderTabData()).
    virtual void RefreshPlaceholder(const std::string& item) = 0;
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

  // `window` shows the synced tab `item` (selected it, or was activated with
  // it selected) and has a placeholder for it: its page comes over soon,
  // from the window that has it, or loads there (Zen's TabSelect / focus
  // swap). A later request for the same item replaces this one.
  void RequestShow(Window* window, const std::string& item);
  // `window` has the page of `item` (it loaded it, or restored it). If
  // another window had one too (both restored it), that one becomes a
  // placeholder soon: one page per synced tab.
  void SetLive(Window* window, const std::string& item);
  // The page of `item` in the window that has it, or null.
  content::WebContents* LivePage(const std::string& item) const;
  // The page of `item` in `window` changed its title or favicon (Zen's
  // ZenTabLabelChanged / ZenTabIconChanged, from the window with the page
  // only): every other window's placeholder row follows.
  void OnPageChanged(Window* window, const std::string& item);
  // `window` is closing (UnloadController::OnWindowClosing()): its pages go
  // to the most recently active other synced window, now, before its tabs
  // close. Not while quitting, nor from inside a reconcile or a move.
  void OnWindowClosing(Window* window);

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
  // Brings the page of `item` to `to`, if both windows can (re-checked
  // here: a queued request may be stale).
  void MovePage(const std::string& item, Window* to);
  void Store();

  // Owns this (as user data), so outlives it.
  const raw_ptr<Profile> profile_;
  window_sync::WindowSyncModel model_;
  // The synced windows, most recently active first.
  std::vector<raw_ptr<Window>> windows_;
  // Windows to reconcile, by what.
  std::set<raw_ptr<Window>> stale_spaces_;
  std::set<raw_ptr<Window>> stale_items_;
  // Requested page moves: item -> the window that shows it.
  std::map<std::string, WindowId> pending_shows_;
  // Second pages to turn back into placeholders: item -> their window.
  std::map<std::string, WindowId> pending_drops_;
  raw_ptr<Window> applying_ = nullptr;
  bool flush_scheduled_ = false;
  base::WeakPtrFactory<KoanWindowSync> weak_factory_{this};
};

// Called from AdjustTabData() (koan_pinned_tabs_hooks.h) for every tab's
// row: a placeholder's row shows the title and favicon of its page in the
// window that has it, as Zen syncs a tab's label and icon to its copies.
// Nothing for other tabs, or while no window has the page (the placeholder's
// own copy of the page's history names it then).
void AdjustPlaceholderTabData(const tabs::TabInterface* tab,
                              tabs::TabData& data);

// Zen's New Blank Window (cmd_zenNewNavigatorUnsynced, Accel+Shift+N): a
// normal window of `profile`'s regular profile without Spaces and outside
// the sync, like Zen's zen-unsynced-window (gZenWorkspaces.
// privateWindowOrDisabled). Its tabs are a plain list. With Zen's
// prefer-unsynced-windows (prefs::kWindowSyncPreferBlankWindows) it opens a
// synced window instead.
void OpenBlankWindow(Profile* profile);
// Chrome's New Window (IDC_NEW_WINDOW, from Koan's command policy): with
// prefer-unsynced-windows, opens a New Blank Window and returns true; false
// lets upstream open a synced one.
bool HandleNewWindowCommand(BrowserWindowInterface* browser);

// A blank window's mark in its session data (koan_session_data.h), so it
// comes back blank after a restart or when reopened (Zen's isZenUnsynced;
// koan::WillRestoreWindow()).
inline constexpr char kBlankWindowSessionKey[] = "koan.window_sync.blank";
// Whether `browser_view`'s window is one.
bool IsBlankWindow(const BrowserView* browser_view);
// KoanBrowserViewController::CreateFor(), before the window's controllers
// exist: marks the window blank if OpenBlankWindow() or a link
// (koan_window_sync_hooks.h) is creating it.
void MarkBlankWindowIfRequested(BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_H_
