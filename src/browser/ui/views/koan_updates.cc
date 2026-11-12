// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_updates.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_sidebar_notification.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_zen_sidebar_notification.h"
#include "koan/browser/updates/koan_build_info.h"
#include "koan/browser/updates/koan_update_logic.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"

namespace koan::updates {

namespace {

// GitHub's list of commits for one commit is a few KB.
constexpr size_t kMaxResponseSize = 256 * 1024;
constexpr base::TimeDelta kRequestTimeout = base::Seconds(30);
// After the window is in its widget, for the sidebar to be laid out.
constexpr base::TimeDelta kWindowSettleDelay = base::Milliseconds(500);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_update_check", R"(
      semantics {
        sender: "Koan update check"
        description:
          "Koan is built from source. Once a day it asks GitHub for the "
          "newest commit of its repository's develop branch, to tell the "
          "user when `git pull && scripts/build.sh` would update it."
        trigger: "At most once a day while a normal window is open."
        data:
          "None: the repository's public name and the branch. No cookies, "
          "credentials or identifiers."
        destination: OTHER
        destination_other: "api.github.com"
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings (chrome://settings/koan/looks), Look and Feel > Updates > "
          "Check for updates."
        policy_exception_justification: "Not implemented."
      })");

// The repository the links point at.
std::string_view LinkRepo() {
  return build_info::GitHubRepo().empty() ? std::string_view(kDefaultGitHubRepo)
                                          : build_info::GitHubRepo();
}

// A normal window of a regular profile, the last one used.
BrowserView* FindWindowForNotice() {
  BrowserWindowInterface* window =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  if (!window || window->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      window->GetProfile()->IsOffTheRecord()) {
    return nullptr;
  }
  return BrowserView::GetBrowserViewForBrowser(window);
}

void OpenInNewTab(BrowserView* browser_view, GURL url) {
  chrome::AddTabAt(browser_view->browser(), url, -1, /*foreground=*/true);
}

void CopyUpdateCommand(BrowserView* browser_view) {
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(kUpdateCommand);
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view)) {
    controller->toast()->Show(u"Copied! Run it in your Koan folder.");
  }
}

class Updater {
 public:
  Updater() = default;
  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;

  void OnWindowShown(BrowserView* browser_view) {
    if (browser_view->browser()->GetType() !=
            BrowserWindowInterface::TYPE_NORMAL ||
        browser_view->GetProfile()->IsOffTheRecord()) {
      return;
    }
    if (!started_) {
      Start();
    }
    if (complete_pending_ || update_pending_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Updater::ShowPending, base::Unretained(this)),
          kWindowSettleDelay);
    }
  }

 private:
  PrefService* local_state() { return g_browser_process->local_state(); }

  void Start() {
    started_ = true;
    // Zen's checkForZenUpdates(): once per build, never on the first launch.
    const std::string build = build_info::Stamp();
    previous_build_ = local_state()->GetString(prefs::kUpdatesLastBuild);
    if (previous_build_ != build) {
      local_state()->SetString(prefs::kUpdatesLastBuild, build);
      complete_pending_ =
          !previous_build_.empty() &&
          local_state()->GetBoolean(prefs::kUpdatesShowUpdateNotification);
    }
    // A newer commit found before (and not shown, or not yet shown for it).
    const Commit latest{
        local_state()->GetString(prefs::kUpdatesLatestCommit),
        local_state()->GetTime(prefs::kUpdatesLatestCommitTime)};
    if (!latest.sha.empty()) {
      OnLatestCommit(latest);
    }
    registrar_.Init(local_state());
    registrar_.Add(
        prefs::kUpdatesCheckEnabled,
        base::BindRepeating(&Updater::ScheduleCheck, base::Unretained(this)));
    ScheduleCheck();
  }

  bool CanCheck() {
    return local_state()->GetBoolean(prefs::kUpdatesCheckEnabled) &&
           !build_info::Commit().empty() && !build_info::GitHubRepo().empty();
  }

  void ScheduleCheck() {
    if (!CanCheck()) {
      check_timer_.Stop();
      loader_.reset();
      return;
    }
    if (loader_) {
      return;
    }
    check_timer_.Start(
        FROM_HERE,
        NextCheckDelay(local_state()->GetTime(prefs::kUpdatesLastCheck),
                       base::Time::Now()),
        base::BindOnce(&Updater::Check, base::Unretained(this)));
  }

  void Check() {
    if (!CanCheck() || g_browser_process->IsShuttingDown()) {
      return;
    }
    // Counted whatever the outcome: one request a day at most, also when
    // GitHub is unreachable or rate limits.
    local_state()->SetTime(prefs::kUpdatesLastCheck, base::Time::Now());
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = LatestCommitUrl(build_info::GitHubRepo());
    request->method = net::HttpRequestHeaders::kGetMethod;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_DISABLE_CACHE;
    request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                               "application/vnd.github+json");
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetTimeoutDuration(kRequestTimeout);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnResponse, base::Unretained(this)),
        kMaxResponseSize);
  }

  void OnResponse(std::optional<std::string> body) {
    loader_.reset();
    if (g_browser_process->IsShuttingDown()) {
      return;
    }
    if (const std::optional<Commit> latest =
            body ? ParseLatestCommit(*body) : std::nullopt) {
      local_state()->SetString(prefs::kUpdatesLatestCommit, latest->sha);
      local_state()->SetTime(prefs::kUpdatesLatestCommitTime, latest->time);
      OnLatestCommit(*latest);
      if (update_pending_) {
        ShowPending();
      }
    }
    ScheduleCheck();
  }

  // Once per newer commit.
  void OnLatestCommit(const Commit& latest) {
    if (IsUpdateAvailable(build_info::Commit(), build_info::CommitTime(),
                          latest) &&
        local_state()->GetString(prefs::kUpdatesNotifiedCommit) != latest.sha) {
      update_pending_ = latest.sha;
    }
  }

  void ShowPending() {
    BrowserView* browser_view = FindWindowForNotice();
    if (!browser_view || g_browser_process->IsShuttingDown()) {
      return;
    }
    // One at a time: what's new after an update first.
    if (complete_pending_) {
      complete_pending_ = !ShowUpdateComplete(browser_view);
      return;
    }
    // Not while Check for updates is off, also for a newer commit found
    // before it was turned off (it shows after the next check once it is on).
    if (update_pending_ && CanCheck() &&
        ShowUpdateAvailable(browser_view, *update_pending_)) {
      local_state()->SetString(prefs::kUpdatesNotifiedCommit, *update_pending_);
      update_pending_.reset();
    }
  }

  // Zen's links: what's new, support Zen, restart in safe mode. Koan has no
  // releases (what's new is the commits between the two builds), no
  // donations, and Chromium has no safe mode: what broke goes to the issues.
  bool ShowUpdateComplete(BrowserView* browser_view) {
    SidebarNotificationParams params;
    params.heading = kUpdatedHeading;
    params.auto_hide = kUpdateNotificationTimeout;
    params.delay = kSidebarNotificationInDelay - kWindowSettleDelay;
    const GURL whats_new =
        WhatsNewUrl(LinkRepo(), CommitOfStamp(previous_build_),
                    CommitOfStamp(build_info::Stamp()));
    params.links.emplace_back(
        kKoanSparklesIcon, kUpdatedLabel, kUpdatedTooltip,
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       whats_new),
        /*special=*/true);
    params.links.emplace_back(
        kKoanSecurityBrokenIcon, kSomethingBrokeLabel, u"Report an issue",
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       IssuesUrl(LinkRepo())));
    return ShowSidebarNotification(browser_view, std::move(params));
  }

  bool ShowUpdateAvailable(BrowserView* browser_view,
                           const std::string& latest) {
    SidebarNotificationParams params;
    params.heading = u"Update Available!";
    params.auto_hide = kUpdateNotificationTimeout;
    params.delay = kSidebarNotificationInDelay - kWindowSettleDelay;
    params.links.emplace_back(
        kKoanSparklesIcon, u"What's new", u"The new commits on GitHub",
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       WhatsNewUrl(LinkRepo(), build_info::Commit(), latest)),
        /*special=*/true);
    params.links.emplace_back(
        kKoanEditCopyIcon, u"Copy the update command",
        std::u16string(kUpdateCommand),
        base::BindOnce(&CopyUpdateCommand, base::Unretained(browser_view)));
    params.links.emplace_back(
        kKoanHelpIcon, u"How to update", u"Updating, in BUILDING-MAC.md",
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       HowToUpdateUrl(LinkRepo())));
    return ShowSidebarNotification(browser_view, std::move(params));
  }

  bool started_ = false;
  std::string previous_build_;
  bool complete_pending_ = false;
  // The newer commit to tell about.
  std::optional<std::string> update_pending_;
  PrefChangeRegistrar registrar_;
  base::OneShotTimer check_timer_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

Updater& GetUpdater() {
  static base::NoDestructor<Updater> updater;
  return *updater;
}

}  // namespace

void OnBrowserWindowShown(BrowserView* browser_view) {
  GetUpdater().OnWindowShown(browser_view);
}

}  // namespace koan::updates
