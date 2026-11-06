// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_new_tab_ui.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "koan/common/koan_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

// The page: nothing but a background, like Zen's. One small document with its
// style inline (the only request), so it paints on the first frame. The
// colour is the new tab page colour of the colour provider of the tab that
// asks, so each window gets its own look's colour: Koan's shadcn mixer maps
// kColorNewTabPageBackground to the look's `--background` (near black,
// #09090b, in the default dark shadcn look), and in the Zen look it is
// Chromium's theme colour for the page, not Zen's window gradient.
std::string BuildPage(const ui::ColorProvider& provider) {
  const SkColor background = provider.GetColor(kColorNewTabPageBackground);
  const bool dark = color_utils::IsDark(background);
  return base::StrCat(
      {"<!doctype html><html lang=", base::i18n::GetConfiguredLocale(),
       "><meta charset=utf-8><title>",
       base::EscapeForHTML(l10n_util::GetStringUTF8(IDS_NEW_TAB_TITLE)),
       "</title><style>html{background:",
       color_utils::SkColorToRgbaString(background),
       ";color-scheme:", dark ? "dark" : "light", "}</style>"});
}

// Serves the page for the tab that asks (never a tab remembered at the time
// of an earlier request: one source serves every new tab page of the
// profile). `wc_getter` returns null once the tab is gone.
class KoanNewTabSource : public content::URLDataSource {
 public:
  // content::URLDataSource:
  std::string GetSource() override { return kNewTabHost; }

  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override {
    std::string page;
    if (content::WebContents* contents = wc_getter.Run()) {
      page = BuildPage(contents->GetColorProvider());
    }
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(std::move(page)));
  }

  std::string GetMimeType(const GURL& url) override { return "text/html"; }

  // Never cached: the colour is the tab's look at the time.
  bool AllowCaching() override { return false; }

  // The page's style is inline.
  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    if (directive == network::mojom::CSPDirectiveName::StyleSrc) {
      return "style-src 'unsafe-inline';";
    }
    return content::URLDataSource::GetContentSecurityPolicy(directive);
  }
};

}  // namespace

KoanNewTabUIConfig::KoanNewTabUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kNewTabHost) {}

KoanNewTabUI::KoanNewTabUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui),
      content::WebContentsObserver(web_ui->GetWebContents()) {
  content::URLDataSource::Add(Profile::FromWebUI(web_ui),
                              std::make_unique<KoanNewTabSource>());
}

KoanNewTabUI::~KoanNewTabUI() = default;

void KoanNewTabUI::OnColorProviderChanged() {
  // A change of the window's look or theme (the Space's colours too): load the
  // page again, it has no state; it takes the new colour. Not from inside the
  // change.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&KoanNewTabUI::Reload, weak_factory_.GetWeakPtr()));
}

void KoanNewTabUI::Reload() {
  // (This controller lives only as long as the tab shows the page.)
  const GURL& url = web_contents()->GetLastCommittedURL();
  if (url.SchemeIs(content::kChromeUIScheme) && url.host() == kNewTabHost) {
    web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                           /*check_for_repost=*/false);
  }
}

WEB_UI_CONTROLLER_TYPE_IMPL(KoanNewTabUI)

}  // namespace koan
