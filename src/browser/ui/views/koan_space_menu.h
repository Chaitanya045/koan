// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_MENU_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_MENU_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view_tracker.h"

namespace gfx {
class Point;
}

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace koan {

// Zen's Space menu (zenWorkspaceMoreActions), for one Space or, opened from
// the header, for the active Space with the list of Spaces to switch to.
class KoanSpaceMenu : public ui::SimpleMenuModel::Delegate {
 public:
  KoanSpaceMenu(KoanWorkspaceController* controller,
                std::optional<tab_groups::TabGroupId> space);
  KoanSpaceMenu(const KoanSpaceMenu&) = delete;
  KoanSpaceMenu& operator=(const KoanSpaceMenu&) = delete;
  ~KoanSpaceMenu() override;

  // A context menu at `screen_point`; `anchor` is what the menu was opened
  // on (the icon picker and theme menu anchor there).
  void RunMenuAt(views::View* anchor, const gfx::Point& screen_point);

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;

 private:
  void Execute(int command_id);

  const raw_ptr<KoanWorkspaceController> controller_;
  // The Space the menu acts on.
  const tab_groups::TabGroupId space_;
  // Opened from the header: also lists the Spaces.
  const bool from_header_;
  ui::SimpleMenuModel model_{this};
  std::map<int, tab_groups::TabGroupId> switch_commands_;
  views::ViewTracker anchor_;
  std::unique_ptr<views::MenuRunner> runner_;
  base::WeakPtrFactory<KoanSpaceMenu> weak_factory_{this};
};

// Zen's "+" menu (zenCreateNewPopup): the controller's create menu
// entries, by section (KoanWorkspaceController::AddCreateMenuEntry()).
class KoanCreateMenu : public ui::SimpleMenuModel::Delegate {
 public:
  explicit KoanCreateMenu(KoanWorkspaceController* controller);
  KoanCreateMenu(const KoanCreateMenu&) = delete;
  KoanCreateMenu& operator=(const KoanCreateMenu&) = delete;
  ~KoanCreateMenu() override;

  void RunMenuAt(views::View* anchor);

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;

 private:
  // The entries as they were when the menu opened, by command id - 1.
  const std::vector<KoanWorkspaceController::CreateMenuEntry> entries_;
  ui::SimpleMenuModel model_{this};
  std::unique_ptr<views::MenuRunner> runner_;
};

// Edit Theme...: the theme a Space can take in Koan (Default, which keeps the
// browser's colour, or one of the tab group colours).
class KoanThemeMenu : public ui::SimpleMenuModel::Delegate {
 public:
  using PickedCallback =
      base::RepeatingCallback<void(const KoanWorkspaceController::SpaceTheme&)>;

  KoanThemeMenu(const KoanWorkspaceController::SpaceTheme& current,
                PickedCallback on_pick);
  KoanThemeMenu(const KoanThemeMenu&) = delete;
  KoanThemeMenu& operator=(const KoanThemeMenu&) = delete;
  ~KoanThemeMenu() override;

  void RunMenuAt(views::View* anchor);

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  KoanWorkspaceController::SpaceTheme ThemeFor(int command_id) const;

  const KoanWorkspaceController::SpaceTheme current_;
  const PickedCallback on_pick_;
  ui::SimpleMenuModel model_{this};
  std::unique_ptr<views::MenuRunner> runner_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_MENU_H_
