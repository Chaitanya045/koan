// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/color/koan_tokens.h"
#include "koan/browser/ui/fonts/koan_fonts.h"
#include "koan/browser/ui/webui/koan_welcome_handler.h"
#include "koan/browser/ui/webui/koan_welcome_resources.h"
#include "koan/common/koan_url_constants.h"

namespace koan {

namespace {

// The welcome is always in shadcn's look (#181), whichever style the browser
// uses: the user's look with the shadcn style, light, dark or following the
// system as the look's mode says.
std::string TokensStylesheet() {
  look::Look look = look::CurrentLook();
  look.style = look::Style::kShadcn;
  const auto block = [&look](bool dark) {
    return base::StrCat(
        {":root {\n  color-scheme: ", dark ? "dark" : "light", ";\n",
         look::TokensToCss(look::ResolveTokens(look, dark)), "}\n"});
  };
  std::string css = base::StrCat(
      {"@font-face { font-family: 'Geist'; src: url(/geist.woff2) "
       "format('woff2'); font-weight: 100 900; font-display: swap; }\n"
       "@font-face { font-family: 'Geist Mono'; src: url(/geist-mono.woff2) "
       "format('woff2'); font-display: swap; }\n"});
  switch (look.mode) {
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
  return css;
}

// The Lucide icons (welcome_icons/, ISC), by name.
std::string_view Icon(std::string_view name) {
  static constexpr std::pair<std::string_view, std::string_view> kIcons[] = {
      {"arrow-left", kKoanWelcomeIconArrowLeft},
      {"arrow-right", kKoanWelcomeIconArrowRight},
      {"check", kKoanWelcomeIconCheck},
      {"circle-check-big", kKoanWelcomeIconCircleCheckBig},
      {"hard-drive-download", kKoanWelcomeIconHardDriveDownload},
      {"keyboard", kKoanWelcomeIconKeyboard},
      {"search", kKoanWelcomeIconSearch},
      {"sparkles", kKoanWelcomeIconSparkles},
      {"x", kKoanWelcomeIconX},
  };
  for (const auto& [key, svg] : kIcons) {
    if (key == name) {
      return svg;
    }
  }
  return std::string_view();
}

void ServeResource(const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  std::string data;
  constexpr std::string_view kIconsDir = "icons/";
  constexpr std::string_view kSvg = ".svg";
  if (path == "koan_welcome.css") {
    data = kKoanWelcomeCss;
  } else if (path == "koan_welcome.js") {
    data = kKoanWelcomeJs;
  } else if (path == "koan_tokens.css") {
    data = TokensStylesheet();
  } else if (path == "geist.woff2") {
    data = std::string(fonts::SansWoff2());
  } else if (path == "geist-mono.woff2") {
    data = std::string(fonts::MonoWoff2());
  } else if (path.starts_with(kIconsDir) && path.ends_with(kSvg)) {
    data = std::string(Icon(std::string_view(path).substr(
        kIconsDir.size(), path.size() - kIconsDir.size() - kSvg.size())));
  } else if (!path.ends_with(".js") && !path.ends_with(".css") &&
             !path.ends_with(".svg") && !path.ends_with(".woff2")) {
    // The page itself, at any other path; an unknown file is empty, not the
    // page served as a script or an image.
    data = kKoanWelcomeHtml;
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(data)));
}

}  // namespace

KoanWelcomeUIConfig::KoanWelcomeUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kWelcomeHost) {}

KoanWelcomeUI::KoanWelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kWelcomeHost);
  source->SetRequestFilter(
      base::BindRepeating([](const std::string&) { return true; }),
      base::BindRepeating(&ServeResource));
  web_ui->AddMessageHandler(std::make_unique<KoanWelcomeHandler>(profile));
}

KoanWelcomeUI::~KoanWelcomeUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(KoanWelcomeUI)

}  // namespace koan
