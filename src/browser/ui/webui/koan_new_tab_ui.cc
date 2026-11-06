// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_new_tab_ui.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
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

// The least time between two reloads of one page.
constexpr base::TimeDelta kMinReloadInterval = base::Milliseconds(250);

// The page: nothing but a background, like Zen's. One small document with its
// style inline (the only request), so it paints on the first frame. The
// colour is the new tab page colour of the colour provider of the tab that
// asks, so each window gets its own look's colour: Koan's shadcn mixer maps
// kColorNewTabPageBackground to the look's `--background` (near black,
// #09090b, in the default dark shadcn look), and in the Zen look it is
// Chromium's theme colour for the page, not Zen's window gradient.
SkColor PageColor(const ui::ColorProvider& provider) {
  return provider.GetColor(kColorNewTabPageBackground);
}

// The colour each tab's page was last served in (UI thread; the key is only
// compared, and the page's controller drops its entry), so the page can tell
// when the window's colour moved on while it was loading.
std::map<const content::WebContents*, SkColor>& ServedColors() {
  static base::NoDestructor<std::map<const content::WebContents*, SkColor>>
      colors;
  return *colors;
}

std::string BuildPage(SkColor background) {
  const bool dark = color_utils::IsDark(background);
  return base::StrCat(
      {"<!doctype html><html lang=", g_browser_process->GetApplicationLocale(),
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
      const SkColor background = PageColor(contents->GetColorProvider());
      ServedColors()[contents] = background;
      page = BuildPage(background);
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
    // The page has no script and loads nothing: only its own inline style.
    if (directive == network::mojom::CSPDirectiveName::DefaultSrc) {
      return "default-src 'none';";
    }
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

KoanNewTabUI::~KoanNewTabUI() {
  ServedColors().erase(web_contents());
}

void KoanNewTabUI::OnColorProviderChanged() {
  ScheduleCheck();
}

void KoanNewTabUI::DidFinishNavigation(content::NavigationHandle* handle) {
  // A colour that changed between the request and the commit was not acted
  // on (the page was not there yet).
  if (handle->IsInPrimaryMainFrame() && handle->HasCommitted()) {
    ScheduleCheck();
  }
}

void KoanNewTabUI::DidStopLoading() {
  ScheduleCheck();
}

void KoanNewTabUI::ScheduleCheck() {
  // One check however many changes come (a colour picker being dragged
  // changes the colours many times a second), and not sooner than a quarter
  // of a second after the page's last load.
  if (check_scheduled_) {
    return;
  }
  check_scheduled_ = true;
  base::TimeDelta delay;
  if (!last_reload_.is_null()) {
    delay = std::max(base::TimeDelta(), last_reload_ + kMinReloadInterval -
                                            base::TimeTicks::Now());
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&KoanNewTabUI::ReloadIfStale, weak_factory_.GetWeakPtr()),
      delay);
}

void KoanNewTabUI::ReloadIfStale() {
  check_scheduled_ = false;
  // (This controller lives only as long as the tab shows the page.) Not while
  // a navigation is under way: reloading would discard it (a bookmark clicked
  // in the page, a link): DidStopLoading() checks again.
  const GURL& url = web_contents()->GetLastCommittedURL();
  content::NavigationController& controller = web_contents()->GetController();
  if (!url.SchemeIs(content::kChromeUIScheme) || url.host() != kNewTabHost ||
      web_contents()->IsLoading() || controller.GetPendingEntry()) {
    return;
  }
  // (No record: nothing was served to this tab that is known; leave it.)
  const auto served = ServedColors().find(web_contents());
  if (served == ServedColors().end() ||
      served->second == PageColor(web_contents()->GetColorProvider())) {
    return;
  }
  last_reload_ = base::TimeTicks::Now();
  controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/false);
}

WEB_UI_CONTROLLER_TYPE_IMPL(KoanNewTabUI)

}  // namespace koan
