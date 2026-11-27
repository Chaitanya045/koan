// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/frame/app_menu_button.h"
#include "koan/browser/koan_shortcut_table.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserView;

namespace ui {
class Event;
struct AXActionData;
}  // namespace ui

namespace koan {

// Zen's app menu button (#PanelUI-button, the "menu" icon) in the sidebar's
// top row, with the single toolbar on Linux: Chromium's app menu (New Tab,
// History, Settings ... Exit) where the hidden toolbar's ⋮ can't be reached.
// Zen shows its button there on every platform but macOS, whose menu bar
// has the same items. The menu shows Koan's keys (Exit: Quit's Ctrl+Q).
class KoanAppMenuButton : public AppMenuButton, public ui::AcceleratorProvider {
  METADATA_HEADER(KoanAppMenuButton, AppMenuButton)

 public:
  explicit KoanAppMenuButton(BrowserView* browser_view);
  KoanAppMenuButton(const KoanAppMenuButton&) = delete;
  KoanAppMenuButton& operator=(const KoanAppMenuButton&) = delete;
  ~KoanAppMenuButton() override;

  // AppMenuButton:
  void ShowMenuWithFlags(int run_types) override;

  // views::View:
  bool HandleAccessibleAction(const ui::AXActionData& action_data) override;

  // ui::AcceleratorProvider (the keys the menu shows): Koan's keys in
  // effect. A command a Koan shortcut runs shows that shortcut's key; any
  // other shows Chrome's, unless Koan runs something else on that key.
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

  // The Koan shortcut that runs `command_id` (an IDC_* in the app menu), if
  // any.
  static std::optional<shortcuts::Action> ActionForCommand(int command_id);

 private:
  void ButtonPressed(const ui::Event& event);

  raw_ptr<BrowserView> browser_view_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_
