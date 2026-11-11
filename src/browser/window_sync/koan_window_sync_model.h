// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_WINDOW_SYNC_KOAN_WINDOW_SYNC_MODEL_H_
#define KOAN_BROWSER_WINDOW_SYNC_KOAN_WINDOW_SYNC_MODEL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/values.h"

// Zen's window sync (ZenWindowSync.sys.mjs, zen.window-sync.*): every synced
// window of a profile shows the same Spaces, Essentials and pinned tabs. This
// is the canonical state, one per profile, as plain data: what the windows
// show is reconciled to it (Plan...()), and which window has the page of a
// synced tab is decided here (RequestShow(), OnWindowClosing()). No browser
// types, so it is unit tested on its own; KoanWindowSync
// (koan/browser/ui/views/koan_window_sync.h) owns it and applies it.
//
// Everything is identified by sync ids: strings that are the same in every
// window (a Space's tab group and a tab have a different id in each window).
namespace koan::window_sync {

// A window, by the value of its SessionID (BrowserWindowInterface::
// GetSessionID().id()). Unique for the life of the browser process.
using WindowId = int32_t;
inline constexpr WindowId kNoWindow = -1;

// A Space: what the user picks for it (KoanWorkspaceController::SpaceData),
// by sync id. Spaces are in sidebar order.
struct SpaceRecord {
  std::string id;
  std::u16string name;
  // An emoji, or empty.
  std::u16string icon;
  // KoanWorkspaceController::SpaceTheme::stored (empty: Zen's default).
  std::string theme;

  bool operator==(const SpaceRecord&) const = default;
};

// A Zen folder (KoanFolder, #60): in a Space's pinned section, maybe inside
// another folder. Folders are in sidebar order (each folder before its
// subfolders, which follow in their order). Its sync id is the folder's
// session token, the same in every window. Zen 1.24b syncs folders as tab
// groups (TabGroupCreate / Update / Moved / Removed).
struct FolderRecord {
  std::string id;
  std::u16string name;
  bool collapsed = false;
  std::string space_id;
  // The folder this one is in; empty at the top of its Space.
  std::string parent_id;

  bool operator==(const FolderRecord&) const = default;
};

// A synced tab: an Essential (in every Space) or a tab pinned to a Space.
// Items are in sidebar order: Essentials first, then each Space's pinned
// tabs (the order among one Space's pinned tabs is what counts).
struct ItemRecord {
  enum class Kind { kEssential, kPinned };

  std::string id;
  Kind kind = Kind::kEssential;
  // The Space a pinned tab is in; empty for an Essential.
  std::string space_id;
  // Zen's pinned URL: what the tab opens where no window has its page.
  std::string url;
  // "Change Label…" and "Change Icon…" (an emoji); empty: the page's.
  std::u16string custom_title;
  std::u16string custom_icon;
  // The user container the tab is in (containers::Serialize()), or empty.
  // An agent container's tab is never an item.
  std::string container;
  // The folder a pinned tab is in, or empty.
  std::string folder_id;

  bool operator==(const ItemRecord&) const = default;
};

// What a window does to show the target state: applied in this order. Moves
// say where an existing entry goes; indices count the target's entries of
// the same kind (Spaces, Essentials, one Space's pinned tabs).
template <typename Record>
struct Plan {
  // In the window but not in the target.
  std::vector<std::string> remove;
  // In the target but not in the window, with their target index.
  std::vector<std::pair<Record, int>> add;
  // In both, with different data (the target's).
  std::vector<Record> update;
  // The target order, when the window's (after the removals and additions)
  // differs: every id, in order.
  std::optional<std::vector<std::string>> order;

  bool empty() const {
    return remove.empty() && add.empty() && update.empty() && !order;
  }
};

// Reconciles a window's Spaces (`local`, in its order) to `target`.
Plan<SpaceRecord> PlanSpaces(const std::vector<SpaceRecord>& local,
                             const std::vector<SpaceRecord>& target);
// A window joining the sync: its Spaces that have no sync id yet (`local`,
// in its order) take the ids of target Spaces no Space of the window has
// (`target` minus `taken`), so that they become those Spaces, with their
// tabs, instead of being deleted. A local Space takes the first free target
// Space with its name and icon, else the first free one in order. Returns
// the id each local Space takes, or empty if none is left for it.
std::vector<std::string> AdoptSpaces(const std::vector<SpaceRecord>& local,
                                     const std::vector<SpaceRecord>& target,
                                     const std::set<std::string>& taken);
// The same for folders, except that a folder's collapsed state isn't synced
// live (Zen keeps it only in the session): `update` leaves out a folder that
// differs only in it, which a new folder still takes from the target.
Plan<FolderRecord> PlanFolders(const std::vector<FolderRecord>& local,
                               const std::vector<FolderRecord>& target);
// The same for synced tabs. `order` is the target's ids in sidebar order
// (an item that changes kind or Space is updated, then ordered).
Plan<ItemRecord> PlanItems(const std::vector<ItemRecord>& local,
                           const std::vector<ItemRecord>& target);

// Where to show a synced tab's page when `window` asks for it.
struct ShowDecision {
  enum class Kind {
    // `window` has it already: nothing to do.
    kNone,
    // Move the page from `from` (the window that has it) to `window`.
    kMove,
    // No window has the page: `window` loads the item's URL.
    kLoad,
  };
  Kind kind = Kind::kNone;
  WindowId from = kNoWindow;

  bool operator==(const ShowDecision&) const = default;
};

// The profile's canonical state.
class WindowSyncModel {
 public:
  WindowSyncModel();
  WindowSyncModel(const WindowSyncModel&) = delete;
  WindowSyncModel& operator=(const WindowSyncModel&) = delete;
  ~WindowSyncModel();

  const std::vector<SpaceRecord>& spaces() const { return spaces_; }
  const std::vector<FolderRecord>& folders() const { return folders_; }
  const std::vector<ItemRecord>& items() const { return items_; }
  // Whether any window has given the model its state yet (a new profile, or
  // one from before window sync, has none: its first window seeds it).
  bool seeded() const { return seeded_; }

  // A synced window's state after the user changed it: it becomes the
  // target of every other window. Returns whether anything changed. Items
  // in Spaces that are gone are dropped (their tabs closed with the Space).
  bool SetSpaces(std::vector<SpaceRecord> spaces);
  // A folder needs its Space, and its parent before it in the same Space
  // (else it is dropped, with what is inside it); a window reports its
  // folders after its Spaces and before its items. A folder's collapsed state
  // is saved but doesn't count as a change (false if nothing else changed).
  bool SetFolders(std::vector<FolderRecord> folders);
  // `closed`: the items whose tab the window closed, unpinned or took out of
  // the Essentials without pinning it (Zen's TabUnpinned closes it in the
  // other windows). Not a tab taken into a split: it stays as a normal tab.
  bool SetItems(std::vector<ItemRecord> items,
                const std::set<std::string>& closed = {});
  // Whether `item` left the model because a window closed or unpinned its
  // tab: every other window closes its tab too, also the one with the page (Zen's
  // TabClose). Otherwise the window with the page keeps it as a normal tab.
  // Until the item comes back (a reopened tab keeps its id).
  bool WasClosed(const std::string& item) const {
    return closed_.contains(item);
  }

  // --- Which window has a synced tab's page (Zen's _zenContentsVisible) ---

  // The window with the page of `item`, or kNoWindow.
  WindowId LiveWindow(const std::string& item) const;
  // `window` has the page of `item` now (it loaded it, or a move finished).
  void SetLive(const std::string& item, WindowId window);
  // `window` shows `item` (it selected it, or got focus with it selected):
  // what it takes to bring the page there. Doesn't change the model; the
  // caller moves or loads, then calls SetLive().
  ShowDecision RequestShow(const std::string& item, WindowId window) const;
  // `window` is closing: the pages it has go to `rescue_to`, the synced
  // window used most recently after it (Zen moves them to the most recent
  // window), or nowhere (kNoWindow: the last synced window, or quitting).
  // Returns the items whose page moves; the rest of the window's pages
  // close with it.
  std::vector<std::string> OnWindowClosing(WindowId window, WindowId rescue_to);
  // `window` is gone: no page is there any more.
  void OnWindowClosed(WindowId window);

  // --- Persistence (prefs::kWindowSyncState) ---

  // {"spaces": [...], "items": [...]}: the pages' windows aren't saved (after
  // a restart the session decides which window has a page).
  base::DictValue ToValue() const;
  // Replaces the state with `value`, skipping malformed entries. A value
  // with no "spaces" leaves the model unseeded.
  void FromValue(const base::DictValue& value);

 private:
  // Drops folders and items whose Space is gone, folders whose parent is,
  // takes items out of folders that are gone, and forgets pages of items
  // that are gone.
  void DropOrphans();
  // Takes `items` out of folders the model doesn't have in their Space.
  void ClearUnknownFolders(std::vector<ItemRecord>& items) const;

  std::vector<SpaceRecord> spaces_;
  std::vector<FolderRecord> folders_;
  std::vector<ItemRecord> items_;
  bool seeded_ = false;
  std::map<std::string, WindowId> live_;
  // Items whose tab was closed (WasClosed()). Not saved: a window reconciled
  // after a restart has no tab left of them to close.
  std::set<std::string> closed_;
};

}  // namespace koan::window_sync

#endif  // KOAN_BROWSER_WINDOW_SYNC_KOAN_WINDOW_SYNC_MODEL_H_
