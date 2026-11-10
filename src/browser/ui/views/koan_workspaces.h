// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "koan/browser/ui/views/koan_swipe_tracker.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class ExpandOnHoverLock;
class PrefService;
class Profile;
class TabStripModel;

namespace gfx {
class Point;
struct VectorIcon;
}  // namespace gfx

namespace ui {
class EventHandler;
class SimpleMenuModel;
}  // namespace ui

namespace views {
class View;
}

namespace ui {
class Event;
class MouseEvent;
class ScrollEvent;
}  // namespace ui

namespace koan {

class KoanSpaceSwipe;

class KoanCreateMenu;
class KoanSpaceBarView;
class KoanSpaceDrag;
class KoanSpaceRouting;
class KoanSpaceCreationView;
class KoanSpaceHeaderView;
class KoanSpaceMenu;
class SlideOutLayers;

// Zen's Spaces for one window, built on upstream tab group focus mode
// (features::kTabGroupsFocusing): a Space is a tab group, the active Space
// is the focused group, so the sidebar shows only its tabs (plus the pinned
// tabs, Koan's Essentials, shared by every Space). Like Zen, the window
// always shows one Space: every unpinned tab belongs to one, and a Space
// whose last tab closes gets a new tab instead of disappearing.
//
// A Space's name and icon are stored in its tab group, and its theme with
// its tabs (see ReadSpace()), so all of it is restored with the session; the
// list is also kept in a profile pref (prefs::kWorkspaceList), for a window
// that opens without a session restore (RecreateStoredSpaces()).
//
// Upstream's focus mode is only the mechanism: its side effects in a Spaces
// window are Koan's (the window colour, ApplySpaceColor(), which the Space
// theme's KoanThemeController sets; no freezing of the other Spaces,
// KeepsOtherSpacesRunning()), and upstream's tab group UI never runs there
// (the group commands and menus, the group editor, collapsed groups). See
// .agent/notes/upstream/tab-groups-omnibox-fullscreen.md.
//
// Owns the Space header above the tabs, the Create-a-Space form and the
// bottom bar of the sidebar. Keyboard shortcuts live in
// KoanShortcutController.
class KoanWorkspaceController : public TabStripModelObserver,
                                public views::ViewObserver,
                                public views::WidgetObserver {
 public:
  // A Space's theme, Zen's gradient from its theme picker
  // (KoanThemeController): a theme family's stored dict as JSON (see
  // theme::GetTokens()), empty for Zen's default theme.
  struct SpaceTheme {
    std::string stored;

    bool is_default() const { return stored.empty(); }
    bool operator==(const SpaceTheme&) const = default;
  };

  // What a Space stores: what the user picks for it.
  struct SpaceData {
    std::u16string name;
    // An emoji, or empty.
    std::u16string icon;
    SpaceTheme theme;
  };

  // What the UI shows for a Space (the bottom bar, menus, the Library).
  struct SpaceInfo {
    tab_groups::TabGroupId id;
    std::u16string name;
    // An emoji, or empty: Zen shows a dot for a Space without an icon.
    std::u16string icon;
    SpaceTheme theme;
    bool active = false;
  };

  // An entry of the bottom bar's "+" menu (Zen's zenCreateNewPopup).
  // Features add theirs (AddCreateMenuEntry()): Spaces' Create Space,
  // folders' New Folder, split view's New Split, ...
  struct CreateMenuEntry {
    CreateMenuEntry();
    CreateMenuEntry(const CreateMenuEntry&);
    CreateMenuEntry& operator=(const CreateMenuEntry&);
    ~CreateMenuEntry();

    std::u16string label;
    raw_ptr<const gfx::VectorIcon> icon = nullptr;
    // The menu's sections, in this order, with a separator between them;
    // entries of a section in the order they were added.
    enum class Section { kCreate, kTabs };
    Section section = Section::kCreate;
    // Null: always enabled.
    base::RepeatingCallback<bool()> is_enabled;
    base::RepeatingClosure run;
    // Names the item for UI DevTools (e2e tests), if set.
    ui::ElementIdentifier element_id;
  };

  explicit KoanWorkspaceController(BrowserView* browser_view);
  // The controller of the window `tab_strip` belongs to, if Spaces are on
  // there.
  static KoanWorkspaceController* FromTabStrip(TabStripModel* tab_strip);

  KoanWorkspaceController(const KoanWorkspaceController&) = delete;
  KoanWorkspaceController& operator=(const KoanWorkspaceController&) = delete;
  ~KoanWorkspaceController() override;

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether Spaces apply to this window (feature on, normal window, not
  // private).
  bool IsEnabled() const;

  // The window's last tab closed and an empty tab is on its way (see
  // KeepWindowOnLastTabClosed()): keep the closed Space's data meanwhile.
  void ExpectEmptyTab() { expect_empty_tab_ = true; }
  // The empty tab KeepWindowOnLastTabClosed() added: this very tab is the
  // Space's empty tab (not one found by its URL, which a new tab page set by
  // policy would not show, nor some other blank tab).
  void AdoptEmptyTab(tabs::TabHandle tab) { empty_tab_ = tab; }
  // Whether the tabs that left the strip last were closed (and not moved to
  // another window, which Zen follows by closing this one).
  bool LastTabsWereClosed() const { return last_removal_closed_; }

  // The Spaces in sidebar order, and the active one.
  std::vector<SpaceInfo> GetSpaces() const;
  std::optional<tab_groups::TabGroupId> GetActiveSpace() const;
  std::optional<SpaceInfo> GetSpace(const tab_groups::TabGroupId& space) const;
  // A Space's id in the stored Spaces (prefs::kWorkspaceList's "id"), made
  // the first time it's asked for and kept while the window is open. A fresh
  // start gives each Space it recreates its stored id back, so what other
  // features store per Space (folders) finds its Space again rather than the
  // one at its old place.
  std::string GetStoredSpaceId(const tab_groups::TabGroupId& space);
  // Runs `callback` (soon, coalesced) whenever the Spaces or the active one
  // change.
  base::CallbackListSubscription AddSpacesChangedCallback(
      base::RepeatingClosure callback);

  // Shows `space`, on its last active tab.
  void SwitchTo(const tab_groups::TabGroupId& space);
  // The next or previous Space, wrapping around like Zen.
  void Cycle(bool forward);
  // The same for the user's own moves (shortcuts, the wheel, swipes), as
  // Zen's settings say (koan_prefs.h kWorkspacesWrapAround): without
  // wrap-around, nothing past the first or last Space. Returns whether it
  // switched.
  bool Navigate(bool forward);
  // Zen's wrap-around-navigation setting.
  bool WrapsAround() const;
  // The `number`th Space (from 1), Zen's "Switch to Workspace N". False if
  // there is none.
  bool SwitchToNumber(int number);
  // The tab `space` was left on (Zen comes back to it), if it still has it:
  // the one remembered when the window left the Space, else (e.g. after a
  // restart) its most recently shown tab. Upstream's EnterFocusMode() asks
  // through PickSpaceTab() (koan_spaces.h), so every way of switching
  // (including upstream's swipe) lands there without passing through the
  // Space's first tab.
  tabs::TabInterface* GetLastActiveTab(const tab_groups::TabGroupId& space);

  // Creates a Space (with a new tab, right after the active Space) and
  // shows it.
  void CreateSpace(const std::u16string& name,
                   const std::u16string& icon,
                   const SpaceTheme& theme);
  // Makes the ungrouped tab at `index` a new Space with `data`, without
  // showing it or changing the active tab (an agent container's Space,
  // koan/browser/containers/koan_agent_containers.h).
  tab_groups::TabGroupId CreateBackgroundSpace(int index,
                                               const SpaceData& data);
  // Makes a Space of the tabs at `indices` (sorted), right after the active
  // Space, and shows it on the last of them (folders' "Convert folder to
  // Space").
  tab_groups::TabGroupId CreateSpaceFromTabs(const std::vector<int>& indices,
                                             const SpaceData& data);
  void RenameSpace(const tab_groups::TabGroupId& space,
                   const std::u16string& name);
  // `icon` empty removes the icon.
  void SetSpaceIcon(const tab_groups::TabGroupId& space,
                    const std::u16string& icon);
  SpaceTheme GetSpaceTheme(const tab_groups::TabGroupId& space) const;
  void SetSpaceTheme(const tab_groups::TabGroupId& space,
                     const SpaceTheme& theme);
  // A Space's stored data. The one place that knows how it is stored (in
  // the tab group: title "<icon> <name>", colour = theme, grey = the
  // default theme); everything else goes through these, so storage richer
  // than a group colour (per-Space themes, #41) changes only them.
  std::optional<SpaceData> ReadSpace(const tab_groups::TabGroupId& space) const;
  void WriteSpace(const tab_groups::TabGroupId& space, const SpaceData& data);
  // Frees the Space's tabs (discards them; they reload when selected), like
  // Zen's Unload Space. Tabs Memory Saver wouldn't discard stay loaded: the
  // tabs on screen, and those playing sound, capturing, with unsaved form
  // input, open in DevTools and so on (a toast says how many).
  void UnloadSpace(const tab_groups::TabGroupId& space);
  void UnloadAllOtherSpaces(const tab_groups::TabGroupId& space);
  // Zen keeps at least one Space.
  bool CanDeleteSpace() const;
  // Has the window take its Space theme's colours again
  // (koan::ApplySpaceColor(), upstream's focus mode): KoanThemeController is
  // their one writer.
  void ApplySpaceColor();
  // Asks, then closes the Space's tabs and shows another Space.
  void DeleteSpace(const tab_groups::TabGroupId& space);
  // Window sync (KoanWindowSyncClient): `space` was deleted in another
  // window. Closes its tabs at once, without asking and without adding them
  // to Recently Closed (that window did).
  void DeleteSpaceForSync(const tab_groups::TabGroupId& space);
  // Puts `space` at position `to` among the Spaces (dragging a card in the
  // Library's Spaces, like Zen's).
  void MoveSpace(const tab_groups::TabGroupId& space, int to);
  // Moves the tabs to `space` and shows it on the last of them (Zen's "Move
  // To" in the tab context menu).
  void MoveTabsToSpace(const std::vector<tabs::TabInterface*>& tabs,
                       const tab_groups::TabGroupId& space);

  // Upstream's tab group command `command_id` (IDC_CREATE_NEW_TAB_GROUP,
  // IDC_CLOSE_TAB_GROUP, ...) as a Spaces window runs it: Zen's equivalent
  // (the Create form, Delete Space, the Space cycle, Koan's new tab) or
  // nothing. False for any other command. The window's ActionItems and the
  // legacy browser command path both come here (koan_command_policy.cc).
  bool RunGroupCommand(int command_id);

  // Zen's UI entry points.
  // The Create-a-Space form in the sidebar (Ctrl+Alt+N, "+" > Create Space).
  void ShowCreateForm();
  void CloseCreateForm();
  bool IsCreateFormOpen() const;
  // The "+" menu: the entries features added, in sections (Zen's: Create
  // Space, Create Folder | New Split, New Tab).
  void ShowCreateMenu(views::View* anchor);
  void AddCreateMenuEntry(CreateMenuEntry entry);
  const std::vector<CreateMenuEntry>& create_menu_entries() const {
    return create_menu_entries_;
  }
  // The Space menu (right-click on the header or a bottom bar icon). Without
  // `space` it is for the active Space and also lists the Spaces to switch
  // to, like Zen's header menu.
  void ShowSpaceMenu(std::optional<tab_groups::TabGroupId> space,
                     views::View* anchor,
                     const gfx::Point& screen_point);
  // Renames the active Space in place, in the header.
  void StartRename();
  // Zen's emoji picker for the Space's icon.
  void ShowIconPicker(const tab_groups::TabGroupId& space, views::View* anchor);
  // The Spaces menu bar's Change Icon… and Theme… for the active Space
  // (Zen's cmd_zenChangeWorkspaceIcon / cmd_zenOpenZenThemePicker), anchored
  // on the Space's header. Nothing without a visible header.
  void ShowIconPickerForActiveSpace();
  void EditActiveSpaceTheme();
  // Zen's mouse wheel over the sidebar (horizontal, or vertical with the
  // modifier of kWorkspacesScrollModifierKey): one Space per notch, at most
  // every 200 ms; `forward` is the wheel's way (down or right), which
  // kWorkspacesNaturalScroll turns round. Touchpad swipes are
  // KoanSpaceSwipe's on macOS, upstream's VerticalTabStripFocusSwipeController
  // elsewhere.
  void OnSpaceWheel(bool forward);
  // Whether a vertical wheel with these modifiers switches Space.
  bool IsSpaceScrollModifierDown(const ui::Event& event) const;
  // Zen's two-finger swipe (koan_swipe_tracker.h, koan_space_swipe.h): the
  // Space follows the fingers by the gesture `amount` (positive: fingers moving
  // left; the strip moves the other way, as in Zen), then EndSwipe() when they
  // lift, with the last movement's `velocity` (amounts per second): it switches
  // to the next (fingers moved right) or previous Space, or lets the Space
  // spring back. Both do nothing where the sidebar shows no Space to swipe
  // (one Space, the Create form open).
  void UpdateSwipe(float amount);
  void EndSwipe(float amount, float velocity);
  // A touchpad scroll over the sidebar (macOS): whether it is part of a swipe,
  // which then takes it.
  bool OnSwipeScroll(const ui::ScrollEvent& event);
  // Abandons a swipe in progress: the end of the gesture can't be relied on to
  // arrive (the sidebar or its header went away, compact mode toggled, the
  // window lost focus, the fingers rested too long). The Space springs back
  // with `animate`, else is put back at once.
  void CancelSwipe(bool animate);
  // Keeps the tab context menu's "Move To" submenu alive while the menu is
  // open (until the next tab menu: there is one at a time).
  void KeepTabMenuSubmenu(std::unique_ptr<ui::SimpleMenuModel> submenu);
  // Theme...: Zen's theme picker for `space`, where Space themes are
  // on (see KoanThemeController::IsEnabled()).
  void EditSpaceTheme(const tab_groups::TabGroupId& space);
  bool CanEditSpaceTheme() const;
  // The Create-a-Space form's Theme...: the picker for a Space not
  // created yet, from `theme`; `on_done` gets the theme when it closes.
  void EditNewSpaceTheme(const SpaceTheme& theme,
                         base::OnceCallback<void(const SpaceTheme&)> on_done);

  // Splits a group title into its emoji icon (empty for a plain-text name)
  // and name. A leading emoji is one grapheme cluster, so ZWJ sequences and
  // flags stay whole; a symbol that is text by default (e.g. (c), arrows)
  // is only an icon with the emoji variation selector.
  static std::pair<std::u16string, std::u16string> SplitTitle(
      const std::u16string& title);
  static std::u16string JoinTitle(const std::u16string& icon,
                                  const std::u16string& name);

  TabStripModel* tab_strip_model() const;
  base::WeakPtr<KoanWorkspaceController> GetWeakPtr();

  // The sidebar's Space views, while they are installed (null otherwise).
  KoanSpaceHeaderView* space_header() const { return header_; }
  KoanSpaceBarView* space_bar() const { return bar_; }
  // Zen's tab drag across Spaces (koan_space_drag.h).
  KoanSpaceDrag* space_drag() const { return space_drag_.get(); }
  // A mouse event on its way to a view in the sidebar.
  void OnSidebarMouseEvent(const ui::MouseEvent& event);

  // Zen's Space routing (koan_space_routing.h).
  KoanSpaceRouting* space_routing() const { return space_routing_.get(); }
  // Makes `tab` the one switching to `space` lands on (a routed tab, Zen's
  // lastSelectedWorkspaceTabs).
  void SetLastActiveTab(const tab_groups::TabGroupId& space,
                        tabs::TabInterface* tab);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;
  void OnTabGroupAdded(const tab_groups::TabGroupId& group_id) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;
  void OnTabGroupFocusChanged(
      std::optional<tab_groups::TabGroupId> new_focused_group_id,
      std::optional<tab_groups::TabGroupId> old_focused_group_id) override;

  // views::ViewObserver:
  void OnViewIsDeleting(views::View* observed_view) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // A Space whose last tab went away; it comes back with a new tab.
  struct ClosedSpace {
    tab_groups::TabGroupId id;
    SpaceData data;
    // The Space it was in front of, if any.
    std::optional<tab_groups::TabGroupId> next;
    bool was_active = false;
  };

  void InstallViews();
  // The "+" menu's own entries (Create Space, New Split, New Tab).
  void AddDefaultCreateMenuEntries();
  // Brings the model back to "one Space shown" soon (coalesced): never from
  // inside a model notification, which can't modify the model.
  void ScheduleUpdate();
  void Update();
  // Brings back closed Spaces, puts loose (ungrouped, unpinned) tabs into a
  // Space and focuses one.
  void EnsureOneSpaceShown();
  // A session restored into this window (Restore after a crash) leaves the
  // Space the window started in, with its new tab page: closes it (Zen's
  // restore adds no Space). Only that Space, and only if it is untouched.
  void DropStartupSpaceAfterRestore();
  // Whether `space` is the window's one Space from before a session restore
  // that is still one untouched new tab page: what restoring replaces.
  bool IsUntouchedStartupSpace(const tab_groups::TabGroupId& space) const;
  void OnSessionRestored(Profile* profile, int tab_count);
  // Upstream's group state a Space can't have: collapsed (its tabs hidden,
  // with no header to expand it), and a Chrome colour on a group that didn't
  // come from Koan (an extension's tabs.group, Chrome's group shortcuts): it
  // gets the default theme.
  void NormalizeSpaces();
  // Tabs reopened into a Space that is gone for good (deleted, or living in
  // another window): they join the Space shown, so a deleted Space stays
  // deleted and a group id stays in one window.
  void AdoptResurrectedSpaces();
  // For a window that opened with no Spaces while the profile has no other
  // window (a start that restored no session: startup set to the New Tab page
  // or to pages; macOS's new window after the last one closed): the Spaces
  // from prefs::kWorkspaceList, the active one taking `loose` (the window's
  // first tabs). True if it made any.
  bool RecreateStoredSpaces(const std::vector<int>& loose);
  // The tabs taken out of `space` that bring it back when it closed (its last
  // tabs were ungrouped): the indices of one loose tab, or of a whole split;
  // empty if none.
  std::vector<int> ReclaimUngroupedTabs(const tab_groups::TabGroupId& space);
  // Keeps prefs::kWorkspaceList up to date from the window the user is in.
  void StoreSpaces();
  // Drops the connection a Space has to a saved tab group (an old profile's,
  // opened from the bookmarks bar or the everything menu): no sync, no
  // Chrome "Delete group?" dialog.
  void DisconnectSavedGroups();
  // Closes upstream's tab group editor, which has nothing to edit here.
  void CloseGroupEditors();
  // Debug builds: fails loudly if upstream state leaked into the Spaces
  // (G14 of .agent/notes/upstream/tab-groups-omnibox-fullscreen.md).
  void CheckSpaces();
  // Once, when the window first has tabs: marks the new tab page it opened
  // on as Zen's empty tab (IsEmptyTab()).
  void MarkStartupEmptyTab();
  // Once, when the window first has tabs: puts back the Essential it opened
  // on.
  void KeepStartupEssential();
  void UpdateViews();
  // Remembers the order and looks of the Spaces, to restore a closed one.
  void SnapshotSpaces();

  tab_groups::TabGroupId NewSpaceGroup(const std::vector<int>& indices);
  // UnloadSpace() without the toast; returns how many tabs stayed loaded.
  int DiscardSpaceTabs(const tab_groups::TabGroupId& space);
  void ShowKeptLoadedToast(int kept);
  // The index at which a Space created after `after` starts.
  int InsertionIndexAfter(std::optional<tab_groups::TabGroupId> after) const;

  // Zen's switch animation: the old Space slides out, the new one in.
  void PlaySwitchAnimation(const tab_groups::TabGroupId& from,
                           const tab_groups::TabGroupId& to);
  // The views that slide when the Space changes: the header and the tabs.
  // Gives each a layer for it (dropped by DropSwitchLayers()).
  std::vector<views::View*> GetSlidingViews();
  bool CanSwipe() const;
  void OnSwipeTimeout();
  // Lets the views the swipe dragged go back to rest.
  void SpringBackSwipe();
  void OnSlideOutDone(SlideOutLayers* slide_out);
  // Drops the layers the animation gave the header and the tabs.
  void DropSwitchLayers();

  void OnTintPrefChanged();
  // The theme the tabs of `space` other than `except` carry (see
  // ReadSpace()), or nullopt if none does.
  std::optional<std::string> ThemeOfTabs(
      const tab_groups::TabGroupId& space,
      const tabs::TabInterface* except) const;
  // A swipe's travel or speed, turned round with Zen's natural-scroll
  // setting, so the Space follows the way it will switch.
  float SwipeAmount(float amount) const;
  // The profile's prefs, also for an Incognito window (koan_prefs.h).
  PrefService* GetPrefs() const;

  void OnDeleteConfirmed(const tab_groups::TabGroupId& space, bool confirmed);
  void CloseSpaceTabs(const tab_groups::TabGroupId& space,
                      uint32_t close_types);

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<KoanSpaceHeaderView> header_ = nullptr;
  raw_ptr<KoanSpaceCreationView> creation_form_ = nullptr;
  raw_ptr<KoanSpaceBarView> bar_ = nullptr;
  // The sidebar (the vertical tab strip region), with the wheel handler.
  raw_ptr<views::View> region_ = nullptr;
  std::unique_ptr<ui::EventHandler> wheel_handler_;
  std::unique_ptr<KoanSpaceDrag> space_drag_;
  std::unique_ptr<KoanSpaceRouting> space_routing_;
  // Keeps a collapsed sidebar expanded while the Create form is open.
  std::unique_ptr<ExpandOnHoverLock> create_form_lock_;

  bool update_pending_ = false;
  // The browser view is going away: no more updates.
  bool destroying_ = false;
  // Koan is creating a group for a Space, or writing a Space's data (so the
  // group isn't foreign, the visuals are Koan's).
  bool creating_space_ = false;
  bool writing_space_ = false;
  // Tabs taken out of a Space since the last update, and the Space each left
  // (it gets them back).
  std::map<tabs::TabHandle, tab_groups::TabGroupId> ungrouped_from_;
  // Groups created outside Koan, still to get the default theme.
  std::set<tab_groups::TabGroupId> foreign_groups_;
  // Groups added with an id they had before (tab restore, a restored
  // window, Koan's own empty Space), to check in the next update: the id of
  // a Space gone for good (AdoptResurrectedSpaces()).
  std::set<tab_groups::TabGroupId> resurrected_;
  // What prefs::kWorkspaceList last got from this window, and whether this
  // window may write it yet (not after a crash until its Spaces change: the
  // user may still restore the session).
  base::ListValue stored_spaces_;
  std::optional<base::ListValue> first_spaces_;
  bool may_store_spaces_ = true;
  // GetStoredSpaceId()'s ids.
  std::map<tab_groups::TabGroupId, std::string> stored_space_ids_;
  // Spaces in order with their data, as of the last update.
  std::vector<std::pair<tab_groups::TabGroupId, SpaceData>> spaces_snapshot_;
  std::vector<ClosedSpace> closed_spaces_;
  // Spaces being deleted (their tabs are closing).
  std::set<tab_groups::TabGroupId> deleting_;
  // A session restore is adding Spaces to this window (set by its first new
  // Space), the window's only Space from before it, and whether the restore
  // finished, so that Space is to go if it is still untouched.
  bool restore_started_ = false;
  std::optional<tab_groups::TabGroupId> pre_restore_space_;
  bool drop_startup_space_ = false;
  base::CallbackListSubscription session_restored_subscription_;
  // The Space shown last, for animating and as a fallback.
  std::optional<tab_groups::TabGroupId> last_space_;
  // Whether the first update with tabs ran, and the Essential selected
  // before it.
  bool started_ = false;
  // The window's first tab while it is a fresh empty page: its startup tab.
  tabs::TabHandle startup_tab_;
  bool expect_empty_tab_ = false;
  tabs::TabHandle empty_tab_;
  bool last_removal_closed_ = false;
  tabs::TabHandle startup_essential_;
  // Each Space's last active tab (switching back restores it).
  std::map<tab_groups::TabGroupId, tabs::TabHandle> last_active_tabs_;

  base::TimeTicks last_wheel_switch_;

  // The swipe: where the Space is dragged to (koan_swipe_tracker.h), the
  // events' tracker, and the watchdog that abandons a swipe whose end never
  // came.
  SwipeDrag drag_;
  std::unique_ptr<KoanSpaceSwipe> swipe_;
  base::OneShotTimer swipe_timer_;

  // The old Spaces sliding out, and the views the animation gave a layer.
  std::vector<std::unique_ptr<SlideOutLayers>> slide_outs_;
  std::vector<std::unique_ptr<views::ViewTracker>> switch_layered_views_;

  std::unique_ptr<KoanCreateMenu> create_menu_;
  std::vector<CreateMenuEntry> create_menu_entries_;
  std::unique_ptr<KoanSpaceMenu> space_menu_;
  std::unique_ptr<KoanEmojiPicker::Bubble> icon_picker_;
  std::unique_ptr<ui::SimpleMenuModel> tab_menu_submenu_;

  PrefChangeRegistrar pref_change_registrar_;
  base::RepeatingClosureList spaces_changed_callbacks_;
  // The views above, which go away with the vertical tab strip.
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      view_observations_{this};
  // Cancels a swipe when the window stops being key (#91).
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};

  base::WeakPtrFactory<KoanWorkspaceController> weak_factory_{this};
};

#if BUILDFLAG(IS_MAC)
// Whether the macOS menu bar's Tab > Group Selected Tab is the Option
// alternate of Group Tab (upstream's menu), or a plain item that the menu
// validation hides (a Spaces window is key). koan_workspaces_mac.mm.
void SetTabGroupMenuAlternate(bool alternate);
#endif

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
