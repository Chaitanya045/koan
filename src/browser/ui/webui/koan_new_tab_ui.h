// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace koan {

class KoanNewTabUI;

// chrome://koan-newtab: Koan's new tab page. Like Zen's, an empty page in the
// theme's background colour, with no content, no script and no requests
// beyond the page itself. Reached through chrome://newtab (see
// koan_new_tab_page.h). Registered from
// chrome/browser/ui/webui/chrome_web_ui_configs.cc.
class KoanNewTabUIConfig : public content::DefaultWebUIConfig<KoanNewTabUI> {
 public:
  KoanNewTabUIConfig();
};

class KoanNewTabUI : public content::WebUIController {
 public:
  explicit KoanNewTabUI(content::WebUI* web_ui);
  KoanNewTabUI(const KoanNewTabUI&) = delete;
  KoanNewTabUI& operator=(const KoanNewTabUI&) = delete;
  ~KoanNewTabUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_
