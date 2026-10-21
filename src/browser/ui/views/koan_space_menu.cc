// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_menu.h"

#include <array>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

enum SpaceMenuCommand {
  kChangeName = 1,
  kChangeIcon,
  kEditTheme,
  kUnload,
  kUnloadOthers,
  kCreateSpace,
  kDelete,
  // The Spaces to switch to (opened from the header).
  kFirstSwitch = 100,
};

enum CreateMenuCommand {
  kCreateSpaceItem = 1,
  kCreateFolder,
  kNewSplit,
  kNewTab,
};

// Edit Theme... choices: Default, then the colours.
struct ThemeChoice {
  std::optional<tab_groups::TabGroupColorId> color;
  const char16_t* name;
};
constexpr auto kThemes = std::to_array<ThemeChoice>({
    {std::nullopt, u"Default"},
    {tab_groups::TabGroupColorId::kBlue, u"Blue"},
    {tab_groups::TabGroupColorId::kCyan, u"Cyan"},
    {tab_groups::TabGroupColorId::kGreen, u"Green"},
    {tab_groups::TabGroupColorId::kYellow, u"Yellow"},
    {tab_groups::TabGroupColorId::kOrange, u"Orange"},
    {tab_groups::TabGroupColorId::kRed, u"Red"},
    {tab_groups::TabGroupColorId::kPink, u"Pink"},
    {tab_groups::TabGroupColorId::kPurple, u"Purple"},
});
constexpr int kThemeRadioGroup = 1;

constexpr int kMenuIconSize = 16;

ui::ImageModel MenuIcon(const gfx::VectorIcon& icon) {
  return ui::ImageModel::FromVectorIcon(icon, ui::kColorMenuIcon,
                                        kMenuIconSize);
}

// Below `anchor`, lined up with its leading edge, like Zen's popups.
void RunBelow(views::MenuRunner* runner, views::View* anchor) {
  gfx::Rect bounds = anchor->GetLocalBounds();
  views::View::ConvertRectToScreen(anchor, &bounds);
  runner->RunMenuAt(anchor->GetWidget(), nullptr, bounds,
                    views::MenuAnchorPosition::kTopLeft,
                    ui::mojom::MenuSourceType::kMouse);
}

}  // namespace

// KoanSpaceMenu.

KoanSpaceMenu::KoanSpaceMenu(KoanWorkspaceController* controller,
                             std::optional<tab_groups::TabGroupId> space)
    : controller_(controller),
      space_(space.value_or(controller->GetActiveSpace().value_or(
          tab_groups::TabGroupId::CreateEmpty()))),
      from_header_(!space.has_value()) {
  const bool active = controller_->GetActiveSpace() == space_;
  // Zen only renames and re-themes the active Space.
  if (active) {
    model_.AddItem(kChangeName, u"Change Name\u2026");
  }
  model_.AddItem(kChangeIcon, u"Change Icon\u2026");
  if (active) {
    model_.AddItem(kEditTheme, u"Edit Theme\u2026");
  }
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  if (from_header_) {
    int command = kFirstSwitch;
    for (const KoanWorkspaceController::SpaceInfo& info :
         controller_->GetSpaces()) {
      // "<icon>  <name>", the current Space disabled (as Zen on Linux).
      model_.AddItem(command,
                     info.icon.empty()
                         ? info.name
                         : base::StrCat({info.icon, u"  ", info.name}));
      switch_commands_.emplace(command++, info.id);
    }
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
  }
  model_.AddItem(kUnload, u"Unload Space");
  model_.AddItem(kUnloadOthers, u"Unload All Other Spaces");
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  model_.AddItem(kCreateSpace, u"Create Space");
  model_.AddItem(kDelete, u"Delete Space");
}

KoanSpaceMenu::~KoanSpaceMenu() = default;

void KoanSpaceMenu::RunMenuAt(views::View* anchor,
                              const gfx::Point& screen_point) {
  anchor_.SetView(anchor);
  runner_ = std::make_unique<views::MenuRunner>(
      &model_,
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(
      anchor->GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
}

void KoanSpaceMenu::ExecuteCommand(int command_id, int event_flags) {
  // After the menu has closed: some commands open more UI.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceMenu::Execute,
                                weak_factory_.GetWeakPtr(), command_id));
}

void KoanSpaceMenu::Execute(int command_id) {
  if (auto it = switch_commands_.find(command_id);
      it != switch_commands_.end()) {
    controller_->SwitchTo(it->second);
    return;
  }
  views::View* anchor = anchor_.view();
  switch (command_id) {
    case kChangeName:
      controller_->StartRename();
      return;
    case kChangeIcon:
      controller_->ShowIconPicker(space_, anchor);
      return;
    case kEditTheme:
      controller_->EditSpaceTheme(space_, anchor);
      return;
    case kUnload:
      controller_->UnloadSpace(space_);
      return;
    case kUnloadOthers:
      controller_->UnloadAllOtherSpaces(space_);
      return;
    case kCreateSpace:
      controller_->ShowCreateForm();
      return;
    case kDelete:
      controller_->DeleteSpace(space_);
      return;
  }
}

bool KoanSpaceMenu::IsCommandIdEnabled(int command_id) const {
  if (auto it = switch_commands_.find(command_id);
      it != switch_commands_.end()) {
    return it->second != controller_->GetActiveSpace();
  }
  if (command_id == kDelete) {
    return controller_->CanDeleteSpace();
  }
  if (command_id == kUnloadOthers) {
    return controller_->GetSpaces().size() > 1;
  }
  return true;
}

// KoanCreateMenu.

KoanCreateMenu::KoanCreateMenu(KoanWorkspaceController* controller,
                               BrowserView* browser_view)
    : controller_(controller), browser_view_(browser_view) {
  model_.AddItemWithIcon(kCreateSpaceItem, u"Create Space",
                         MenuIcon(vector_icons::kContentCopyIcon));
  model_.AddItemWithIcon(kCreateFolder, u"Create Folder",
                         MenuIcon(vector_icons::kFolderChromeRefreshOldIcon));
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  model_.AddItemWithIcon(kNewSplit, u"New Split", MenuIcon(kSplitSceneIcon));
  model_.AddItemWithIcon(kNewTab, u"New Tab", MenuIcon(kAddIcon));
}

KoanCreateMenu::~KoanCreateMenu() = default;

void KoanCreateMenu::RunMenuAt(views::View* anchor) {
  runner_ = std::make_unique<views::MenuRunner>(
      &model_, views::MenuRunner::HAS_MNEMONICS);
  RunBelow(runner_.get(), anchor);
}

void KoanCreateMenu::ExecuteCommand(int command_id, int event_flags) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanCreateMenu::Execute,
                                weak_factory_.GetWeakPtr(), command_id));
}

void KoanCreateMenu::Execute(int command_id) {
  switch (command_id) {
    case kCreateSpaceItem:
      controller_->ShowCreateForm();
      return;
    case kNewSplit:
      chrome::ExecuteCommand(browser_view_->browser(), IDC_NEW_SPLIT_TAB);
      return;
    case kNewTab:
      chrome::ExecuteCommand(browser_view_->browser(), IDC_NEW_TAB);
      return;
  }
}

bool KoanCreateMenu::IsCommandIdEnabled(int command_id) const {
  // Zen's folders (tab folders in the sidebar) aren't in Koan yet.
  if (command_id == kCreateFolder) {
    return false;
  }
  if (command_id == kNewSplit) {
    return chrome::IsCommandEnabled(browser_view_->browser(),
                                    IDC_NEW_SPLIT_TAB);
  }
  return true;
}

// KoanThemeMenu.

KoanThemeMenu::KoanThemeMenu(const KoanWorkspaceController::SpaceTheme& current,
                             PickedCallback on_pick)
    : current_(current), on_pick_(std::move(on_pick)) {
  for (size_t i = 0; i < kThemes.size(); ++i) {
    model_.AddRadioItem(static_cast<int>(i), kThemes[i].name, kThemeRadioGroup);
  }
}

KoanThemeMenu::~KoanThemeMenu() = default;

void KoanThemeMenu::RunMenuAt(views::View* anchor) {
  runner_ = std::make_unique<views::MenuRunner>(
      &model_, views::MenuRunner::HAS_MNEMONICS);
  RunBelow(runner_.get(), anchor);
}

KoanWorkspaceController::SpaceTheme KoanThemeMenu::ThemeFor(
    int command_id) const {
  return {.color = kThemes[command_id].color};
}

void KoanThemeMenu::ExecuteCommand(int command_id, int event_flags) {
  on_pick_.Run(ThemeFor(command_id));
}

bool KoanThemeMenu::IsCommandIdChecked(int command_id) const {
  return ThemeFor(command_id) == current_;
}

}  // namespace koan
