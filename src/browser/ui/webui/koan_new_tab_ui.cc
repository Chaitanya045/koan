// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_new_tab_ui.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/common/koan_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

// The page's style: shadcn's `--background` token for the look's mode (near
// black, #09090b, by default; with the mode on auto, the system's decides).
// In the Zen style there are no tokens, and the page takes the theme's new
// tab page colour.
std::string BuildStyle(const ui::ColorProvider& provider) {
  const auto rule = [](SkColor background, bool dark) {
    return base::StrCat(
        {"html{background:", color_utils::SkColorToRgbaString(background),
         ";color-scheme:", dark ? "dark" : "light", "}"});
  };
  if (!look::IsShadcnStyle()) {
    return base::StrCat({"html{background:",
                         color_utils::SkColorToRgbaString(
                             provider.GetColor(kColorNewTabPageBackground)),
                         ";color-scheme:light dark}"});
  }
  const auto background = [](bool dark) {
    return look::CurrentTokens(dark)[look::Token::kBackground];
  };
  switch (look::CurrentLook().mode) {
    case look::Mode::kDark:
      return rule(background(true), true);
    case look::Mode::kLight:
      return rule(background(false), false);
    case look::Mode::kAuto:
      return base::StrCat({rule(background(false), false),
                           "@media(prefers-color-scheme:dark){",
                           rule(background(true), true), "}"});
  }
  NOTREACHED();
}

// The page: nothing but a background, like Zen's. It is one small document
// with its style inline (the only request; the style needs the CSP override
// below), so it paints on the first frame.
std::string BuildPage(const ui::ColorProvider& provider) {
  return base::StrCat(
      {"<!doctype html><html lang=en><meta charset=utf-8>"
       "<title>New Tab</title><style>",
       BuildStyle(provider), "</style>"});
}

// Serves the page with the look as it is at the time of the request, so a
// reload follows a change of look or theme. `contents` is null once the tab
// is gone.
void ServePage(base::WeakPtr<content::WebContents> contents,
               const std::string& path,
               content::WebUIDataSource::GotDataCallback callback) {
  std::string page;
  if (contents) {
    page = BuildPage(contents->GetColorProvider());
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(page)));
}

}  // namespace

KoanNewTabUIConfig::KoanNewTabUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kNewTabHost) {}

KoanNewTabUI::KoanNewTabUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), kNewTabHost);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc, "style-src 'unsafe-inline';");
  source->SetRequestFilter(
      base::BindRepeating([](const std::string&) { return true; }),
      base::BindRepeating(&ServePage, web_ui->GetWebContents()->GetWeakPtr()));
}

KoanNewTabUI::~KoanNewTabUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(KoanNewTabUI)

}  // namespace koan
