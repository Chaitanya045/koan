// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpaceManager.mjs (the Space and Create menus)
//   zen-workspaces.ftl (strings)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_space_menu.h"

#include <array>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
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
    model_.AddItem(kEditTheme, u"Theme\u2026");
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
  // An old runner's close callback must not let go of the new hold.
  runner_.reset();
  hold_ = HoldSidebarOpen(anchor);
  runner_ = std::make_unique<views::MenuRunner>(
      &model_,
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&KoanSpaceMenu::OnMenuClosed,
                          base::Unretained(this)));
  runner_->RunMenuAt(
      anchor->GetWidget(), nullptr, gfx::Rect(screen_point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
  // No menu, or it ran to its end (macOS).
  if (!runner_->IsRunning()) {
    OnMenuClosed();
  }
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
      controller_->EditSpaceTheme(space_);
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
  if (command_id == kEditTheme) {
    return controller_->CanEditSpaceTheme();
  }
  if (command_id == kUnloadOthers) {
    return controller_->GetSpaces().size() > 1;
  }
  return true;
}

// KoanCreateMenu.

KoanCreateMenu::KoanCreateMenu(KoanWorkspaceController* controller)
    : entries_(controller->create_menu_entries()) {
  using Section = KoanWorkspaceController::CreateMenuEntry::Section;
  for (Section section : {Section::kCreate, Section::kTabs}) {
    bool first = true;
    for (size_t i = 0; i < entries_.size(); ++i) {
      const KoanWorkspaceController::CreateMenuEntry& entry = entries_[i];
      if (entry.section != section) {
        continue;
      }
      if (first && model_.GetItemCount() > 0) {
        model_.AddSeparator(ui::NORMAL_SEPARATOR);
      }
      first = false;
      const int command_id = static_cast<int>(i) + 1;
      if (entry.icon) {
        model_.AddItemWithIcon(command_id, entry.label, MenuIcon(*entry.icon));
      } else {
        model_.AddItem(command_id, entry.label);
      }
    }
  }
}

KoanCreateMenu::~KoanCreateMenu() = default;

void KoanCreateMenu::RunMenuAt(views::View* anchor) {
  runner_.reset();
  hold_ = HoldSidebarOpen(anchor);
  runner_ = std::make_unique<views::MenuRunner>(
      &model_, views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&KoanCreateMenu::OnMenuClosed,
                          base::Unretained(this)));
  RunBelow(runner_.get(), anchor);
  if (!runner_->IsRunning()) {
    OnMenuClosed();
  }
}

void KoanCreateMenu::ExecuteCommand(int command_id, int event_flags) {
  // Posted, with its own copy: the entry may close this menu (and its
  // anchor) or change the tab strip.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, entries_[command_id - 1].run);
}

bool KoanCreateMenu::IsCommandIdEnabled(int command_id) const {
  const KoanWorkspaceController::CreateMenuEntry& entry =
      entries_[command_id - 1];
  return entry.is_enabled.is_null() || entry.is_enabled.Run();
}

}  // namespace koan
