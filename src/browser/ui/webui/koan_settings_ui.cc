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
#include "base/strings/strcat.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/fonts/koan_fonts.h"
#include "koan/browser/ui/webui/koan_settings_handler.h"
#include "koan/browser/ui/webui/koan_settings_resources.h"
#include "koan/common/koan_url_constants.h"

namespace koan {

namespace {

// The page is three files, built into the binary by embed_resources.py (no
// grit or .pak changes upstream). Any other path is the page itself, so
// chrome://koan-settings/shortcuts opens on that pane.
//
// The look's stylesheet draws the page from the same tokens as the native UI
// (koan::look::TokensToCss), so the two never differ. In the Zen style it is
// empty and the page keeps its own Zen values. It is rebuilt on every
// request; the page asks again when a look pref changes.
std::string LookStylesheet() {
  const look::Look& current = look::CurrentLook();
  if (current.style != look::Style::kShadcn) {
    return std::string();
  }
  const auto block = [](bool dark) {
    return base::StrCat(
        {":root {\n  color-scheme: ", dark ? "dark" : "light", ";\n",
         look::TokensToCss(look::CurrentTokens(dark)),
         // Hover surface and the field border the page derives from tokens.
         "--secondary-hover: color-mix(in srgb, var(--secondary), "
         "var(--foreground) 8%);\n"
         "--field-border: var(--input);\n"
         "--r-item: calc(var(--radius) - 2px);\n"
         "--r-field: calc(var(--radius) - 2px);\n"
         "--r-button: calc(var(--radius) - 2px);\n"
         "--r-card: calc(var(--radius) + 4px);\n}\n"});
  };
  std::string css = base::StrCat(
      {"@font-face { font-family: 'Geist'; src: url(/geist.woff2) "
       "format('woff2'); font-weight: 100 900; font-display: swap; }\n"
       "@font-face { font-family: 'Geist Mono'; src: url(/geist-mono.woff2) "
       "format('woff2'); font-display: swap; }\n"});
  switch (current.mode) {
    case look::Mode::kDark:
      css += block(true);
      break;
    case look::Mode::kLight:
      css += block(false);
      break;
    case look::Mode::kAuto:
      css += block(false);
      css += "@media (prefers-color-scheme: dark) {\n" + block(true) + "}\n";
      break;
  }
  css +=
      "body { font-family: 'Geist', Inter, system-ui, sans-serif; "
      "font-size: 14px; }\n"
      "kbd, code, .shortcut-input { font-family: 'Geist Mono', ui-monospace, "
      "monospace; }\n";
  return css;
}

void ServeResource(const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  std::string data;
  if (path == "koan_settings.css") {
    data = kKoanSettingsCss;
  } else if (path == "koan_settings.js") {
    data = kKoanSettingsJs;
  } else if (path == "koan_tokens.css") {
    data = LookStylesheet();
  } else if (path == "geist.woff2") {
    data = std::string(fonts::SansWoff2());
  } else if (path == "geist-mono.woff2") {
    data = std::string(fonts::MonoWoff2());
  } else {
    data = kKoanSettingsHtml;
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(data)));
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
