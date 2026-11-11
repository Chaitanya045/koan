// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Glance for links that leave a pinned tab's site and for the context menu's
// "Search ... for" (koan_glance.h, Zen's ZenGlanceManager.mjs onTabOpen and
// onSearchSelectCommand).

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_site.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

const PrefService* PrefsFor(content::WebContents* contents) {
  // Incognito's prefs read through to the regular profile's.
  return user_prefs::UserPrefs::Get(contents->GetBrowserContext());
}

// The Glance of the normal window whose active tab is `contents`, or null:
// Glance off, `contents` isn't a tab (a Glance page, DevTools, an extension
// popup) or isn't the active one.
KoanGlanceController* GlanceOfActiveTab(content::WebContents* contents) {
  if (!contents || !features::IsGlanceEnabled() ||
      !PrefsFor(contents)->GetBoolean(prefs::kGlanceEnabled)) {
    return nullptr;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (!browser ||
      browser->GetTabStripModel()->GetActiveWebContents() != contents) {
    return nullptr;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->GetIsNormalType()) {
    return nullptr;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  return controller ? controller->glance() : nullptr;
}

// Looked up again when the task runs: the tab may have closed, changed
// window or stopped being the active one.
void OpenInGlance(base::WeakPtr<content::WebContents> source,
                  const content::OpenURLParams& params) {
  if (KoanGlanceController* glance = GlanceOfActiveTab(source.get())) {
    glance->Open(params, source.get());
  }
}

// On a tab whose context menu just ran "Search ... for": its search opens
// the next new tab (after the data-protection check, which may answer
// later, so this is not a scoped flag). Dropped by the next command, by the
// first new tab from a context menu, and ignored once it is stale (the
// search went to Lens, or was refused).
const void* const kContextMenuSearchKey = &kContextMenuSearchKey;
constexpr base::TimeDelta kContextMenuSearchLifetime = base::Seconds(5);

struct ContextMenuSearch : public base::SupportsUserData::Data {
  base::TimeTicks time = base::TimeTicks::Now();
};

}  // namespace

bool OpenNewTabInGlance(content::RenderFrameHost* opener,
                        const GURL& target_url,
                        const content::Referrer& referrer,
                        WindowOpenDisposition disposition,
                        bool user_gesture,
                        bool opener_suppressed) {
  if (!opener || disposition != WindowOpenDisposition::kNewForegroundTab ||
      !user_gesture || !opener_suppressed) {
    return false;
  }
  content::WebContents* source =
      content::WebContents::FromRenderFrameHost(opener);
  if (!source || !IsPinnedForUser(source) ||
      !PrefsFor(source)->GetBoolean(prefs::kGlanceOpenPinnedExternalLinks) ||
      !LeavesPinnedSite(source->GetLastCommittedURL(), target_url) ||
      !GlanceOfActiveTab(source)) {
    return false;
  }
  // Like "Open Link in Glance": the referrer under its policy and the
  // frame's origin. http(s) only (LeavesPinnedSite()).
  content::OpenURLParams params(
      target_url, content::Referrer::SanitizeForRequest(target_url, referrer),
      WindowOpenDisposition::kCurrentTab, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  params.initiator_origin = opener->GetLastCommittedOrigin();
  // CanCreateWindow() answers a sync IPC from the renderer: Glance creates
  // its WebContents and widget after it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OpenInGlance, source->GetWeakPtr(), std::move(params)));
  return true;
}

void OnContextMenuCommand(content::WebContents* source, int command_id) {
  if (!source) {
    return;
  }
  if (command_id == IDC_CONTENT_CONTEXT_SEARCHWEBFOR) {
    source->SetUserData(kContextMenuSearchKey,
                        std::make_unique<ContextMenuSearch>());
  } else {
    source->RemoveUserData(kContextMenuSearchKey);
  }
}

bool OpenContextMenuSearchInGlance(content::WebContents* source,
                                   const content::OpenURLParams& params) {
  if (!source || !params.started_from_context_menu) {
    return false;
  }
  const auto* search = static_cast<const ContextMenuSearch*>(
      source->GetUserData(kContextMenuSearchKey));
  if (!search) {
    return false;
  }
  const bool fresh =
      base::TimeTicks::Now() - search->time < kContextMenuSearchLifetime;
  source->RemoveUserData(kContextMenuSearchKey);
  if (!fresh ||
      params.disposition != WindowOpenDisposition::kNewForegroundTab ||
      !params.url.SchemeIsHTTPOrHTTPS() ||
      !PrefsFor(source)->GetBoolean(prefs::kGlanceContextMenuSearch)) {
    return false;
  }
  KoanGlanceController* glance = GlanceOfActiveTab(source);
  if (!glance) {
    return false;
  }
  glance->Open(params, source);
  return true;
}

}  // namespace koan
