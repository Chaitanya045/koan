// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace koan {

class KoanNewTabUI;

// chrome://koan-newtab: Koan's new tab page. Like Zen's, an empty page in the
// colour of the window's look (shadcn's `--background`, or the theme's new tab
// page colour in the Zen look), with no content, no script and no requests
// beyond the page itself. It loads again when the window's colours change.
// Reached through chrome://newtab (see koan_new_tab_page.h). Registered from
// chrome/browser/ui/webui/chrome_web_ui_configs.cc.
class KoanNewTabUIConfig : public content::DefaultWebUIConfig<KoanNewTabUI> {
 public:
  KoanNewTabUIConfig();
};

class KoanNewTabUI : public content::WebUIController,
                     public content::WebContentsObserver {
 public:
  explicit KoanNewTabUI(content::WebUI* web_ui);
  KoanNewTabUI(const KoanNewTabUI&) = delete;
  KoanNewTabUI& operator=(const KoanNewTabUI&) = delete;
  ~KoanNewTabUI() override;

  // content::WebContentsObserver:
  void OnColorProviderChanged() override;
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStopLoading() override;

  WEB_UI_CONTROLLER_TYPE_DECL();

 private:
  // Loads the page again when its colour is no longer the window's, once
  // nothing is loading (see the .cc).
  void ScheduleCheck();
  void ReloadIfStale();

  bool check_scheduled_ = false;
  base::TimeTicks last_reload_;
  base::WeakPtrFactory<KoanNewTabUI> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_NEW_TAB_UI_H_
