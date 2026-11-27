// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/frame/app_menu_button.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"

class BrowserView;

namespace ui {
class Event;
}  // namespace ui

namespace koan {

// Zen's app menu button (#PanelUI-button, the "menu" icon) in the sidebar's
// top row, with the single toolbar on Linux: Chromium's app menu (New Tab,
// History, Settings ... Exit) where the hidden toolbar's ⋮ can't be reached.
// Zen shows its button there on every platform but macOS, whose menu bar
// has the same items. The menu's Exit shows Koan's Quit key (Ctrl+Q).
class KoanAppMenuButton : public AppMenuButton, public ui::AcceleratorProvider {
  METADATA_HEADER(KoanAppMenuButton, AppMenuButton)

 public:
  explicit KoanAppMenuButton(BrowserView* browser_view);
  KoanAppMenuButton(const KoanAppMenuButton&) = delete;
  KoanAppMenuButton& operator=(const KoanAppMenuButton&) = delete;
  ~KoanAppMenuButton() override;

  // AppMenuButton:
  void ShowMenuWithFlags(int run_types) override;

  // ui::AcceleratorProvider (the keys the menu shows):
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;

 private:
  void ButtonPressed(const ui::Event& event);

  raw_ptr<BrowserView> browser_view_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_APP_MENU_BUTTON_H_
