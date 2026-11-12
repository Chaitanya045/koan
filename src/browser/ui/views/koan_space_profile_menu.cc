// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpaceManager.mjs, ZenSpaceCreation.mjs (the profile menus)
//   zen-workspaces.ftl (strings)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_space_profile_menu.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "koan/browser/koan_prefs.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/image_model.h"

namespace koan {

namespace {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileNameField);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileNameField);

enum ProfileMenuCommand {
  kDefaultProfile = 1,
  kNewProfile,
  // The user's containers.
  kFirstProfile = 100,
};

// The colour dot before a profile's name (as Chromium's tab group menus).
constexpr int kProfileDotSize = 12;

// Zen's zen-workspace-default-profile, and a container the list lost.
constexpr char16_t kDefaultProfileName[] = u"Default";
constexpr char16_t kUnknownProfileName[] = u"Profile";

// The user's containers are the original profile's (Spaces are off in
// Incognito windows, but a pref read never goes to an off-the-record one).
PrefService* PrefsOf(Profile* profile) {
  return profile->GetOriginalProfile()->GetPrefs();
}

}  // namespace

std::vector<containers::UserContainer> GetUserContainers(Profile* profile) {
  return containers::ParseUserContainers(
      PrefsOf(profile)->GetList(prefs::kUserContainers));
}

std::u16string SpaceProfileName(Profile* profile,
                                const std::string& container) {
  const std::optional<containers::ContainerId> id =
      containers::SpaceContainerFromString(container);
  if (!id) {
    return kDefaultProfileName;
  }
  for (const containers::UserContainer& listed : GetUserContainers(profile)) {
    if (listed.id == *id) {
      return base::UTF8ToUTF16(listed.name);
    }
  }
  return kUnknownProfileName;
}

ui::ColorId ContainerLineColorId(Profile* profile,
                                 const containers::ContainerId& container,
                                 bool active_frame) {
  tab_groups::TabGroupColorId color = tab_groups::TabGroupColorId::kBlue;
  for (const containers::UserContainer& listed : GetUserContainers(profile)) {
    if (listed.id == container) {
      color = listed.color;
    }
  }
  return GetTabGroupTabStripColorId(color, active_frame);
}

KoanSpaceProfileMenu::KoanSpaceProfileMenu(BrowserWindowInterface* browser,
                                           std::string current,
                                           PickCallback on_pick)
    : browser_(browser),
      current_(std::move(current)),
      on_pick_(std::move(on_pick)) {
  model_.AddCheckItem(kDefaultProfile, kDefaultProfileName);
  const std::vector<containers::UserContainer> listed =
      GetUserContainers(browser_->GetProfile());
  if (!listed.empty()) {
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
  }
  for (const containers::UserContainer& container : listed) {
    const int command_id = kFirstProfile + static_cast<int>(listed_.size());
    model_.AddCheckItem(command_id, base::UTF8ToUTF16(container.name));
    model_.SetIconForCommandId(
        command_id,
        ui::ImageModel::FromVectorIcon(
            kCircleFilledIcon, GetTabGroupContextMenuColorId(container.color),
            kProfileDotSize));
    listed_.push_back(containers::Serialize(container.id));
  }
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  model_.AddItem(kNewProfile, u"New Profile…");
}

KoanSpaceProfileMenu::~KoanSpaceProfileMenu() = default;

void KoanSpaceProfileMenu::ExecuteCommand(int command_id, int event_flags) {
  // After the menu has closed: New Profile... opens a dialog.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceProfileMenu::Execute,
                                weak_factory_.GetWeakPtr(), command_id));
}

void KoanSpaceProfileMenu::Execute(int command_id) {
  if (command_id == kDefaultProfile) {
    on_pick_.Run(std::string());
    return;
  }
  if (command_id == kNewProfile) {
    ShowNewProfilePrompt(
        browser_, base::OnceCallback<void(const std::string&)>(on_pick_));
    return;
  }
  const size_t index = static_cast<size_t>(command_id - kFirstProfile);
  if (command_id >= kFirstProfile && index < listed_.size()) {
    on_pick_.Run(listed_[index]);
  }
}

bool KoanSpaceProfileMenu::IsCommandIdChecked(int command_id) const {
  if (command_id == kDefaultProfile) {
    return !containers::SpaceContainerFromString(current_);
  }
  const size_t index = static_cast<size_t>(command_id - kFirstProfile);
  return command_id >= kFirstProfile && index < listed_.size() &&
         listed_[index] == current_;
}

void ShowNewProfilePrompt(BrowserWindowInterface* browser,
                          base::OnceCallback<void(const std::string&)> done) {
  auto builder = std::make_unique<ui::DialogModel::Builder>();
  ui::DialogModel* model = builder->model();
  builder->SetTitle(u"New Profile")
      .AddParagraph(ui::DialogModelLabel(
          u"Profiles are used to separate cookies and site data between "
          u"spaces."))
      .AddTextfield(
          kKoanNewProfileNameField, std::u16string(), std::u16string(),
          ui::DialogModelTextfield::Params().SetAccessibleName(u"Name"))
      .SetInitiallyFocusedField(kKoanNewProfileNameField)
      .AddOkButton(
          base::BindOnce(
              [](base::WeakPtr<Profile> profile,
                 base::OnceCallback<void(const std::string&)> done,
                 ui::DialogModel* model) {
                if (!profile) {
                  return;
                }
                std::optional<containers::UserContainer> container;
                {
                  ScopedListPrefUpdate update(PrefsOf(profile.get()),
                                              prefs::kUserContainers);
                  container = containers::AddUserContainer(
                      update.Get(),
                      model->GetTextfieldByUniqueId(kKoanNewProfileNameField)
                          ->text());
                }
                if (container) {
                  std::move(done).Run(containers::Serialize(container->id));
                }
              },
              browser->GetProfile()->GetOriginalProfile()->GetWeakPtr(),
              std::move(done), base::Unretained(model)),
          ui::DialogModel::Button::Params().SetLabel(u"Create"))
      .AddCancelButton(base::DoNothing());
  chrome::ShowBrowserModal(browser, builder->Build());
}

}  // namespace koan
