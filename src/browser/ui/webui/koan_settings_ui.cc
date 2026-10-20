// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/ui/webui/koan_settings_handler.h"
#include "koan/browser/ui/webui/koan_settings_resources.h"
#include "koan/common/koan_url_constants.h"

namespace koan {

namespace {

// The page is three files, built into the binary by embed_resources.py (no
// grit or .pak changes upstream). Any other path is the page itself, so
// chrome://koan-settings/shortcuts opens on that pane.
void ServeResource(const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  std::string_view data = kKoanSettingsHtml;
  if (path == "koan_settings.css") {
    data = kKoanSettingsCss;
  } else if (path == "koan_settings.js") {
    data = kKoanSettingsJs;
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::string(data)));
}

}  // namespace

KoanSettingsUIConfig::KoanSettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kSettingsHost) {}

KoanSettingsUI::KoanSettingsUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kSettingsHost);
  source->SetRequestFilter(
      base::BindRepeating([](const std::string&) { return true; }),
      base::BindRepeating(&ServeResource));
  // Koan's settings are the regular profile's, also from an incognito window.
  web_ui->AddMessageHandler(std::make_unique<KoanSettingsHandler>(
      *profile->GetOriginalProfile()->GetPrefs(),
      *g_browser_process->local_state()));
}

KoanSettingsUI::~KoanSettingsUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(KoanSettingsUI)

}  // namespace koan
