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
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class ExpandOnHoverLock;
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

namespace koan {

class KoanCreateMenu;
class KoanSpaceBarView;
class KoanSpaceCreationView;
class KoanSpaceHeaderView;
class KoanSpaceMenu;
class KoanThemeMenu;
class SlideOutLayers;

// Zen's Spaces for one window, built on upstream tab group focus mode
// (features::kTabGroupsFocusing): a Space is a tab group, the active Space
// is the focused group, so the sidebar shows only its tabs (plus the pinned
// tabs, Koan's Essentials, shared by every Space). Like Zen, the window
// always shows one Space: every unpinned tab belongs to one, and a Space
// whose last tab closes gets a new tab instead of disappearing.
//
// A Space's name, icon and theme are stored in its tab group (see
// ReadSpace()), so all of it is restored with the session.
//
// Owns the Space header above the tabs, the Create-a-Space form and the
// bottom bar of the sidebar. Keyboard shortcuts live in
// KoanShortcutController.
class KoanWorkspaceController : public TabStripModelObserver,
                                public views::ViewObserver,
                                public views::WidgetObserver {
 public:
  // A Space's theme. Zen gives each Space a gradient (its theme picker);
  // Koan tints the browser with one colour while the Space is active, or
  // keeps the user's theme (`color` unset, Zen's default theme).
  struct SpaceTheme {
    std::optional<tab_groups::TabGroupColorId> color;

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

  // The Spaces in sidebar order, and the active one.
  std::vector<SpaceInfo> GetSpaces() const;
  std::optional<tab_groups::TabGroupId> GetActiveSpace() const;
  std::optional<SpaceInfo> GetSpace(const tab_groups::TabGroupId& space) const;
  // Runs `callback` (soon, coalesced) whenever the Spaces or the active one
  // change.
  base::CallbackListSubscription AddSpacesChangedCallback(
      base::RepeatingClosure callback);

  // Shows `space`, on its last active tab.
  void SwitchTo(const tab_groups::TabGroupId& space);
  // The next or previous Space, wrapping around like Zen.
  void Cycle(bool forward);
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
  // Asks, then closes the Space's tabs and shows another Space.
  void DeleteSpace(const tab_groups::TabGroupId& space);
  // Moves the tabs to `space` and shows it on the last of them (Zen's "Move
  // To" in the tab context menu).
  void MoveTabsToSpace(const std::vector<tabs::TabInterface*>& tabs,
                       const tab_groups::TabGroupId& space);

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
  // Zen's mouse wheel over the sidebar (horizontal, or vertical with Ctrl):
  // one Space per notch, at most every 200 ms. Touchpad swipes are
  // upstream's VerticalTabStripFocusSwipeController.
  void OnSpaceWheel(bool forward);
  // Keeps the tab context menu's "Move To" submenu alive while the menu is
  // open (until the next tab menu: there is one at a time).
  void KeepTabMenuSubmenu(std::unique_ptr<ui::SimpleMenuModel> submenu);
  // Edit Theme...: Koan's colours until the theme picker lands.
  void EditSpaceTheme(const tab_groups::TabGroupId& space, views::View* anchor);

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

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabGroupChanged(const TabGroupChange& change) override;
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
  void OnSlideOutDone(SlideOutLayers* slide_out);
  // Drops the layers the animation gave the header and the tabs.
  void DropSwitchLayers();

  // Tints the browser with the active Space's theme, or restores the
  // user's colour for a Space with the default theme. The tint is the
  // profile's one colour, so only the window the user is in sets it, when
  // its Space (or the Space's theme) changes or the window becomes active.
  void UpdateTint();
  bool DecidesTint() const;
  void OnTintPrefChanged();

  void OnDeleteConfirmed(const tab_groups::TabGroupId& space, bool confirmed);

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<KoanSpaceHeaderView> header_ = nullptr;
  raw_ptr<KoanSpaceCreationView> creation_form_ = nullptr;
  raw_ptr<KoanSpaceBarView> bar_ = nullptr;
  // The sidebar (the vertical tab strip region), with the wheel handler.
  raw_ptr<views::View> region_ = nullptr;
  std::unique_ptr<ui::EventHandler> wheel_handler_;
  // Keeps a collapsed sidebar expanded while the Create form is open.
  std::unique_ptr<ExpandOnHoverLock> create_form_lock_;

  bool update_pending_ = false;
  // Spaces in order with their data, as of the last update.
  std::vector<std::pair<tab_groups::TabGroupId, SpaceData>> spaces_snapshot_;
  std::vector<ClosedSpace> closed_spaces_;
  // Spaces being deleted (their tabs are closing).
  std::set<tab_groups::TabGroupId> deleting_;
  // The Space shown last, for animating and as a fallback.
  std::optional<tab_groups::TabGroupId> last_space_;
  // Each Space's last active tab (switching back restores it).
  std::map<tab_groups::TabGroupId, tabs::TabHandle> last_active_tabs_;

  base::TimeTicks last_wheel_switch_;

  // The old Spaces sliding out, and the views the animation gave a layer.
  std::vector<std::unique_ptr<SlideOutLayers>> slide_outs_;
  std::vector<std::unique_ptr<views::ViewTracker>> switch_layered_views_;

  std::unique_ptr<KoanCreateMenu> create_menu_;
  std::vector<CreateMenuEntry> create_menu_entries_;
  std::unique_ptr<KoanSpaceMenu> space_menu_;
  std::unique_ptr<KoanThemeMenu> theme_menu_;
  std::unique_ptr<KoanEmojiPicker::Bubble> icon_picker_;
  std::unique_ptr<ui::SimpleMenuModel> tab_menu_submenu_;

  PrefChangeRegistrar pref_change_registrar_;
  base::RepeatingClosureList spaces_changed_callbacks_;
  // The views above, which go away with the vertical tab strip.
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      view_observations_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};

  base::WeakPtrFactory<KoanWorkspaceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
