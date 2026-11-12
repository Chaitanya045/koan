// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILE_MENU_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILE_MENU_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "ui/color/color_id.h"
#include "ui/menus/simple_menu_model.h"

class BrowserWindowInterface;
class Profile;

namespace koan {

// Zen's containers per Space (#318), as its UI calls them: profiles. Zen
// fills these from Firefox's container menu, with "No Container" relabelled
// Default; Firefox makes containers in its settings, Koan here, with New
// Profile....

// The user's containers (prefs::kUserContainers), in the order they were made.
std::vector<containers::UserContainer> GetUserContainers(Profile* profile);

// The name a Space's container (SpaceData::container) goes by: Zen's
// "Default" for none, else the container's name, or "Profile" for one the
// list no longer has.
std::u16string SpaceProfileName(Profile* profile, const std::string& container);

// The colour id of a container's line on a tab (Zen's .tab-context-line) or
// its dot in a menu.
ui::ColorId ContainerLineColorId(Profile* profile,
                                 const containers::ContainerId& container,
                                 bool active_frame);

// The profile menu: the Space menu's Set Profile submenu and the Create-a-
// Space form's Profile button. Default, the user's containers (each with its
// colour), New Profile... (which asks for a name, makes the container and
// picks it). `current` (a Space's container, empty for none) is checked.
// `on_pick` gets the picked container, after the menu closed.
class KoanSpaceProfileMenu : public ui::SimpleMenuModel::Delegate {
 public:
  using PickCallback = base::RepeatingCallback<void(const std::string&)>;

  KoanSpaceProfileMenu(BrowserWindowInterface* browser,
                       std::string current,
                       PickCallback on_pick);
  KoanSpaceProfileMenu(const KoanSpaceProfileMenu&) = delete;
  KoanSpaceProfileMenu& operator=(const KoanSpaceProfileMenu&) = delete;
  ~KoanSpaceProfileMenu() override;

  ui::SimpleMenuModel* model() { return &model_; }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  void Execute(int command_id);

  const raw_ptr<BrowserWindowInterface> browser_;
  const std::string current_;
  const PickCallback on_pick_;
  // The containers listed, by command id - kFirstProfile.
  std::vector<std::string> listed_;
  ui::SimpleMenuModel model_{this};
  base::WeakPtrFactory<KoanSpaceProfileMenu> weak_factory_{this};
};

// New Profile...: asks for the new profile's name in a dialog of `browser`'s
// window, makes the container and runs `done` with it (Serialize()d). Never
// runs it if the dialog is cancelled or the name is refused.
void ShowNewProfilePrompt(BrowserWindowInterface* browser,
                          base::OnceCallback<void(const std::string&)> done);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_PROFILE_MENU_H_
