// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Notifications in a Space's profile (#478, #530,
// .agent/notes/upstream/space-containers.md, "Notifications"): a page there
// asks for the permission and shows its notifications like a default tab,
// names its Space in them, and a click shows its tab. A service worker there
// shows notifications whose click and close reach that profile's worker only,
// and whose openWindow() and focus() land in its Space. Push stays refused.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/test/mock_permission_prompt_factory.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/url_formatter/elide_url.h"
#include "content/public/browser/notification_event_dispatcher.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/persistent_notification_status.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_page_notifications.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/message_center/public/cpp/notification.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace koan {

namespace {

// Chromium's own notification test page (chrome/test/data/notifications):
// RequestPermission(), DisplayPersistentNotification(), a service worker.
constexpr char kTestPage[] =
    "/notifications/platform_notification_service.html";

// Subscribes the page's service worker to push; the result, or the error's
// name and message.
constexpr char kSubscribePush[] = R"(
  navigator.serviceWorker.ready
      .then(registration => registration.pushManager.subscribe({
        userVisibleOnly: true,
        applicationServerKey: new Uint8Array(65).fill(4),
      }))
      .then(() => 'subscribed', error => error.name + ': ' + error.message))";

// A site whose service worker shows the notifications (#530). The worker
// tells every page of its partition what it got ("click:<title>",
// "close:<title>"), opens `data.open` on a click with clients.openWindow(),
// and focuses the page that showed it with `data.focus`.
constexpr char kWorkerPage[] = "/sw_page.html";
constexpr char kWorkerPageHtml[] = R"(<!doctype html><title>Mail</title><script>
  window.received = [];
  navigator.serviceWorker.addEventListener(
      'message', event => window.received.push(event.data));
  async function Ready() {
    await navigator.serviceWorker.register('/sw.js');
    await navigator.serviceWorker.ready;
    return 'ok';
  }
  async function Show(title, options) {
    const registration = await navigator.serviceWorker.ready;
    try {
      await registration.showNotification(title, options || {});
      return 'ok';
    } catch (error) {
      return '' + error;
    }
  }
  async function Titles() {
    const registration = await navigator.serviceWorker.ready;
    return (await registration.getNotifications()).map(n => n.title).join();
  }
</script>)";
constexpr char kWorkerJs[] = R"(
  async function Tell(message) {
    const pages =
        await clients.matchAll({type: 'window', includeUncontrolled: true});
    for (const page of pages) page.postMessage(message);
    return pages;
  }
  addEventListener('notificationclick', event => {
    const data = event.notification.data || {};
    event.waitUntil(Tell('click:' + event.notification.title).then(pages => {
      if (data.open) return clients.openWindow(data.open);
      if (data.focus && pages.length) return pages[0].focus();
    }));
  });
  addEventListener('notificationclose', event => {
    event.waitUntil(Tell('close:' + event.notification.title));
  });
)";

std::unique_ptr<net::test_server::HttpResponse> ServeWorkerSite(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (request.GetURL().path() == kWorkerPage) {
    response->set_content_type("text/html");
    response->set_content(kWorkerPageHtml);
  } else if (request.GetURL().path() == "/sw.js") {
    response->set_content_type("text/javascript");
    response->set_content(kWorkerJs);
  } else {
    return nullptr;
  }
  return response;
}

class KoanSpaceNotificationsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeWorkerSite));
    ASSERT_TRUE(embedded_test_server()->Start());
    notifications_ =
        std::make_unique<NotificationDisplayServiceTester>(profile());
  }

  void TearDownOnMainThread() override {
    notifications_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }
  GURL TestPage() { return embedded_test_server()->GetURL(kTestPage); }

  // A new profile (New Profile...), Serialize()d.
  std::string AddProfile(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), name);
    return container ? containers::Serialize(container->id) : std::string();
  }

  // Creates a Space called Work with a profile of its own, and shows it.
  tab_groups::TabGroupId CreateWorkSpace() {
    KoanWorkspaceController* spaces = GetSpaces(browser());
    EXPECT_TRUE(spaces);
    EXPECT_TRUE(WaitForSpaceCount(browser(), 1));
    const std::string work = AddProfile(u"Work Profile");
    EXPECT_FALSE(work.empty());
    spaces->CreateSpace(u"Work", u"", {}, work);
    EXPECT_TRUE(WaitForSpaceCount(browser(), 2));
    return *spaces->GetActiveSpace();
  }

  // Opens `url` (the test page by default) in a new tab of the active Space,
  // like Cmd+T and the URL bar.
  content::WebContents* OpenTestPage(std::optional<GURL> url = std::nullopt) {
    NavigateParams params(browser(), url.value_or(TestPage()),
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents;
  }

  // Asks for the permission in `contents`; the answer, with `prompts`
  // counting the prompts and accepting them.
  std::string RequestPermission(
      content::WebContents* contents,
      permissions::MockPermissionPromptFactory& prompts) {
    prompts.set_response_type(
        permissions::PermissionRequestManager::ACCEPT_ALL);
    return content::EvalJs(contents, "RequestPermission()").ExtractString();
  }

  std::vector<message_center::Notification> PageNotifications() {
    return notifications_->GetDisplayedNotificationsForType(
        NotificationHandler::Type::WEB_NON_PERSISTENT);
  }

  // Shows a page notification titled `title` from `contents`; the
  // notification, as the platform gets it.
  std::optional<message_center::Notification> ShowPageNotification(
      content::WebContents* contents,
      const std::string& title) {
    const size_t before = PageNotifications().size();
    if (!content::ExecJs(contents, "new Notification('" + title + "')")) {
      return std::nullopt;
    }
    if (!base::test::RunUntil(
            [&] { return PageNotifications().size() > before; })) {
      return std::nullopt;
    }
    return PageNotifications().back();
  }

  std::u16string Site() {
    return url_formatter::FormatOriginForSecurityDisplay(
        url::Origin::Create(TestPage()),
        url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS);
  }

  GURL WorkerPage() { return embedded_test_server()->GetURL(kWorkerPage); }

  // The worker site allowed to notify, as a site permission: every partition
  // shares it.
  void AllowWorkerSite() {
    HostContentSettingsMapFactory::GetForProfile(profile())
        ->SetContentSettingDefaultScope(WorkerPage(), GURL(),
                                        ContentSettingsType::NOTIFICATIONS,
                                        CONTENT_SETTING_ALLOW);
  }

  // Opens the worker site in a new tab of `space` (shown first), with its
  // service worker ready.
  content::WebContents* OpenWorkerSite(const tab_groups::TabGroupId& space) {
    GetSpaces(browser())->SwitchTo(space);
    content::WebContents* contents = OpenTestPage(WorkerPage());
    if (!contents || content::EvalJs(contents, "Ready()") != "ok") {
      return nullptr;
    }
    return contents;
  }

  std::vector<message_center::Notification> WorkerNotifications() {
    return notifications_->GetDisplayedNotificationsForType(
        NotificationHandler::Type::WEB_PERSISTENT);
  }

  // Shows a service-worker notification titled `title` from `contents` with
  // `options` (a JS object); the notification, as the platform gets it.
  std::optional<message_center::Notification> ShowWorkerNotification(
      content::WebContents* contents,
      const std::string& title,
      const std::string& options = "{}") {
    if (content::EvalJs(contents, "Show('" + title + "', " + options + ")") !=
        "ok") {
      return std::nullopt;
    }
    std::optional<message_center::Notification> shown;
    if (!base::test::RunUntil([&] {
          for (const message_center::Notification& notification :
               WorkerNotifications()) {
            if (notification.title() == base::UTF8ToUTF16(title)) {
              shown = notification;
            }
          }
          return shown.has_value();
        })) {
      return std::nullopt;
    }
    return shown;
  }

  // What the page's service worker told it, comma-separated.
  std::string Received(content::WebContents* contents) {
    return content::EvalJs(contents, "window.received.join()").ExtractString();
  }

  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
};

}  // namespace

// The case upstream refuses: a page in a Space's profile gets the prompt, and
// its notification names the Space.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       PageInSpaceProfileAsksAndNotifies) {
  CreateWorkSpace();
  content::WebContents* contents = OpenTestPage();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(containers::ContainerOf(contents));
  permissions::MockPermissionPromptFactory prompts(
      permissions::PermissionRequestManager::FromWebContents(contents));

  EXPECT_EQ("granted", RequestPermission(contents, prompts));
  EXPECT_EQ(1, prompts.show_count());

  const std::optional<message_center::Notification> notification =
      ShowPageNotification(contents, "New mail");
  ASSERT_TRUE(notification);
  EXPECT_EQ(u"New mail", notification->title());
  EXPECT_EQ(PageNotificationContext(Site(), u"Work"),
            notification->context_message());
}

// A default tab is as upstream has it, and the answer is the site's: a tab of
// the same site in a Space's profile has it without a second prompt.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       DefaultTabStillNotifiesAndSharesTheAnswer) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  const tab_groups::TabGroupId work = CreateWorkSpace();
  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  ASSERT_NE(work, spaces->GetActiveSpace());

  content::WebContents* plain = OpenTestPage();
  ASSERT_TRUE(plain);
  ASSERT_FALSE(containers::ContainerOf(plain));
  permissions::MockPermissionPromptFactory prompts(
      permissions::PermissionRequestManager::FromWebContents(plain));
  EXPECT_EQ("granted", RequestPermission(plain, prompts));
  EXPECT_EQ(1, prompts.show_count());
  const std::optional<message_center::Notification> notification =
      ShowPageNotification(plain, "Hello");
  ASSERT_TRUE(notification);
  // Upstream's line: the site, which the platform adds itself.
  EXPECT_TRUE(notification->context_message().empty());

  spaces->SwitchTo(work);
  content::WebContents* in_work = OpenTestPage();
  ASSERT_TRUE(in_work);
  ASSERT_TRUE(containers::ContainerOf(in_work));
  EXPECT_EQ(
      "granted",
      content::EvalJs(in_work, "Notification.permission").ExtractString());
  EXPECT_TRUE(ShowPageNotification(in_work, "Hello from Work"));
}

// A click shows the notification's tab, in its Space.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       ClickShowsTheTabInItsSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  const tab_groups::TabGroupId work = CreateWorkSpace();
  content::WebContents* contents = OpenTestPage();
  ASSERT_TRUE(contents);
  permissions::MockPermissionPromptFactory prompts(
      permissions::PermissionRequestManager::FromWebContents(contents));
  ASSERT_EQ("granted", RequestPermission(contents, prompts));
  const std::optional<message_center::Notification> notification =
      ShowPageNotification(contents, "Meeting");
  ASSERT_TRUE(notification);

  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  ASSERT_NE(work, spaces->GetActiveSpace());
  ASSERT_NE(contents, browser()->GetTabStripModel()->GetActiveWebContents());

  notifications_->SimulateClick(NotificationHandler::Type::WEB_NON_PERSISTENT,
                                notification->id(), std::nullopt, std::nullopt);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return spaces->GetActiveSpace() == work &&
           browser()->GetTabStripModel()->GetActiveWebContents() == contents;
  }));
}

// Push stays refused in a Space's profile, with the permission granted: its
// messages would reach the site's default partition.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest, PushStaysRefused) {
  CreateWorkSpace();
  content::WebContents* contents = OpenTestPage();
  ASSERT_TRUE(contents);
  permissions::MockPermissionPromptFactory prompts(
      permissions::PermissionRequestManager::FromWebContents(contents));
  ASSERT_EQ("granted", RequestPermission(contents, prompts));

  const std::string push =
      content::EvalJs(contents, kSubscribePush).ExtractString();
  EXPECT_NE("subscribed", push);
  EXPECT_NE(std::string::npos, push.find("push service not available")) << push;
}

// A service worker in profile A shows a notification (#530); its click and
// close reach A's worker only, not the same site's worker in the default
// partition or in profile B, and each one's getNotifications() has its own.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       ServiceWorkerNotificationReachesItsProfileOnly) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  AllowWorkerSite();
  const tab_groups::TabGroupId plain_space = spaces->GetSpaces()[0].id;
  const tab_groups::TabGroupId work = CreateWorkSpace();
  spaces->CreateSpace(u"Home", u"", {}, AddProfile(u"Home Profile"));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 3));
  const tab_groups::TabGroupId home = *spaces->GetActiveSpace();

  content::WebContents* in_work = OpenWorkerSite(work);
  content::WebContents* plain = OpenWorkerSite(plain_space);
  content::WebContents* in_home = OpenWorkerSite(home);
  ASSERT_TRUE(in_work && plain && in_home);
  ASSERT_TRUE(containers::ContainerOf(in_work));
  ASSERT_FALSE(containers::ContainerOf(plain));
  ASSERT_TRUE(containers::ContainerOf(in_home));

  // The same tag in two partitions: two notifications, not one replacing
  // the other.
  const std::optional<message_center::Notification> from_work =
      ShowWorkerNotification(in_work, "From Work", "{tag: 'inbox'}");
  const std::optional<message_center::Notification> from_plain =
      ShowWorkerNotification(plain, "From Default", "{tag: 'inbox'}");
  ASSERT_TRUE(from_work && from_plain);
  EXPECT_EQ(2u, WorkerNotifications().size());
  // The id names A's partition (content's id format; see
  // koan_partition_domain.h); a default one's is upstream's.
  EXPECT_TRUE(base::StartsWith(from_work->id(), "pkoanuser"))
      << from_work->id();
  EXPECT_TRUE(base::StartsWith(from_plain->id(), "p#")) << from_plain->id();
  EXPECT_EQ(PageNotificationContext(
                url_formatter::FormatOriginForSecurityDisplay(
                    url::Origin::Create(WorkerPage()),
                    url_formatter::SchemeDisplay::OMIT_HTTP_AND_HTTPS),
                u"Work"),
            from_work->context_message());
  EXPECT_TRUE(from_plain->context_message().empty());

  EXPECT_EQ("From Work", content::EvalJs(in_work, "Titles()"));
  EXPECT_EQ("From Default", content::EvalJs(plain, "Titles()"));
  EXPECT_EQ("", content::EvalJs(in_home, "Titles()"));

  notifications_->SimulateClick(NotificationHandler::Type::WEB_PERSISTENT,
                                from_work->id(), std::nullopt, std::nullopt);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Received(in_work) == "click:From Work"; }));
  notifications_->RemoveNotification(NotificationHandler::Type::WEB_PERSISTENT,
                                     from_work->id(), /*by_user=*/true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Received(in_work) == "click:From Work,close:From Work"; }));
  // The default one still reaches the default worker, as upstream.
  notifications_->SimulateClick(NotificationHandler::Type::WEB_PERSISTENT,
                                from_plain->id(), std::nullopt, std::nullopt);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Received(plain) == "click:From Default"; }));

  EXPECT_EQ("click:From Work,close:From Work", Received(in_work));
  EXPECT_EQ("click:From Default", Received(plain));
  EXPECT_EQ("", Received(in_home));
}

// clients.openWindow() from a click on profile A's notification opens the tab
// in A, in A's Space, which shows; client.focus() shows the site's tab there.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       ServiceWorkerWindowsOpenInTheirSpace) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  AllowWorkerSite();
  const tab_groups::TabGroupId plain_space = spaces->GetSpaces()[0].id;
  const tab_groups::TabGroupId work = CreateWorkSpace();
  content::WebContents* in_work = OpenWorkerSite(work);
  ASSERT_TRUE(in_work);
  const GURL opened = embedded_test_server()->GetURL("/sw_page.html?opened");

  const std::optional<message_center::Notification> open_one =
      ShowWorkerNotification(in_work, "Open",
                             "{data: {open: '" + opened.spec() + "'}}");
  ASSERT_TRUE(open_one);
  spaces->SwitchTo(plain_space);
  notifications_->SimulateClick(NotificationHandler::Type::WEB_PERSISTENT,
                                open_one->id(), std::nullopt, std::nullopt);
  content::WebContents* new_tab = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    TabStripModel* strip = browser()->GetTabStripModel();
    for (int i = 0; i < strip->count(); ++i) {
      if (strip->GetWebContentsAt(i)->GetVisibleURL() == opened) {
        new_tab = strip->GetWebContentsAt(i);
      }
    }
    return new_tab != nullptr;
  }));
  EXPECT_EQ(containers::ContainerOf(in_work), containers::ContainerOf(new_tab));
  EXPECT_EQ(work,
            browser()->GetTabStripModel()->GetTabGroupForTab(
                browser()->GetTabStripModel()->GetIndexOfWebContents(new_tab)));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return spaces->GetActiveSpace() == work &&
           browser()->GetTabStripModel()->GetActiveWebContents() == new_tab;
  }));

  const std::optional<message_center::Notification> focus_one =
      ShowWorkerNotification(in_work, "Focus", "{data: {focus: true}}");
  ASSERT_TRUE(focus_one);
  spaces->SwitchTo(plain_space);
  ASSERT_NE(work, spaces->GetActiveSpace());
  notifications_->SimulateClick(NotificationHandler::Type::WEB_PERSISTENT,
                                focus_one->id(), std::nullopt, std::nullopt);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return spaces->GetActiveSpace() == work &&
           containers::ContainerOf(
               browser()->GetTabStripModel()->GetActiveWebContents()) ==
               containers::ContainerOf(in_work);
  }));
}

// A click on a notification whose profile is gone finds nothing, and upstream
// then closes it (PersistentNotificationHandler::OnClickCompleted()).
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       ClickForAGoneProfileFindsNothing) {
  base::test::TestFuture<content::PersistentNotificationStatus> status;
  content::NotificationEventDispatcher::GetInstance()
      ->DispatchNotificationClickEvent(
          profile(),
          "pkoanusersgoneprofile#" +
              url::Origin::Create(WorkerPage()).Serialize() + "#010001",
          url::Origin::Create(WorkerPage()).GetURL(), std::nullopt,
          std::nullopt, status.GetCallback());
  EXPECT_EQ(content::PersistentNotificationStatus::kDatabaseError,
            status.Get());
}

// The site panel says so on a page in a Space's profile, unless the site is
// blocked from notifying; never on a default tab.
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       SitePanelNoteInSpaceProfileOnly) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  CreateWorkSpace();
  content::WebContents* in_work = OpenTestPage();
  ASSERT_TRUE(in_work);
  EXPECT_TRUE(ShowsProfileNotificationNote(in_work));

  HostContentSettingsMapFactory::GetForProfile(profile())
      ->SetContentSettingDefaultScope(TestPage(), GURL(),
                                      ContentSettingsType::NOTIFICATIONS,
                                      CONTENT_SETTING_BLOCK);
  EXPECT_FALSE(ShowsProfileNotificationNote(in_work));

  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  content::WebContents* plain = OpenTestPage();
  ASSERT_TRUE(plain);
  EXPECT_FALSE(ShowsProfileNotificationNote(plain));
}

}  // namespace koan
