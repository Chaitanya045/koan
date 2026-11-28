// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_EXTENSIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_EXTENSIONS_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"

class BrowserView;
class Profile;

namespace views {
class MenuRunner;
}

// Extensions per Space profile (#473,
// .agent/notes/upstream/space-extensions.md): the UI that turns an extension
// off in a Space's profile, and what applies it in a window. The rule itself,
// which upstream's hooks ask, is koan_container_extensions.h.
namespace koan {

class KoanWorkspaceController;

namespace containers {
struct ContainerId;
}

// The Space menu's Extensions...: a dialog with the installed extensions and a
// switch each for `space`'s profile. Done saves the list with the profile
// (koan.containers.user) and, if it changed, restarts the profile's tabs
// (RestartProfileTabs()). A Space without a profile (the default partition)
// can't have a list: turning an extension off there asks for a new profile's
// name, gives the Space that profile with the extension off, and opens the
// dialog again.
void ShowSpaceExtensions(KoanWorkspaceController* controller,
                         const tab_groups::TabGroupId& space);

// The context menu of the URL pill's Extensions button: "Extensions in This
// Space...", the same dialog for the active Space.
class KoanSpaceExtensionsContextMenu : public views::ContextMenuController,
                                       public ui::SimpleMenuModel::Delegate {
 public:
  explicit KoanSpaceExtensionsContextMenu(BrowserView* browser_view);
  KoanSpaceExtensionsContextMenu(const KoanSpaceExtensionsContextMenu&) =
      delete;
  KoanSpaceExtensionsContextMenu& operator=(
      const KoanSpaceExtensionsContextMenu&) = delete;
  ~KoanSpaceExtensionsContextMenu() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  KoanWorkspaceController* Workspaces() const;
  void ShowDialog();

  const raw_ptr<BrowserView> browser_view_;
  ui::SimpleMenuModel model_{this};
  std::unique_ptr<views::MenuRunner> runner_;
  base::WeakPtrFactory<KoanSpaceExtensionsContextMenu> weak_factory_{this};
};

// Ends the renderer processes of `container` (a user container of `profile`)
// and reloads its tabs, in every window, once they are gone. Which extensions
// a renderer loads is fixed when it starts, so this is what applies a changed
// list to open pages. Discarded tabs are left: they reload when selected.
void RestartProfileTabs(Profile* profile,
                        const containers::ContainerId& container);

// Per window: what the extension hooks can't see. Closes the side panel while
// it shows the panel of an extension that is off in the active tab's profile
// (when that tab becomes active, the panel opens, or the list changes), and
// has the toolbar ask again which extension buttons show when the list
// changes (koan::HidesExtensionAction(), koan_extensions_hooks.h).
class KoanSpaceExtensionsGuard : public TabStripModelObserver {
 public:
  explicit KoanSpaceExtensionsGuard(BrowserView* browser_view);
  KoanSpaceExtensionsGuard(const KoanSpaceExtensionsGuard&) = delete;
  KoanSpaceExtensionsGuard& operator=(const KoanSpaceExtensionsGuard&) = delete;
  ~KoanSpaceExtensionsGuard() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void OnListChanged();
  // Closes the side panel if it shows an extension that is off in the active
  // tab's profile. Posted: the side panel calls its observers mid-show.
  void ScheduleCheckSidePanel();
  void CheckSidePanel();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  // The side panel's shown callback, once the window has its side panel
  // (made after the browser view, so after this).
  base::CallbackListSubscription side_panel_shown_subscription_;
  base::WeakPtrFactory<KoanSpaceExtensionsGuard> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_EXTENSIONS_H_
