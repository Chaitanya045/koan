// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Notifications in a Space's profile (#478,
// .agent/notes/upstream/space-containers.md, "Notifications"): a page there
// asks for the permission and shows its notifications like a default tab,
// names its Space in them, and a click shows its tab; service-worker
// notifications and push stay refused there.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/test/run_until.h"
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
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_page_notifications.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
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

class KoanSpaceNotificationsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
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

  // Opens the test page in a new tab of the active Space, like Cmd+T and the
  // URL bar.
  content::WebContents* OpenTestPage() {
    NavigateParams params(browser(), TestPage(), ui::PAGE_TRANSITION_TYPED);
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

// What stays refused in a Space's profile, with the permission granted:
// service-worker notifications (their clicks would reach the site's default
// partition) and push (its messages would).
IN_PROC_BROWSER_TEST_F(KoanSpaceNotificationsBrowserTest,
                       ServiceWorkerNotificationsAndPushStayRefused) {
  CreateWorkSpace();
  content::WebContents* contents = OpenTestPage();
  ASSERT_TRUE(contents);
  permissions::MockPermissionPromptFactory prompts(
      permissions::PermissionRequestManager::FromWebContents(contents));
  ASSERT_EQ("granted", RequestPermission(contents, prompts));

  EXPECT_NE("ok",
            content::EvalJs(contents, "DisplayPersistentNotification('x')")
                .ExtractString());
  EXPECT_TRUE(notifications_
                  ->GetDisplayedNotificationsForType(
                      NotificationHandler::Type::WEB_PERSISTENT)
                  .empty());

  const std::string push =
      content::EvalJs(contents, kSubscribePush).ExtractString();
  EXPECT_NE("subscribed", push);
  EXPECT_NE(std::string::npos, push.find("push service not available")) << push;
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
