// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace koan {

class KoanWelcomeUI;

// chrome://koan-welcome: Zen's welcome flow, shown over the window on first
// run (KoanWelcome). Registered from
// chrome/browser/ui/webui/chrome_web_ui_configs.cc.
class KoanWelcomeUIConfig : public content::DefaultWebUIConfig<KoanWelcomeUI> {
 public:
  KoanWelcomeUIConfig();
};

class KoanWelcomeUI : public content::WebUIController {
 public:
  explicit KoanWelcomeUI(content::WebUI* web_ui);
  KoanWelcomeUI(const KoanWelcomeUI&) = delete;
  KoanWelcomeUI& operator=(const KoanWelcomeUI&) = delete;
  ~KoanWelcomeUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_UI_H_
