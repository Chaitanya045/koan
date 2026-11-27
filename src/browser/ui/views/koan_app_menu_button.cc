// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_app_menu_button.h"

#include <memory>
#include <optional>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/view_class_properties.h"

namespace koan {

KoanAppMenuButton::KoanAppMenuButton(BrowserView* browser_view)
    : AppMenuButton(base::BindRepeating(&KoanAppMenuButton::ButtonPressed,
                                        base::Unretained(this))),
      browser_view_(browser_view) {
  // AppMenuButton names itself the toolbar's ⋮ (kToolbarAppMenuButtonElementId)
  // for tutorials and promos, which anchor to that one: this is another
  // button.
  ClearProperty(views::kElementIdentifierKey);
  SetVectorIcons(kKoanMenuIcon, kKoanMenuIcon);
  // Zen's tooltip for #PanelUI-menu-button.
  SetTooltipText(u"Open application menu");
  GetViewAccessibility().SetName(u"Open application menu");
}

KoanAppMenuButton::~KoanAppMenuButton() = default;

void KoanAppMenuButton::ButtonPressed(const ui::Event& event) {
  // As BrowserAppMenuButton::ButtonPressed(): from the keyboard the menu
  // shows its mnemonics and selects its first item.
  ShowMenuWithFlags(event.IsKeyEvent()
                        ? (views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                           views::MenuRunner::INVOKED_FROM_KEYBOARD)
                        : views::MenuRunner::NO_FLAGS);
}

void KoanAppMenuButton::ShowMenuWithFlags(int run_types) {
  if (IsMenuShowing()) {
    return;
  }
  // As the toolbar's button (BrowserAppMenuButton::ShowMenuWithFlags()).
  BrowserWindowInterface* browser = browser_view_->browser();
  if (base::FeatureList::IsEnabled(features::kAppMenuGlowUp)) {
    RunActionMenu(browser, run_types);
    return;
  }
  ToolbarView* toolbar = browser_view_->toolbar();
  RunMenu(std::make_unique<AppMenuModel>(
              this, browser,
              toolbar ? toolbar->app_menu_icon_controller() : nullptr),
          browser, run_types);
}

bool KoanAppMenuButton::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  const KoanShortcutController* shortcuts =
      controller ? controller->shortcuts() : nullptr;
  if (!shortcuts) {
    return false;
  }
  // A command a Koan shortcut runs (or does Koan's way, e.g. the Library's
  // History) shows that shortcut's key in effect, which Zen may have moved
  // (Save Page to Ctrl+Alt+Shift+S, private windows to Ctrl+Shift+P) and the
  // user may have changed or cleared. Chrome has none for Exit on Linux.
  if (const std::optional<shortcuts::Action> action =
          ActionForCommand(command_id)) {
    const std::optional<ui::Accelerator> key =
        shortcuts->GetAccelerator(*action);
    if (key) {
      *accelerator = *key;
    }
    return key.has_value();
  }
  // Any other command: Chrome's key, unless a Koan shortcut runs on it
  // instead (Ctrl+S is compact mode, not Save Page; Ctrl+Shift+D pins the
  // tab, not Bookmark All Tabs).
  const ToolbarView* toolbar = browser_view_->toolbar();
  ui::Accelerator chrome_key;
  if (!toolbar ||
      !toolbar->GetAcceleratorForCommandId(command_id, &chrome_key) ||
      shortcuts->IsBound(chrome_key)) {
    return false;
  }
  *accelerator = chrome_key;
  return true;
}

// static
std::optional<shortcuts::Action> KoanAppMenuButton::ActionForCommand(
    int command_id) {
  using shortcuts::Action;
  // The commands of KoanShortcutController::Run() that Chromium's app menu
  // has (AppMenuModel asks for reading mode's key as
  // IDC_SHOW_READING_MODE_KEYBOARD).
  switch (command_id) {
    case IDC_EXIT:
      return Action::kQuit;
    case IDC_NEW_TAB:
      return Action::kNewTab;
    case IDC_NEW_WINDOW:
      return Action::kNewWindow;
    case IDC_NEW_INCOGNITO_WINDOW:
      return Action::kNewPrivateWindow;
    case IDC_NEW_SPLIT_TAB:
      return Action::kNewEmptySplit;
    case IDC_SAVE_PAGE:
      return Action::kSavePage;
    case IDC_SHOW_HISTORY:
      return Action::kLibraryHistory;
    case IDC_SHOW_DOWNLOADS:
      return Action::kDownloads;
    case IDC_SHOW_BOOKMARK_SIDE_PANEL:
      return Action::kBookmarks;
    case IDC_SHOW_READING_MODE:
    case IDC_SHOW_READING_MODE_KEYBOARD:
      return Action::kReaderMode;
    case IDC_DEV_TOOLS_INSPECT:
      return Action::kInspect;
    case IDC_WINDOW_MUTE_SITE:
      return Action::kMuteSite;
    default:
      return std::nullopt;
  }
}

bool KoanAppMenuButton::HandleAccessibleAction(
    const ui::AXActionData& action_data) {
  // As BrowserAppMenuButton: screen readers open and close the menu with
  // expand and collapse.
  if (action_data.action == ax::mojom::Action::kExpand) {
    ShowMenuWithFlags(views::MenuRunner::NO_FLAGS);
    return true;
  }
  if (action_data.action == ax::mojom::Action::kCollapse) {
    if (IsMenuShowing()) {
      CloseMenu();
    }
    return true;
  }
  return AppMenuButton::HandleAccessibleAction(action_data);
}

BEGIN_METADATA(KoanAppMenuButton)
END_METADATA

}  // namespace koan
