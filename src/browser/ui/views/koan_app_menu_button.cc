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
  ShowMenuWithFlags(event.IsKeyEvent()
                        ? views::MenuRunner::SHOULD_SHOW_MNEMONICS
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
  if (command_id == IDC_EXIT) {
    // Chrome has no key for Exit on Linux; Koan's Quit has one, which the
    // user may have changed or cleared.
    const KoanBrowserViewController* controller =
        KoanBrowserViewController::From(browser_view_);
    const std::optional<ui::Accelerator> key =
        controller && controller->shortcuts()
            ? controller->shortcuts()->GetAccelerator(shortcuts::Action::kQuit)
            : std::nullopt;
    if (key) {
      *accelerator = *key;
    }
    return key.has_value();
  }
  const ToolbarView* toolbar = browser_view_->toolbar();
  return toolbar &&
         toolbar->GetAcceleratorForCommandId(command_id, accelerator);
}

BEGIN_METADATA(KoanAppMenuButton)
END_METADATA

}  // namespace koan
