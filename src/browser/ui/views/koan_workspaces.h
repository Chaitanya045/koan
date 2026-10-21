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
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/time/time.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "ui/views/view_observer.h"

class BrowserView;
class TabStripModel;

namespace gfx {
class Point;
}

namespace ui {
class MouseWheelEvent;
}

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

// Zen's Spaces for one window, built on upstream tab group focus mode
// (features::kTabGroupsFocusing): a Space is a tab group, the active Space
// is the focused group, so the sidebar shows only its tabs (plus the pinned
// tabs, Koan's Essentials, shared by every Space). Like Zen, the window
// always shows one Space: every unpinned tab belongs to one, and a Space
// whose last tab closes gets a new tab instead of disappearing.
//
// A Space's name and icon are its group title ("<emoji> <name>"), its theme
// is the group colour, so all of it is restored with the session.
//
// Owns the Space header above the tabs, the Create-a-Space form and the
// bottom bar of the sidebar. Keyboard shortcuts live in
// KoanShortcutController.
class KoanWorkspaceController : public TabStripModelObserver,
                                public views::ViewObserver {
 public:
  // A Space's theme. Zen gives each Space a gradient (its theme picker);
  // Koan tints the browser with one colour while the Space is active, or
  // keeps the user's theme (`color` unset, Zen's default theme).
  struct SpaceTheme {
    std::optional<tab_groups::TabGroupColorId> color;

    bool operator==(const SpaceTheme&) const = default;
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

  explicit KoanWorkspaceController(BrowserView* browser_view);
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
  // Frees the Space's tabs (discards them; they reload when selected), like
  // Zen's Unload Space. The active tab stays loaded.
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
  // The "+" menu: Create Space, Create Folder, New Split, New Tab.
  void ShowCreateMenu(views::View* anchor);
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
  // Zen switches Spaces with a horizontal mouse wheel over the sidebar
  // (touchpad swipes are upstream's VerticalTabStripFocusSwipeController).
  bool OnMouseWheel(const ui::MouseWheelEvent& event);
  // Edit Theme...: Koan's colours until the theme picker lands.
  void EditSpaceTheme(const tab_groups::TabGroupId& space, views::View* anchor);

  // Splits a group title into its emoji icon (empty for a plain-text name)
  // and name. A leading emoji is one grapheme cluster, so ZWJ sequences and
  // flags stay whole.
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

 private:
  // A Space whose last tab went away; it comes back with a new tab.
  struct ClosedSpace {
    tab_groups::TabGroupId id;
    tab_groups::TabGroupVisualData visuals;
    // The Space it was in front of, if any.
    std::optional<tab_groups::TabGroupId> next;
    bool was_active = false;
  };

  void InstallViews();
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

  tab_groups::TabGroupId NewSpaceGroup(
      const std::vector<int>& indices,
      const tab_groups::TabGroupVisualData& visuals);
  // The index at which a Space created after `after` starts.
  int InsertionIndexAfter(std::optional<tab_groups::TabGroupId> after) const;

  // Activates the Space's last active tab, if it still has one.
  void RestoreLastActiveTab(const tab_groups::TabGroupId& space);
  // Zen's switch animation: the old Space slides out, the new one in.
  void PlaySwitchAnimation(const tab_groups::TabGroupId& from,
                           const tab_groups::TabGroupId& to);

  // Tints the browser with the active Space's theme, or restores the
  // user's colour for a Space with the default theme.
  void UpdateTint();
  void OnTintPrefChanged();

  void OnDeleteConfirmed(const tab_groups::TabGroupId& space, bool confirmed);

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<KoanSpaceHeaderView> header_ = nullptr;
  raw_ptr<KoanSpaceCreationView> creation_form_ = nullptr;
  raw_ptr<KoanSpaceBarView> bar_ = nullptr;

  bool update_pending_ = false;
  // Spaces in order with their looks, as of the last update.
  std::vector<std::pair<tab_groups::TabGroupId, tab_groups::TabGroupVisualData>>
      spaces_snapshot_;
  std::vector<ClosedSpace> closed_spaces_;
  // Spaces being deleted (their tabs are closing).
  std::set<tab_groups::TabGroupId> deleting_;
  // The Space shown last, for animating and as a fallback.
  std::optional<tab_groups::TabGroupId> last_space_;
  // The Space focus mode was just left (until the next update), to tell a
  // swipe past the first or last Space.
  std::optional<tab_groups::TabGroupId> exited_space_;
  // Each Space's last active tab (switching back restores it).
  std::map<tab_groups::TabGroupId, tabs::TabHandle> last_active_tabs_;

  base::TimeTicks last_wheel_switch_;

  std::unique_ptr<KoanCreateMenu> create_menu_;
  std::unique_ptr<KoanSpaceMenu> space_menu_;
  std::unique_ptr<KoanThemeMenu> theme_menu_;
  std::unique_ptr<KoanEmojiPicker::Bubble> icon_picker_;

  PrefChangeRegistrar pref_change_registrar_;
  base::RepeatingClosureList spaces_changed_callbacks_;
  // The views above, which go away with the vertical tab strip.
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      view_observations_{this};

  base::WeakPtrFactory<KoanWorkspaceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WORKSPACES_H_
