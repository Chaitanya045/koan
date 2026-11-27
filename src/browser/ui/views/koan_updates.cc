// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_updates.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "koan/browser/agent/koan_agent_forwarder.h"
#include "koan/browser/agent/koan_agent_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_sidebar_notification.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_update_installer.h"
#include "koan/browser/ui/views/koan_zen_sidebar_notification.h"
#include "koan/browser/updates/koan_update_install_logic.h"
#include "koan/browser/updates/koan_update_logic.h"
#include "koan/browser/updates/koan_update_platform.h"
#include "koan/browser/updates/koan_update_source.h"
#include "koan/version/koan_version.h"
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

constexpr base::TimeDelta kRequestTimeout = base::Seconds(30);
// After the window is in its widget, for the sidebar to be laid out.
constexpr base::TimeDelta kWindowSettleDelay = base::Milliseconds(500);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("koan_update_check", R"(
      semantics {
        sender: "Koan update check"
        description:
          "At most every 12 hours Koan downloads the signed update manifest "
          "(koan-update.json, about 1 KB) of its latest release on GitHub, "
          "to tell the user when a newer version is out. On the Mac, a newer "
          "release is then downloaded and installed (koan_update_download)."
        trigger:
          "A minute after the first normal window opens, then at most every "
          "12 hours while one is open."
        data:
          "None: a fixed URL. No cookies, credentials or identifiers."
        destination: OTHER
        destination_other:
          "github.com (koan-browser/koan's releases), which redirects to "
          "GitHub's release asset host."
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings (chrome://settings/koan/looks), Look and Feel > Updates > "
          "Check for updates."
        policy_exception_justification: "Not implemented."
      })");

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

// Linux: the Flatpak can't replace itself. The release page, to download
// the bundle, and the command that installs it from the Downloads folder.
void DownloadFlatpak(BrowserView* browser_view,
                     GURL release_page,
                     std::optional<std::string> install_command) {
  OpenInNewTab(browser_view, release_page);
  if (!install_command) {
    return;
  }
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(base::UTF8ToUTF16(*install_command));
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view)) {
    controller->toast()->Show(
        u"Install command copied! Run it once the download is done.");
  }
}

// Whether an agent is connected to Koan or holds a tab.
bool AgentIsUsingKoan() {
  const agent::AgentControlService* service = agent::AgentControlService::Get();
  return service &&
         (!service->clients().empty() || !service->controlled_tabs().empty());
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
    if (installer_) {
      installer_->OnWindowShown();
    }
    if (complete_pending_ || update_pending_) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Updater::ShowPending, base::Unretained(this)),
          kWindowSettleDelay);
    }
  }

  bool IsReadyToInstall() const { return installer_ && installer_->IsReady(); }

  // Whether another Koan runs from the app (`callback`; false without an
  // installer).
  void CheckOtherInstances(base::OnceCallback<void(bool)> callback) {
    if (!installer_) {
      std::move(callback).Run(false);
      return;
    }
    installer_->CheckOtherInstances(std::move(callback));
  }

 private:
  PrefService* local_state() { return g_browser_process->local_state(); }

  void Start() {
    started_ = true;
    // Zen's checkForZenUpdates(): once per build, never on the first launch.
    // A build of 0.0.2 or before stamped its commit instead, so the first
    // launch after it says so too.
    const std::string build =
        BuildStamp(koan::version::Full(), version_info::GetVersionNumber());
    const std::string previous_build =
        local_state()->GetString(prefs::kUpdatesLastBuild);
    if (previous_build != build) {
      local_state()->SetString(prefs::kUpdatesLastBuild, build);
      complete_pending_ =
          !previous_build.empty() &&
          local_state()->GetBoolean(prefs::kUpdatesShowUpdateNotification);
    }
    local_state()->ClearPref(prefs::kUpdatesLatestCommitObsolete);
    local_state()->ClearPref(prefs::kUpdatesLatestCommitTimeObsolete);
    local_state()->ClearPref(prefs::kUpdatesNotifiedCommitObsolete);

    platform_ = CurrentPlatform();
    source_ = GetUpdateSource(*base::CommandLine::ForCurrentProcess());
    if (platform_) {
      if (std::unique_ptr<InstallPlatform> install = CreateInstallPlatform()) {
        installer_ = std::make_unique<UpdateInstaller>(
            std::move(install), source_,
            base::BindRepeating(&Updater::ShowInstallNotice,
                                base::Unretained(this)));
        installer_->Start();
      }
    }
    // A release found before (and not shown, or not yet shown for it),
    // verified again: with this build's key.
    ReadManifest(local_state()->GetString(prefs::kUpdatesLatestManifest));
    registrar_.Init(local_state());
    registrar_.Add(prefs::kUpdatesCheckEnabled,
                   base::BindRepeating(&Updater::OnCheckSettingChanged,
                                       base::Unretained(this)));
    if (installer_) {
      registrar_.Add(prefs::kUpdatesAutoInstall,
                     base::BindRepeating(&UpdateInstaller::OnSettingsChanged,
                                         base::Unretained(installer_.get())));
    }
    ScheduleCheck();
  }

  // "Check for updates" changed: the installer stops a download when it is
  // turned off, and a release found before is taken again when it is back
  // on (rather than at the next check, up to 12 hours later).
  void OnCheckSettingChanged() {
    ScheduleCheck();
    if (!installer_) {
      return;
    }
    installer_->OnSettingsChanged();
    if (CanCheck()) {
      ReadManifest(local_state()->GetString(prefs::kUpdatesLatestManifest));
    }
  }

  bool CanCheck() {
    return local_state()->GetBoolean(prefs::kUpdatesCheckEnabled) &&
           platform_.has_value();
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
    // Counted whatever the outcome: one request every 12 hours at most, also
    // when GitHub is unreachable.
    local_state()->SetTime(prefs::kUpdatesLastCheck, base::Time::Now());
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = source_.manifest_url;
    request->method = net::HttpRequestHeaders::kGetMethod;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_DISABLE_CACHE;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetTimeoutDuration(kRequestTimeout);
    loader_->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&Updater::OnResponse, base::Unretained(this)),
        kMaxManifestSize);
  }

  void OnResponse(std::optional<std::string> body) {
    loader_.reset();
    if (g_browser_process->IsShuttingDown()) {
      return;
    }
    // A manifest that doesn't verify changes nothing (not even the one kept).
    if (body && ReadManifest(*body)) {
      local_state()->SetString(prefs::kUpdatesLatestManifest, *body);
      if (update_pending_) {
        ShowPending();
      }
    }
    ScheduleCheck();
  }

  // Whether `json` is a manifest for this platform that verifies. If it is,
  // the release is what "Update Available!" will tell about, once per newer
  // version.
  bool ReadManifest(std::string_view json) {
    if (!platform_ || json.empty()) {
      return false;
    }
    base::expected<UpdateInfo, ManifestError> update =
        ParseManifest(json, *platform_, source_.public_key);
    if (!update.has_value()) {
      VLOG(1) << "Ignoring the update manifest: error "
              << static_cast<int>(update.error());
      return false;
    }
    // The Mac installs it itself, and says what it does (ShowInstallNotice()).
    if (installer_) {
      if (CanCheck() &&
          IsNewerVersion(update->version, koan::version::Full())) {
        installer_->OnUpdateFound(*update);
      }
      return true;
    }
    notice_pending_ = InstallNotice::kDownloadByHand;
    if (ShouldNotify(
            update->version, koan::version::Full(),
            local_state()->GetString(prefs::kUpdatesNotifiedVersion))) {
      update_pending_ = std::move(update).value();
    } else {
      update_pending_.reset();
    }
    return true;
  }

  // From the installer: what to tell about `update`. The release page shows
  // once per version, as on Linux. Update ("let me choose") shows once per
  // launch until it is clicked, since nothing else starts the download;
  // "Restart to update" each time it is asked for (when the update is
  // staged, and after 6 days).
  void ShowInstallNotice(const UpdateInfo& update, InstallNotice notice) {
    if (notice == InstallNotice::kDownloadByHand &&
        !ShouldNotify(
            update.version, koan::version::Full(),
            local_state()->GetString(prefs::kUpdatesNotifiedVersion))) {
      return;
    }
    update_pending_ = update;
    notice_pending_ = notice;
    ShowPending();
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
    // Not while Check for updates is off, also for a release found before it
    // was turned off (it shows after the next check once it is on).
    if (update_pending_ && CanCheck() &&
        ShowUpdateAvailable(browser_view, *update_pending_, notice_pending_)) {
      if (notice_pending_ == InstallNotice::kDownloadByHand) {
        local_state()->SetString(prefs::kUpdatesNotifiedVersion,
                                 update_pending_->version);
      }
      update_pending_.reset();
    }
  }

  // Zen's links: what's new, support Zen, restart in safe mode. Koan has no
  // donations, and Chromium has no safe mode: what broke goes to the issues.
  bool ShowUpdateComplete(BrowserView* browser_view) {
    SidebarNotificationParams params;
    params.heading = kUpdatedHeading;
    params.auto_hide = kUpdateNotificationTimeout;
    params.delay = kSidebarNotificationInDelay - kWindowSettleDelay;
    params.links.emplace_back(
        kKoanSparklesIcon, kUpdatedLabel, kUpdatedTooltip,
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       ReleasePageUrl(koan::version::Full())),
        /*special=*/true);
    params.links.emplace_back(
        kKoanSecurityBrokenIcon, kSomethingBrokeLabel, u"Report an issue",
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       IssuesUrl()));
    return ShowSidebarNotification(browser_view, std::move(params));
  }

  bool ShowUpdateAvailable(BrowserView* browser_view,
                           const UpdateInfo& update,
                           InstallNotice notice) {
    SidebarNotificationParams params;
    params.heading = u"Update Available!";
    params.auto_hide = kUpdateNotificationTimeout;
    params.delay = kSidebarNotificationInDelay - kWindowSettleDelay;
    const std::u16string version = base::UTF8ToUTF16(update.version);
    params.links.emplace_back(
        kKoanSparklesIcon, u"What's new",
        base::StrCat({u"What's new in Koan ", version}),
        base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                       update.notes_url),
        /*special=*/true);
    if (notice == InstallNotice::kRestartToUpdate) {
      params.links.emplace_back(
          kKoanReloadIcon, u"Restart to update",
          base::StrCat({u"Restart Koan to finish updating to ", version}),
          base::BindOnce(&RestartToUpdate, base::Unretained(browser_view)));
      return ShowSidebarNotification(browser_view, std::move(params));
    }
    if (notice == InstallNotice::kAskToUpdate && installer_) {
      params.links.emplace_back(
          kKoanDownloadsIcon, u"Update",
          base::StrCat({u"Download Koan ", version,
                        u" and install it when Koan restarts"}),
          base::BindOnce(&UpdateInstaller::Accept,
                         base::Unretained(installer_.get())));
      return ShowSidebarNotification(browser_view, std::move(params));
    }
    const GURL release_page = ReleasePageUrl(update.version);
    const std::u16string download_tooltip =
        base::StrCat({u"Koan ", version, u" on GitHub"});
    if (update.platform == kPlatformLinuxX8664) {
      params.links.emplace_back(
          kKoanDownloadsIcon, u"Download", download_tooltip,
          base::BindOnce(
              &DownloadFlatpak, base::Unretained(browser_view), release_page,
              FlatpakInstallCommand(update, DownloadPrefs::FromBrowserContext(
                                                browser_view->GetProfile())
                                                ->DownloadPath()
                                                .AsUTF8Unsafe())));
    } else {
      // The Mac, where Koan can't install it itself.
      params.links.emplace_back(
          kKoanDownloadsIcon, u"Download", download_tooltip,
          base::BindOnce(&OpenInNewTab, base::Unretained(browser_view),
                         release_page));
    }
    return ShowSidebarNotification(browser_view, std::move(params));
  }

  bool started_ = false;
  bool complete_pending_ = false;
  std::optional<std::string_view> platform_;
  UpdateSource source_;
  // The newer release to tell about, and how.
  std::optional<UpdateInfo> update_pending_;
  InstallNotice notice_pending_ = InstallNotice::kDownloadByHand;
  // The Mac's (null elsewhere).
  std::unique_ptr<UpdateInstaller> installer_;
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

bool IsUpdateReadyToInstall() {
  return GetUpdater().IsReadyToInstall();
}

namespace {

void ContinueRestart(base::WeakPtr<BrowserView> browser_view,
                     bool other_instance_running) {
  const gfx::NativeWindow window =
      browser_view ? browser_view->GetNativeWindow() : gfx::NativeWindow();
  switch (DecideRestart(IsUpdateReadyToInstall(), other_instance_running,
                        AgentIsUsingKoan())) {
    case RestartAction::kNone:
      return;
    case RestartAction::kQuitOtherFirst:
      // The helper can't replace the app while that one runs from it, so a
      // restart now would only bring this version back.
      chrome::ShowWarningMessageBoxAsync(
          window, u"Quit the other Koan first",
          u"Another Koan runs from this app, with its own profile folder. "
          u"Quit it, then restart to update.");
      return;
    case RestartAction::kAsk:
      // Nothing restarts on its own, and not under an agent without asking.
      chrome::ShowQuestionMessageBoxAsync(
          window, u"Restart to update Koan?",
          u"An agent is using Koan. Restart anyway?",
          base::BindOnce([](chrome::MessageBoxResult result) {
            if (result == chrome::MESSAGE_BOX_RESULT_YES &&
                IsUpdateReadyToInstall()) {
              chrome::AttemptRelaunch();
            }
          }));
      return;
    case RestartAction::kRestart:
      chrome::AttemptRelaunch();
      return;
  }
}

}  // namespace

void RestartToUpdate(BrowserView* browser_view) {
  if (!IsUpdateReadyToInstall()) {
    return;
  }
  GetUpdater().CheckOtherInstances(base::BindOnce(
      &ContinueRestart, browser_view ? browser_view->GetAsWeakPtr()
                                     : base::WeakPtr<BrowserView>()));
}

}  // namespace koan::updates
