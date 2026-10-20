// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace koan {

class KoanSettingsUI;

// chrome://koan-settings: Koan's settings, laid out like Zen's preferences
// (Look and Feel, Tab Management, Keyboard Shortcuts). Registered from
// chrome/browser/ui/webui/chrome_web_ui_configs.cc.
class KoanSettingsUIConfig
    : public content::DefaultWebUIConfig<KoanSettingsUI> {
 public:
  KoanSettingsUIConfig();
};

class KoanSettingsUI : public content::WebUIController {
 public:
  explicit KoanSettingsUI(content::WebUI* web_ui);
  KoanSettingsUI(const KoanSettingsUI&) = delete;
  KoanSettingsUI& operator=(const KoanSettingsUI&) = delete;
  ~KoanSettingsUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_SETTINGS_UI_H_
