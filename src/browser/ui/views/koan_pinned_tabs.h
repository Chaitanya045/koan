// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_tabs_hooks.h"
#include "ui/base/models/image_model.h"
#include "url/gurl.h"

class BrowserView;
class TabStripModel;

namespace content {
class NavigationEntry;
class WebContents;
}  // namespace content

namespace koan {

struct TabMenuContext;

// Zen's per-tab state for pinned tabs, Essentials and renamed tabs. Kept on
// the tab's WebContents (carried over when a discard replaces it) and saved
// in the session as extra tab data, so it survives restarts.
struct KoanTabState {
  KoanTabState();
  KoanTabState(const KoanTabState&);
  KoanTabState& operator=(const KoanTabState&);
  ~KoanTabState();

  // The URL the tab was pinned on (Zen's "pinned URL"): where "Reset Pinned
  // Tab" and the reset button go. Set while the tab is an Essential or
  // pinned to its Space, empty otherwise.
  GURL pinned_url;
  // Pinned to its Space (one of Zen's pinned tabs) rather than an Essential.
  // Essentials are Chromium's pinned tabs; pinned tabs stay in their Space.
  bool pinned_in_space = false;
  // A reset asked for while the tab wasn't loaded: it goes to the pinned URL
  // when it is next shown (or restored), like Zen's reset of an unloaded tab,
  // without loading it now.
  bool reset_pending = false;
  // "Change Label…" / "Change Icon…": shown instead of the page's title and
  // favicon. The icon is an emoji.
  std::u16string custom_title;
  std::u16string custom_icon;

  // Not saved:
  // The page's icon at the pinned URL, shown by the reset button.
  ui::ImageModel pinned_icon;
  // The pinned URL the favicon database was last asked about.
  GURL pinned_icon_requested;
  // Set while upstream or a drag moves the tab: re-checked once things
  // settle (KoanPinnedTabController::Normalize()).
  bool was_essential = false;
  // Moved to another Space, or restored: back at the end of its Space's
  // pinned tabs if it isn't among them.
  bool changed_group = false;
  // Moved by a tab drag since the last check: Zen's drop rules apply. Other
  // moves (the keyboard, an extension) stay in their section, as Zen keeps
  // pinned and unpinned tabs apart (moveTabTo). (A move Koan didn't make
  // while this window's drag is on, or one in the moments between a drop
  // and the check, counts as dragged too: rare, and decided like a drop.)
  bool dragged = false;
  // Got its pinned URL when it was added, before the session knew the tab.
  bool unsaved = false;
  // While a tab drag holds the tab over its Space's list: the section it
  // would drop in (true: pinned), where the sidebar shows it, the separator
  // moving past it as Zen's does (ShowsInPinnedSection()). Unset otherwise.
  std::optional<bool> drop_pinned;

  bool empty() const;
  std::string Serialize() const;
  static KoanTabState Deserialize(std::string_view json);
};

// The state of `tab` / `contents`, or null if Koan has none.
KoanTabState* GetTabState(const tabs::TabInterface* tab);
KoanTabState* GetTabState(content::WebContents* contents);

// Whether `tab` is one of Zen's pinned tabs that stays in its Space.
bool IsPinnedInSpace(const tabs::TabInterface* tab);
// Whether `tab` is pinned and has navigated away from its pinned URL.
bool IsAwayFromPinnedUrl(const tabs::TabInterface* tab);
// Whether the sidebar shows `tab` in its Space's pinned section: pinned to
// it, or a dragged tab that would drop there (KoanTabState::drop_pinned).
bool ShowsInPinnedSection(const tabs::TabInterface* tab);
// Whether `tab` isn't loaded: unloaded, or restored and not shown yet.
bool IsPendingTab(const tabs::TabInterface* tab);
// Whether the close button of `tab` runs the close behaviour rather than
// closing it: a loaded tab pinned to its Space, unless the behaviour is to
// close. Zen shows its unload button ("Unload and switch to tab") there.
bool CloseButtonUnloads(const tabs::TabInterface* tab);

// Pinned tabs' part of the tab context menu (a TabMenuFeature, see
// koan_tab_menu.h).
void AddPinnedTabMenuItems(TabMenuContext& context);
bool IsPinnedTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                   int index,
                                   int command_id);
bool ExecutePinnedTabMenuCommand(TabStripModel* tab_strip,
                                 int index,
                                 int command_id);

// Pinned tabs' key in a tab's session data (koan_session_data.h), and
// their TabRestoreHook: a pinned tab comes back at its pinned URL after a
// reset it didn't load for, and on startup with Zen's
// zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url.
inline constexpr char kPinnedTabSessionKey[] = "koan.pinned_tab";
void OnRestoringPinnedTab(
    content::WebContents* contents,
    const std::string& data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation);

// Zen's pinned tab manager for one window: pinning to a Space, the
// Essentials (Chromium's pinned tabs, shared by all Spaces, at most 12),
// the pinned URL with reset / replace / edit, unloading, and what closing a
// pinned tab does (pref koan.pinned_tabs.close_shortcut_behavior, Zen's
// zen.pinned-tab-manager.close-shortcut-behavior).
//
// The context menu items, the reset button and the Close Tab command
// (chrome::CloseTab) call in here; Ctrl+Shift+D lives in
// KoanShortcutController.
class KoanPinnedTabController : public TabStripModelObserver {
 public:
  // Zen's zen.tabs.essentials.max.
  static constexpr int kMaxEssentials = 12;

  explicit KoanPinnedTabController(BrowserView* browser_view);
  KoanPinnedTabController(const KoanPinnedTabController&) = delete;
  KoanPinnedTabController& operator=(const KoanPinnedTabController&) = delete;
  ~KoanPinnedTabController() override;

  // The controller of the window `tab` is in, or null.
  static KoanPinnedTabController* ForTab(const tabs::TabInterface* tab);

  void OnBrowserViewDestroying();

  TabStripModel* tab_strip_model() const;
  int EssentialsCount() const;
  // Whether `count` more Essentials fit (a split view adds two).
  bool CanAddToEssentials(int count = 1) const;

  // Ctrl+Shift+D and the menu's Pin / Unpin: an Essential leaves the
  // Essentials, a pinned tab is unpinned, any other tab is pinned to its
  // Space. A split view is one unit everywhere, as in Zen: these and the
  // Essentials' operations take the other tab of the split along, and a
  // split's tabs are never one pinned and one not.
  void TogglePin(tabs::TabInterface* tab);
  void PinInSpace(tabs::TabInterface* tab);
  void UnpinFromSpace(tabs::TabInterface* tab);
  void AddToEssentials(tabs::TabInterface* tab);
  // The tab becomes a normal tab of the active Space, as in Zen.
  void RemoveFromEssentials(tabs::TabInterface* tab);
  // Zen's drops (moveToAnotherTabContainerIfNecessary()), once a tab drag is
  // over (koan_space_drag.h). `tabs` dropped on the Essentials grid become
  // Essentials, in their order, before the Essential `before` (null: after
  // the last one), while Zen's 12 fit; the others stay where they are. (A
  // split view goes after the last one, kept together.)
  void DropInEssentials(const std::vector<tabs::TabInterface*>& tabs,
                        tabs::TabInterface* before);
  // The Essential `tab` dropped on the active Space's list of tabs leaves
  // the Essentials for that place, before the Space's tab `before` (null:
  // after its last tab), in the section it was dropped in (Zen's
  // removeEssentials(tab, unpin)): in the pinned one (`pinned`) one of the
  // Space's pinned tabs, keeping its pinned URL; in the normal one a normal
  // tab, which forgets it.
  void DropEssentialInSpace(tabs::TabInterface* tab,
                            tabs::TabInterface* before,
                            bool pinned);
  // Space tabs dragged within their Space, where Chromium's drag places them
  // among its tabs (koan_space_drag.h). Zen pins or unpins them by the
  // section they are dropped in: between two pinned tabs pinned, after a
  // normal tab normal, and at the separator (where Chromium's place is the
  // same for both sides) by the section under the pointer, `pointer_pinned`
  // (DragDropsPinned()). While the drag goes on, PreviewDropInSpace() shows
  // each tab in that section (nullopt: in its own, e.g. the pointer is
  // elsewhere) and returns whether any moved section; DropInSpace() makes it
  // so once the drag is over: a tab dropped pinned keeps its pinned URL or
  // gets its current one (gBrowser.pinTab()), one dropped normal forgets it
  // (unpinTab()). Split views go as one.
  bool PreviewDropInSpace(const std::vector<tabs::TabInterface*>& tabs,
                          std::optional<bool> pointer_pinned);
  void DropInSpace(const std::vector<tabs::TabInterface*>& tabs,
                   bool pointer_pinned);
  // Duplicate (the tab menu, the menu bar, extensions) made `duplicate` from
  // `source`: Zen's duplicate of a pinned tab or Essential is a normal tab of
  // the active Space (SessionStore's duplicate path drops the pin), below the
  // pinned tabs. Chromium's stays an Essential, and a pinned tab's lands in
  // the pinned run.
  void OnTabDuplicated(const tabs::TabInterface* source,
                       tabs::TabInterface* duplicate);

  // Goes back to the pinned URL with a fresh history, like Zen's reset. A
  // tab that isn't loaded stays so and goes there when it is next shown.
  void ResetToPinnedUrl(tabs::TabInterface* tab);
  // The reset button: resets and selects the tab. With Ctrl (Cmd on macOS),
  // the current page first moves to a new tab ("Separate from pinned tab").
  void OnResetButtonPressed(tabs::TabInterface* tab, bool separate);
  void ReplacePinnedUrlWithCurrent(tabs::TabInterface* tab);
  // Asks for a new pinned URL ("Edit Pinned URL…") and resets the tab to it.
  void EditPinnedUrl(tabs::TabInterface* tab);
  void SetPinnedUrl(tabs::TabInterface* tab, const GURL& url);

  // "Change Label…": edits the label in place. An empty label goes back to
  // the page's title.
  void StartRenaming(tabs::TabInterface* tab);
  void SetCustomTitle(tabs::TabInterface* tab, std::u16string title);
  // "Change Icon…": an emoji picker. An empty icon goes back to the favicon.
  void ShowIconPicker(tabs::TabInterface* tab);
  void SetCustomIcon(tabs::TabInterface* tab, std::u16string icon);

  // Zen's "Unload": discards the tabs, switching away first if one of them
  // is the active tab.
  void UnloadTabs(const std::vector<tabs::TabInterface*>& tabs);

  // Chrome's Close Tab command (Ctrl+W, Cmd+W) with `tab` active: runs the
  // close behaviour for the selected pinned tabs and returns true, leaving
  // any other selected tab open (Zen); returns false (Chrome closes the
  // tabs) when none is pinned.
  bool HandleCloseShortcut(tabs::TabInterface* tab);
  // The close button or a middle click. Like Zen, an unloaded pinned tab
  // (not an Essential) closes.
  bool HandleCloseRequest(tabs::TabInterface* tab);

  // Writes the tab's state to the session.
  void SaveTabState(content::WebContents* contents);

  // Window sync (KoanWindowSyncClient): makes `tab` an Essential, or a
  // pinned tab of the Space it is in, with `pinned_url` and the label and
  // icon of "Change Label…" / "Change Icon…", as it is in another window.
  // None of the user's pinning rules run, no toast shows and the tab doesn't
  // navigate; the caller puts it in its place.
  void SetSyncedPin(tabs::TabInterface* tab,
                    bool essential,
                    const GURL& pinned_url,
                    const std::u16string& custom_title,
                    const std::u16string& custom_icon);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;

 private:
  class DiscardAfterReset;

  // Remembers the current URL and icon as the pinned ones, unless it has a
  // pinned URL already. Returns whether it set a pinned URL.
  bool InitPinnedState(tabs::TabInterface* tab, KoanTabState& state);
  // Looks the pinned URL's icon up in the favicon database.
  void FetchPinnedIcon(content::WebContents* contents);
  // `tab`, or both tabs of its split view.
  std::vector<tabs::TabInterface*> WithSplit(tabs::TabInterface* tab) const;
  // The first index of the section (Space, or the tabs in none) of the tab
  // at `index`.
  int SectionStart(int index) const;
  // The index of the last pinned-in-Space tab at the top of the section of
  // the tab at `index`, ignoring `ignore`; -1 if there is none.
  int LastPinnedInSection(int index,
                          const std::vector<tabs::TabInterface*>& ignore) const;
  // Whether the tab at `index` is among the pinned tabs at the top of its
  // section (they are all before it), or above one of them.
  bool InPinnedRun(int index) const;
  bool AbovePinnedTab(int index) const;
  // `tabs` with the other tab of each split view, in tab strip order, without
  // Essentials.
  std::vector<tabs::TabInterface*> SpaceTabsWithSplits(
      const std::vector<tabs::TabInterface*>& tabs) const;
  // Whether the Space tab `tab`, one of the dragged `dragged`, drops pinned
  // where it is (DropInSpace()).
  bool DropsPinned(tabs::TabInterface* tab,
                   const std::vector<tabs::TabInterface*>& dragged,
                   bool pointer_pinned) const;
  // Moves `tab` (with its split) right after the other pinned tabs at the
  // top of its Space, up or down: to the top of the normal tabs (unpinned
  // tabs and new tabs opened among the pinned ones).
  void MoveAfterPinnedTabs(tabs::TabInterface* tab);
  // A pinned `tab` (with its split) that isn't among its Space's pinned tabs
  // moves up to the end of them; one among them stays where it is.
  void MoveIntoPinnedTabs(tabs::TabInterface* tab);
  // Makes `tab` (with its split, kept together) an Essential, or moves it
  // out of the Essentials into the active Space. Its Koan state is the
  // caller's.
  void SetEssential(tabs::TabInterface* tab, bool essential);
  // A new split view of a pinned tab and a normal one: pins both.
  void MatchSplitPinnedState(split_tabs::SplitTabId split);
  // After tabs were added or their contents replaced: keeps pinned tabs
  // exempt from freezing, and tabs opened inside the pinned section below
  // it.
  void OnTabsAdded(std::vector<tabs::TabHandle> tabs);
  // Re-checks tabs after moves, drags and pin changes Koan didn't make, once
  // they settle (a drag moves tabs on every step): Zen's drop rules (a pinned
  // tab dragged out of the pinned tabs is unpinned, a tab dropped among them
  // is pinned, an Essential dropped there keeps its pinned URL) and the
  // Essentials' limit.
  void ScheduleNormalize(tabs::TabInterface* tab);

  // An Essential unpinned from outside Koan and not by a drag (an extension's
  // chrome.tabs.update()): upstream leaves it outside every Space, hidden, at
  // the end of the Essentials. It goes into the active Space at once, at the
  // top of its pinned tabs (Zen's pinned-tabs `prepend()`, the place a drop
  // there gives it), so that its row appears there as the tile leaves, with
  // no gap and no detour through the end of the list.
  void PlaceUnpinnedEssential(tabs::TabHandle handle);
  // A tab drag is on in this window (moving, or dragged in, here).
  bool IsDraggingHere() const;
  void Normalize();
  // Clears the tab's pinned state (it stays where it is).
  void ClearPinnedState(tabs::TabInterface* tab);
  // Selects the tab Zen would switch to when `tab` goes away, or a new tab.
  void SelectAnotherTab(tabs::TabInterface* tab);
  void RunCloseBehaviour(tabs::TabInterface* tab, std::string_view behavior);
  void Discard(content::WebContents* contents);
  // Refreshes the tab's row (reset button, label and icon).
  void UpdateTabView(tabs::TabInterface* tab);

  const raw_ptr<BrowserView> browser_view_;
  // Set while Koan itself changes a tab's pinned state.
  bool changing_pin_ = false;
  std::map<content::WebContents*, std::unique_ptr<DiscardAfterReset>>
      discard_after_reset_;
  std::vector<tabs::TabHandle> to_normalize_;
  base::OneShotTimer normalize_timer_;
  base::CancelableTaskTracker favicon_tracker_;

  base::WeakPtrFactory<KoanPinnedTabController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_PINNED_TABS_H_
