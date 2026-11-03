// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_welcome_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/string_view_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/ui/webui/koan_welcome_handler.h"
#include "koan/browser/ui/webui/koan_welcome_media.h"
#include "koan/browser/ui/webui/koan_welcome_resources.h"
#include "koan/common/koan_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace koan {

namespace {

// Zen's favicons for the Essentials (welcome_favicons/).
std::string_view Favicon(std::string_view name) {
  static constexpr std::pair<std::string_view, std::string_view> kFavicons[] = {
      {"calendar", kKoanWelcomeFaviconCalendar},
      {"discord", kKoanWelcomeFaviconDiscord},
      {"figma", kKoanWelcomeFaviconFigma},
      {"github", kKoanWelcomeFaviconGithub},
      {"notion", kKoanWelcomeFaviconNotion},
      {"obsidian", kKoanWelcomeFaviconObsidian},
      {"slack", kKoanWelcomeFaviconSlack},
      {"trello", kKoanWelcomeFaviconTrello},
      {"tuta", kKoanWelcomeFaviconTuta},
  };
  for (const auto& [key, svg] : kFavicons) {
    if (key == name) {
      return svg;
    }
  }
  return std::string_view();
}

bool IsSvg(const std::string& path) {
  return base::EndsWith(path, ".svg");
}

void ServeResource(const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  std::string data;
  if (path == "koan_welcome.css") {
    data = kKoanWelcomeCss;
  } else if (path == "koan_welcome.js") {
    data = kKoanWelcomeJs;
  } else if (path == "back.svg") {
    data = kKoanWelcomeBackSvg;
  } else if (path == "forward.svg") {
    data = kKoanWelcomeForwardSvg;
  } else if (path == "grain-bg.png") {
    data = std::string(base::as_string_view(welcome_media::kGrainBg));
  } else if (path == "motion.min.js") {
    data = kKoanWelcomeMotionJs;
  } else if (path.starts_with("favicons/") && IsSvg(path)) {
    data = std::string(Favicon(std::string_view(path).substr(
        9, path.size() - 9 - std::string_view(".svg").size())));
  } else {
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
  // The engines' logos where a build bundles them (chrome://theme).
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src chrome://theme 'self' data:;");
  web_ui->AddMessageHandler(std::make_unique<KoanWelcomeHandler>(profile));
}

KoanWelcomeUI::~KoanWelcomeUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(KoanWelcomeUI)

}  // namespace koan
