// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_new_tab_ui.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "koan/common/koan_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

// The page: nothing but a background. It is one small document with its
// style inline (the only request; the style needs the CSP override below), so
// it paints on the first frame. `color-scheme` keeps the canvas, scrollbars
// and the frame before the first paint in the theme's mode.
//
// TODO(#48, https://github.com/Chaitanya045/koan/issues/48): the background
// is the theme's new tab page colour for now; take shadcn's `--background`
// token from the style once #48 lands.
std::string BuildPage(const std::string& background) {
  return base::StrCat(
      {"<!doctype html><html lang=en><meta charset=utf-8>"
       "<meta name=color-scheme content=\"light dark\">"
       "<title>New Tab</title>"
       "<style>html{background:",
       background, "}</style>"});
}

// Serves the page in the colours the tab shows at the time of the request, so
// a reload follows a theme change. `contents` is null once the tab is gone.
void ServePage(base::WeakPtr<content::WebContents> contents,
               const std::string& path,
               content::WebUIDataSource::GotDataCallback callback) {
  std::string background;
  if (contents) {
    background = color_utils::SkColorToRgbaString(
        contents->GetColorProvider().GetColor(kColorNewTabPageBackground));
  }
  std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(
      BuildPage(background.empty() ? "Canvas" : background)));
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
