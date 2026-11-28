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

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
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

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileNameField);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileCreateButton);
// Why Create didn't make the profile: one at a time, under the name.
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileTooLongMessage);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileControlMessage);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanNewProfileFailedMessage);

// Apart from the Space menu's (koan_space_menu.cc), which holds this one as
// Set Profile: views' menus run a command by its id, looked up in the whole
// menu (MenuModelAdapter::ExecuteCommand()), so an id of the Space menu's
// would run the Space menu's command.
enum ProfileMenuCommand {
  kDefaultProfile = 10000,
  kNewProfile,
  // The user's containers.
  kFirstProfile = 10100,
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

// Shows the New Profile dialog's message `shown` and hides the others (none
// for a null id).
void ShowNewProfileMessage(ui::DialogModel* model,
                           ui::ElementIdentifier shown) {
  for (ui::ElementIdentifier id :
       {kKoanNewProfileTooLongMessage, kKoanNewProfileControlMessage,
        kKoanNewProfileFailedMessage}) {
    // (Only a change: each one lays the dialog out again.)
    if (model->GetFieldByUniqueId(id)->is_visible() != (id == shown)) {
      model->SetVisible(id, id == shown);
    }
  }
}

// The name typed: Create is on once it isn't empty or only spaces, and a
// message about an earlier name goes.
void OnNewProfileNameChanged(ui::DialogModel* model) {
  const std::u16string& name =
      model->GetTextfieldByUniqueId(kKoanNewProfileNameField)->text();
  model->SetButtonEnabled(
      model->GetButtonByUniqueId(kKoanNewProfileCreateButton),
      containers::CheckUserContainerName(name) !=
          containers::UserContainerNameError::kEmpty);
  ShowNewProfileMessage(model, ui::ElementIdentifier());
}

// Create: makes the profile and runs `done` with it, and closes the dialog
// (true). A name the profile can't take, or a failure, leaves the dialog open
// with a message (false), never closed with no profile (#576).
bool CreateNewProfile(base::WeakPtr<Profile> profile,
                      base::OnceCallback<void(const std::string&)>& done,
                      ui::DialogModel* model) {
  if (!profile) {
    // The window is going, and the dialog with it.
    return true;
  }
  const std::u16string name =
      model->GetTextfieldByUniqueId(kKoanNewProfileNameField)->text();
  switch (containers::CheckUserContainerName(name)) {
    case containers::UserContainerNameError::kEmpty:
      // (Enter, or a click as Create turned off: Create has nothing to make.)
      return false;
    case containers::UserContainerNameError::kTooLong:
      ShowNewProfileMessage(model, kKoanNewProfileTooLongMessage);
      return false;
    case containers::UserContainerNameError::kControlCharacter:
      ShowNewProfileMessage(model, kKoanNewProfileControlMessage);
      return false;
    case containers::UserContainerNameError::kNone:
      break;
  }
  std::optional<containers::UserContainer> container;
  {
    ScopedListPrefUpdate update(PrefsOf(profile.get()), prefs::kUserContainers);
    container = containers::AddUserContainer(update.Get(), name);
  }
  if (!container || !done) {
    ShowNewProfileMessage(model, kKoanNewProfileFailedMessage);
    return false;
  }
  std::move(done).Run(containers::Serialize(container->id));
  return true;
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
  // Create is off until there is a name (as Chromium's own dialogs with a
  // field that must be filled, e.g. the sync passphrase dialog's OK).
  ui::DialogModel::Button::Params create_params;
  create_params.SetId(kKoanNewProfileCreateButton)
      .SetLabel(u"Create")
      .SetEnabled(false);
  builder->SetTitle(u"New Profile")
      .AddParagraph(ui::DialogModelLabel(
          u"Profiles are used to separate cookies and site data between "
          u"spaces."))
      .AddTextfield(
          kKoanNewProfileNameField, std::u16string(), std::u16string(),
          ui::DialogModelTextfield::Params().SetAccessibleName(u"Name"))
      .AddParagraph(
          ui::DialogModelLabel(base::StrCat(
              {u"This name is too long. Use at most ",
               base::NumberToString16(containers::kMaxUserContainerNameLength),
               u" characters."})),
          std::u16string(), kKoanNewProfileTooLongMessage)
      .AddParagraph(ui::DialogModelLabel(u"A name can't have a tab, a line "
                                         u"break or another control "
                                         u"character."),
                    std::u16string(), kKoanNewProfileControlMessage)
      .AddParagraph(
          ui::DialogModelLabel(u"Koan couldn't make the profile. Try again."),
          std::u16string(), kKoanNewProfileFailedMessage)
      .SetInitiallyFocusedField(kKoanNewProfileNameField)
      .AddOkButton(
          base::BindRepeating(
              &CreateNewProfile,
              browser->GetProfile()->GetOriginalProfile()->GetWeakPtr(),
              base::OwnedRef(std::move(done)), base::Unretained(model)),
          create_params)
      .AddCancelButton(base::DoNothing());
  ShowNewProfileMessage(model, ui::ElementIdentifier());
  // The dialog's model outlives its fields' callbacks: the subscription goes
  // when the dialog does.
  base::CallbackListSubscription subscription =
      model->GetTextfieldByUniqueId(kKoanNewProfileNameField)
          ->AddOnFieldChangedCallback(base::BindRepeating(
              &OnNewProfileNameChanged, base::Unretained(model)));
  builder->SetDialogDestroyingCallback(
      base::DoNothingWithBoundArgs(std::move(subscription)));
  chrome::ShowBrowserModal(browser, builder->Build());
}

}  // namespace koan
