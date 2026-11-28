// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Settings > Tab Management > Profiles (#477,
// .agent/notes/upstream/space-containers.md "Managing profiles"): a rename or
// a new colour reaches every window; Delete profile... resets its Spaces in
// every window, closes its tabs and deletes its data, and nothing else; Clear
// Data clears only that profile; Clear browsing data and Forget About This
// Site clear the profiles too; a deleted profile never comes back, from
// Recently Closed or after a restart.

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_load_waiter.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/test/test_extension_dir.h"
#include "koan/browser/containers/koan_container_extensions.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_container_proxy.h"
#include "koan/browser/containers/koan_history_profiles.h"
#include "koan/browser/containers/koan_partition_domain.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_tab_auto_close.h"
#include "koan/browser/ui/views/koan_space_profile_menu.h"
#include "koan/browser/ui/views/koan_space_profiles.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/cookies/canonical_cookie.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace koan {

namespace {

class KoanSpaceProfilesBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }

  // A new profile (New Profile...), Serialize()d.
  std::string AddProfile(const std::u16string& name) {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), name);
    return container ? containers::Serialize(container->id) : std::string();
  }

  static containers::ContainerId IdOf(const std::string& container) {
    return *containers::SpaceContainerFromString(container);
  }

  // Makes a Space with `container` in `browser`'s window and opens `url` in
  // it, in the Space's partition.
  content::WebContents* OpenInNewSpace(BrowserWindowInterface* browser,
                                       const std::u16string& name,
                                       const std::string& container,
                                       const GURL& url) {
    KoanWorkspaceController* spaces = GetSpaces(browser);
    const size_t count = spaces->GetSpaces().size();
    spaces->CreateSpace(name, u"", {}, container);
    if (!WaitForSpaceCount(browser, count + 1)) {
      return nullptr;
    }
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents;
  }

  // Logs in: a cookie named `name` for the tab's site.
  void SetCookie(content::WebContents* contents, const std::string& name) {
    ASSERT_TRUE(content::ExecJs(
        contents, "document.cookie = '" + name + "=1; max-age=3600';"));
    EXPECT_EQ(name + "=1", content::EvalJs(contents, "document.cookie"));
  }

  // The names of the cookies in `container`'s partition (none: the default),
  // sorted; nullopt if its partition isn't loaded (and never loads it then).
  std::optional<std::vector<std::string>> CookieNames(
      const std::string& container) {
    content::StoragePartition* partition =
        container.empty()
            ? profile()->GetDefaultStoragePartition()
            : profile()->GetStoragePartition(
                  containers::PartitionConfigFor(profile(), IdOf(container)),
                  /*can_create=*/false);
    if (!partition) {
      return std::nullopt;
    }
    base::test::TestFuture<const std::vector<net::CanonicalCookie>&> cookies;
    partition->GetCookieManagerForBrowserProcess()->GetAllCookies(
        cookies.GetCallback());
    std::vector<std::string> names;
    for (const net::CanonicalCookie& cookie : cookies.Get()) {
      names.push_back(cookie.Name());
    }
    std::ranges::sort(names);
    return names;
  }

  // The tabs in `container` (none: the default), in every window, the
  // Spaces' empty tabs too.
  int TabsIn(const std::string& container) {
    int count = 0;
    for (BrowserWindowInterface* window : {browser(), second_.get()}) {
      if (!window) {
        continue;
      }
      TabStripModel* tab_strip = window->GetTabStripModel();
      for (int i = 0; i < tab_strip->count(); ++i) {
        count += containers::ContainerOf(tab_strip->GetWebContentsAt(i)) ==
                 containers::SpaceContainerFromString(container);
      }
    }
    return count;
  }

  // The Spaces with `container`, in every window.
  int SpacesWith(const std::string& container) {
    int count = 0;
    for (BrowserWindowInterface* window : {browser(), second_.get()}) {
      KoanWorkspaceController* spaces = window ? GetSpaces(window) : nullptr;
      for (const auto& space :
           spaces ? spaces->GetSpaces()
                  : std::vector<KoanWorkspaceController::SpaceInfo>()) {
        count += space.container == container;
      }
    }
    return count;
  }

  void RemoveBrowsingData(
      uint64_t mask,
      std::unique_ptr<content::BrowsingDataFilterBuilder> filter = nullptr) {
    content::BrowsingDataRemover* remover = profile()->GetBrowsingDataRemover();
    content::BrowsingDataRemoverCompletionObserver observer(remover);
    if (!filter) {
      filter = content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kPreserve);
    }
    remover->RemoveWithFilterAndReply(
        base::Time(), base::Time::Max(), mask,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
        std::move(filter), &observer);
    observer.BlockUntilCompletion();
  }

  GURL Page(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/title1.html");
  }

  history::HistoryService* history() {
    return HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::EXPLICIT_ACCESS);
  }

  // The pages with visits in the profile `container` (none: the default).
  std::set<GURL> Pages(const std::string& container) {
    history::QueryOptions options;
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    options.app_id = containers::HistoryAppIdForProfile(container);
    base::test::TestFuture<history::QueryResults> results;
    base::CancelableTaskTracker tracker;
    history()->QueryHistory(u"", options, results.GetCallback(), &tracker);
    std::set<GURL> pages;
    for (const history::URLResult& result : results.Take()) {
      pages.insert(result.url());
    }
    return pages;
  }

  // A visit to `url` in the profile `container`, as a tab of it records one.
  void AddVisit(const GURL& url, const std::string& container) {
    history::HistoryAddPageArgs args;
    args.url = url;
    args.time = base::Time::Now();
    args.redirects = {url};
    args.transition = ui::PAGE_TRANSITION_TYPED;
    args.app_id = containers::HistoryAppIdForProfile(container);
    history()->AddPage(args);
  }

  // A tab "Close tabs I haven't used for" closed, of the profile `container`.
  void AddAutoClosedTab(const GURL& url, const std::string& container) {
    auto_close::ClosedTab tab;
    tab.id = container.empty() ? "plain" : container;
    tab.url = url;
    tab.closed = base::Time::Now();
    if (!container.empty()) {
      tab.data[containers::kContainerSessionKey] = container;
    }
    PrefService* prefs = profile()->GetPrefs();
    prefs->SetList(
        prefs::kAutoClosedTabs,
        auto_close::AddClosedTab(prefs->GetList(prefs::kAutoClosedTabs), tab,
                                 base::Time::Now()));
  }

  // The profiles of the closed tabs "Close tabs I haven't used for" keeps.
  std::multiset<std::string> AutoClosedTabProfiles() {
    std::multiset<std::string> profiles;
    for (const base::Value& entry :
         profile()->GetPrefs()->GetList(prefs::kAutoClosedTabs)) {
      if (std::optional<auto_close::ClosedTab> tab =
              auto_close::ClosedTab::FromValue(entry)) {
        const auto it = tab->data.find(containers::kContainerSessionKey);
        profiles.insert(it == tab->data.end() ? std::string() : it->second);
      }
    }
    return profiles;
  }

  // prefs::kDeletedContainersHistory.
  std::vector<std::string> DeletedHistory() {
    std::vector<std::string> ids;
    for (const base::Value& id :
         profile()->GetPrefs()->GetList(prefs::kDeletedContainersHistory)) {
      ids.push_back(id.is_string() ? id.GetString() : std::string());
    }
    return ids;
  }

  // A second window, for "every window".
  raw_ptr<BrowserWindowInterface> second_ = nullptr;
};

}  // namespace

// Rename and Colour reach every window: the Set Profile menu, the colour
// line's colour, the profile's name, and Settings' list.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       RenameAndRecolourReachEveryWindow) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  second_ = CreateBrowser(profile());
  ASSERT_TRUE(GetSpaces(second_));
  ASSERT_TRUE(OpenInNewSpace(second_, u"Work", work, Page("a.test")));

  EXPECT_TRUE(RenameSpaceProfile(profile(), work, u"Day job"));
  EXPECT_TRUE(SetSpaceProfileColor(
      profile(), work, static_cast<int>(tab_groups::TabGroupColorId::kRed)));
  for (BrowserWindowInterface* window : {browser(), second_.get()}) {
    KoanSpaceProfileMenu menu(window, work, base::DoNothing());
    bool listed = false;
    for (size_t i = 0; i < menu.model()->GetItemCount(); ++i) {
      listed |= menu.model()->GetLabelAt(i) == u"Day job";
    }
    EXPECT_TRUE(listed);
    EXPECT_EQ(u"Day job", SpaceProfileName(window->GetProfile(), work));
    EXPECT_EQ(GetTabGroupTabStripColorId(tab_groups::TabGroupColorId::kRed,
                                         /*active_frame=*/true),
              ContainerLineColorId(window->GetProfile(), IdOf(work),
                                   /*active_frame=*/true));
  }
  const std::vector<SpaceProfileInfo> listed = GetSpaceProfiles(profile());
  ASSERT_EQ(1u, listed.size());
  EXPECT_EQ("Day job", listed[0].name);
  EXPECT_EQ(static_cast<int>(tab_groups::TabGroupColorId::kRed),
            listed[0].color);
  EXPECT_EQ(std::vector<std::u16string>{u"Work"}, listed[0].spaces);
  EXPECT_EQ(1, listed[0].open_tabs);

  // Refused: an empty name, a colour profiles don't have, a profile that
  // isn't one.
  EXPECT_FALSE(RenameSpaceProfile(profile(), work, u"  "));
  EXPECT_FALSE(SetSpaceProfileColor(
      profile(), work, static_cast<int>(tab_groups::TabGroupColorId::kGrey)));
  EXPECT_FALSE(RenameSpaceProfile(profile(), "user:snothere", u"X"));
  EXPECT_EQ(u"Day job", SpaceProfileName(profile(), work));
}

// What else is kept with a profile shows in its row in Settings: its proxy
// ("Direct" without one) and the extensions that are off in it, by name (an
// extension that is no longer installed is not one), and each follows a
// change.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       RowShowsProxyAndExtensions) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  extensions::TestExtensionDir dir;
  dir.WriteManifest(R"({"name": "Koan row test", "version": "1",
                        "manifest_version": 3})");
  scoped_refptr<const extensions::Extension> extension =
      extensions::ChromeTestExtensionLoader(profile()).LoadExtension(
          dir.UnpackedPath());
  ASSERT_TRUE(extension);

  const auto info = [&](const std::string& id) {
    for (const SpaceProfileInfo& listed : GetSpaceProfiles(profile())) {
      if (listed.id == id) {
        return listed;
      }
    }
    ADD_FAILURE() << id << " isn't listed";
    return SpaceProfileInfo();
  };
  EXPECT_EQ("", info(work).proxy);
  EXPECT_FALSE(info(work).proxy_disconnected);
  EXPECT_TRUE(info(work).extensions_off.empty());

  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    ASSERT_TRUE(containers::SetUserContainerProxy(
        update.Get(), IdOf(work),
        containers::ProxySetting{.server = "socks5://127.0.0.1:1080"}));
    // Off, and an id of an extension that isn't installed.
    ASSERT_TRUE(containers::SetExtensionsOff(
        update.Get(), IdOf(work), {extension->id(), std::string(32, 'a')}));
  }
  const SpaceProfileInfo with = info(work);
  EXPECT_EQ("socks5://127.0.0.1:1080", with.proxy);
  EXPECT_FALSE(with.proxy_disconnected);
  ASSERT_EQ(1u, with.extensions_off.size());
  EXPECT_EQ(extension->id(), with.extensions_off[0].id);
  EXPECT_EQ(u"Koan row test", with.extensions_off[0].name);
  // The other profile has neither.
  EXPECT_EQ("", info(home).proxy);
  EXPECT_TRUE(info(home).extensions_off.empty());

  // The list is edited for a Space: no Space uses Home, so nothing opens.
  EXPECT_FALSE(ShowSpaceProfileExtensions(profile(), nullptr, home));
  EXPECT_FALSE(ShowSpaceProfileExtensions(profile(), nullptr, "user:snothere"));

  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    ASSERT_TRUE(containers::SetUserContainerProxy(update.Get(), IdOf(work),
                                                  std::nullopt));
  }
  EXPECT_EQ("", info(work).proxy);
  // Uninstalled: no longer listed as off.
  ASSERT_TRUE(
      extensions::ExtensionRegistrar::Get(profile())->UninstallExtension(
          extension->id(), extensions::UNINSTALL_REASON_FOR_TESTING, nullptr));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return info(work).extensions_off.empty(); }));
}

// Delete profile...: its Spaces go back to Default in every window (and stay),
// its tabs close, its cookies are gone; another profile's and the default
// partition's are kept, and new tabs in its old Spaces open in the default.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       DeleteResetsSpacesAndRemovesOnlyItsData) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  second_ = CreateBrowser(profile());
  ASSERT_TRUE(GetSpaces(second_));

  content::WebContents* in_default =
      OpenInNewSpace(browser(), u"None", "", Page("a.test"));
  SetCookie(in_default, "plain");
  SetCookie(OpenInNewSpace(browser(), u"Work", work, Page("a.test")), "work");
  SetCookie(OpenInNewSpace(second_, u"Work too", work, Page("b.test")),
            "work2");
  SetCookie(OpenInNewSpace(browser(), u"Home", home, Page("a.test")), "home");
  ASSERT_EQ((std::vector<std::string>{"work", "work2"}), *CookieNames(work));
  ASSERT_GE(SpacesWith(work), 2);
  ASSERT_GE(TabsIn(work), 2);
  const int default_tabs = TabsIn("");

  base::test::TestFuture<void> deleted;
  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, deleted.GetCallback()));
  ASSERT_TRUE(deleted.Wait());

  // Out of the list; its Spaces are Default and still there.
  EXPECT_EQ(std::vector<std::string>{home}, [&] {
    std::vector<std::string> ids;
    for (const SpaceProfileInfo& info : GetSpaceProfiles(profile())) {
      ids.push_back(info.id);
    }
    return ids;
  }());
  EXPECT_EQ(0, SpacesWith(work));
  EXPECT_TRUE(std::ranges::contains(SpaceNames(browser()), u"Work"));
  EXPECT_TRUE(std::ranges::contains(SpaceNames(second_), u"Work too"));
  EXPECT_GE(SpacesWith(home), 1);
  // Its tabs closed (each emptied Space got its empty tab back, in the
  // default partition); the others' are open.
  EXPECT_TRUE(base::test::RunUntil([&] { return TabsIn(work) == 0; }));
  EXPECT_GE(TabsIn(""), default_tabs);
  EXPECT_GE(TabsIn(home), 1);
  // Its data is gone; the others' is kept.
  if (std::optional<std::vector<std::string>> left = CookieNames(work)) {
    EXPECT_TRUE(left->empty());
  }
  EXPECT_EQ(std::vector<std::string>{"home"}, *CookieNames(home));
  EXPECT_EQ(std::vector<std::string>{"plain"}, *CookieNames(""));
  // The folder, still loaded, goes at the next start.
  EXPECT_TRUE(profile()->GetPrefs()->GetBoolean(
      ::prefs::kShouldGarbageCollectStoragePartitions));
  // A new tab in its old Space opens in the default partition.
  KoanWorkspaceController* spaces = GetSpaces(browser());
  for (const auto& space : spaces->GetSpaces()) {
    if (space.name == u"Work") {
      spaces->SwitchTo(space.id);
    }
  }
  NavigateParams params(browser(), Page("a.test"), ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::kNewForegroundTab;
  ui_test_utils::NavigateToURL(&params);
  EXPECT_EQ(std::nullopt,
            containers::ContainerOf(params.navigated_or_inserted_contents));
  // A profile the list doesn't have: nothing to delete.
  EXPECT_FALSE(DeleteSpaceProfile(profile(), work, base::DoNothing()));
}

// A tab of a deleted profile in Recently Closed doesn't bring it back: it
// restores into nothing and closes.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       RecentlyClosedTabsDontBringItBack) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  content::WebContents* tab =
      OpenInNewSpace(browser(), u"Work", work, Page("a.test"));
  ASSERT_TRUE(tab);
  TabRestoreServiceLoadWaiter waiter(
      TabRestoreServiceFactory::GetForProfile(profile()));
  waiter.Wait();
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tab_strip->CloseWebContentsAt(tab_strip->GetIndexOfWebContents(tab),
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);

  base::test::TestFuture<void> deleted;
  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, deleted.GetCallback()));
  ASSERT_TRUE(deleted.Wait());
  // (Its Space's empty tab closed too, and came back in the default.)
  ASSERT_TRUE(base::test::RunUntil([&] { return TabsIn(work) == 0; }));
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const int count = tab_strip->count();
  chrome::RestoreTab(browser());
  EXPECT_TRUE(
      base::test::RunUntil([&] { return tab_strip->count() == count; }));
  EXPECT_EQ(0, TabsIn(work));
  EXPECT_FALSE(containers::SiteInstanceForRestoredTab(
      profile(), {{containers::kContainerSessionKey, work}}));
}

// Clear Data signs out of that profile only, and keeps it and its tabs.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest, ClearDataClearsOnlyIt) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  SetCookie(OpenInNewSpace(browser(), u"None", "", Page("a.test")), "plain");
  content::WebContents* in_work =
      OpenInNewSpace(browser(), u"Work", work, Page("a.test"));
  SetCookie(in_work, "work");
  ASSERT_TRUE(content::ExecJs(in_work, "localStorage.setItem('k', 'v');"));
  SetCookie(OpenInNewSpace(browser(), u"Home", home, Page("a.test")), "home");

  base::test::TestFuture<void> cleared;
  ASSERT_TRUE(ClearSpaceProfileData(profile(), work, cleared.GetCallback()));
  ASSERT_TRUE(cleared.Wait());
  EXPECT_TRUE(CookieNames(work)->empty());
  EXPECT_EQ(std::vector<std::string>{"home"}, *CookieNames(home));
  EXPECT_EQ(std::vector<std::string>{"plain"}, *CookieNames(""));
  EXPECT_GE(TabsIn(work), 1);
  EXPECT_EQ(2u, GetSpaceProfiles(profile()).size());
  // Its storage is gone too, after a reload.
  content::NavigationController& controller = in_work->GetController();
  controller.Reload(content::ReloadType::NORMAL, false);
  ASSERT_TRUE(content::WaitForLoadStop(in_work));
  EXPECT_EQ(true,
            content::EvalJs(in_work, "localStorage.getItem('k') === null"));
  EXPECT_EQ("", content::EvalJs(in_work, "document.cookie"));
}

// Clear browsing data (cookies and site data, all time) signs out of every
// profile too.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       ClearBrowsingDataClearsProfiles) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  SetCookie(OpenInNewSpace(browser(), u"None", "", Page("a.test")), "plain");
  SetCookie(OpenInNewSpace(browser(), u"Work", work, Page("a.test")), "work");
  SetCookie(OpenInNewSpace(browser(), u"Home", home, Page("b.test")), "home");

  RemoveBrowsingData(chrome_browsing_data_remover::DATA_TYPE_SITE_DATA);
  EXPECT_TRUE(CookieNames("")->empty());
  // (The profiles' removals run after the one that asked for them.)
  EXPECT_TRUE(base::test::RunUntil([&] {
    return CookieNames(work)->empty() && CookieNames(home)->empty();
  }));
}

// A removal for one site that names no partition (a filtered Clear browsing
// data, the browsingData extension API) reaches every profile, for that site
// only. (The Library's Forget About This Site names each partition itself,
// #474, so with one profile shown it stays in that one.)
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       SiteRemovalReachesEveryProfile) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  SetCookie(OpenInNewSpace(browser(), u"Work", work, Page("a.test")), "a");
  NavigateParams params(browser(), Page("b.test"), ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::kNewForegroundTab;
  ui_test_utils::NavigateToURL(&params);
  ASSERT_EQ(containers::SpaceContainerFromString(work),
            containers::ContainerOf(params.navigated_or_inserted_contents));
  SetCookie(params.navigated_or_inserted_contents, "b");
  ASSERT_EQ((std::vector<std::string>{"a", "b"}), *CookieNames(work));

  auto filter = content::BrowsingDataFilterBuilder::Create(
      content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter->AddRegisterableDomain("a.test");
  RemoveBrowsingData(chrome_browsing_data_remover::DATA_TYPE_SITE_DATA |
                         content::BrowsingDataRemover::DATA_TYPE_CACHE,
                     std::move(filter));
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return *CookieNames(work) == std::vector<std::string>{"b"}; }));
}

// After a restart, nothing of a deleted profile is left on disk: its folder,
// in use when it was deleted, goes with Chromium's storage partition garbage
// collection, which nothing keeps it from.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       PRE_DeletedProfilesFolderGoes) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  SetCookie(OpenInNewSpace(browser(), u"Work", work, Page("a.test")), "work");
  base::test::TestFuture<void> deleted;
  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, deleted.GetCallback()));
  ASSERT_TRUE(deleted.Wait());
  base::RunLoop run_loop;
  profile()->GetPrefs()->CommitPendingWrite(run_loop.QuitClosure());
  run_loop.Run();
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       DeletedProfilesFolderGoes) {
  EXPECT_TRUE(GetSpaceProfiles(profile()).empty());
  const auto koan_user_folders = [&] {
    base::ScopedAllowBlockingForTesting allow_blocking;
    int count = 0;
    base::FileEnumerator folders(
        profile()->GetPath().Append(FILE_PATH_LITERAL("Storage")),
        /*recursive=*/true, base::FileEnumerator::DIRECTORIES);
    for (base::FilePath path = folders.Next(); !path.empty();
         path = folders.Next()) {
      count += path.BaseName().MaybeAsASCII().starts_with(
          containers::kUserPartitionDomainPrefix);
    }
    return count;
  };
  EXPECT_TRUE(base::test::RunUntil([&] { return koan_user_folders() == 0; }));
  // And the Space that used it is Default.
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  for (const auto& space : spaces->GetSpaces()) {
    EXPECT_EQ("", space.container);
  }
}

// Deleting a profile forgets its history (#474) and the tabs "Close tabs I
// haven't used for" kept (#482), and its extension list (#473) goes with its
// entry. Another profile's and the default profile's are kept.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       DeleteForgetsItsHistoryClosedTabsAndExtensions) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ui_test_utils::WaitForHistoryToLoad(history());
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  // Visits in each, the same page in two.
  ASSERT_TRUE(OpenInNewSpace(browser(), u"None", "", Page("a.test")));
  ASSERT_TRUE(OpenInNewSpace(browser(), u"Work", work, Page("w.test")));
  ASSERT_TRUE(OpenInNewSpace(browser(), u"Home", home, Page("h.test")));
  AddVisit(Page("a.test"), work);
  AddVisit(Page("a.test"), home);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Pages(work) == std::set<GURL>{Page("a.test"), Page("w.test")} &&
           Pages(home) == std::set<GURL>{Page("a.test"), Page("h.test")} &&
           Pages("").contains(Page("a.test"));
  }));
  AddAutoClosedTab(Page("w.test"), work);
  AddAutoClosedTab(Page("h.test"), home);
  AddAutoClosedTab(Page("a.test"), "");
  const std::string extension(32, 'a');
  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    ASSERT_TRUE(
        containers::SetExtensionsOff(update.Get(), IdOf(work), {extension}));
    ASSERT_TRUE(
        containers::SetExtensionsOff(update.Get(), IdOf(home), {extension}));
  }

  base::test::TestFuture<void> deleted;
  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, deleted.GetCallback()));
  ASSERT_TRUE(deleted.Wait());

  EXPECT_TRUE(base::test::RunUntil([&] { return Pages(work).empty(); }));
  EXPECT_EQ((std::set<GURL>{Page("a.test"), Page("h.test")}), Pages(home));
  EXPECT_TRUE(Pages("").contains(Page("a.test")));
  EXPECT_EQ((std::multiset<std::string>{"", home}), AutoClosedTabProfiles());
  const base::ListValue& listed =
      profile()->GetPrefs()->GetList(prefs::kUserContainers);
  EXPECT_TRUE(containers::ExtensionsOffIn(listed, IdOf(work)).empty());
  EXPECT_EQ(std::set<std::string>{extension},
            containers::ExtensionsOffIn(listed, IdOf(home)));
  ASSERT_EQ(1u, listed.size());
  // Kept for the next start, which checks that the visits are gone.
  EXPECT_EQ(std::vector<std::string>{work}, DeletedHistory());
}

// A Space's empty tab is in Default as soon as its profile is deleted (#561),
// not with the next update. The question names the profile's Essentials
// (#510), which close with it.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       DeleteMovesTheEmptyTabAtOnceAndNamesEssentials) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  content::WebContents* tab =
      OpenInNewSpace(browser(), u"Work", work, Page("a.test"));
  ASSERT_TRUE(tab);
  ASSERT_TRUE(OpenInNewSpace(browser(), u"Work 2", work, Page("b.test")));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  tab_strip->SetTabPinned(tab_strip->GetIndexOfWebContents(tab), true);
  std::vector<SpaceProfileInfo> listed = GetSpaceProfiles(profile());
  ASSERT_EQ(1u, listed.size());
  EXPECT_EQ(2, listed[0].open_tabs);
  EXPECT_EQ(1, listed[0].essentials);
  EXPECT_EQ(0, listed[0].pinned_tabs);
  // A second window has the Essential too (window sync's copy): still one.
  second_ = CreateBrowser(profile());
  ASSERT_TRUE(GetSpaces(second_));
  ASSERT_TRUE(base::test::RunUntil([&] {
    TabStripModel* other = second_->GetTabStripModel();
    return other->IndexOfFirstNonPinnedTab() > 0;
  }));
  listed = GetSpaceProfiles(profile());
  EXPECT_EQ(2, listed[0].open_tabs);
  EXPECT_EQ(1, listed[0].essentials);

  // The empty tabs of the Spaces that use it.
  const auto empty_tab_containers = [&] {
    std::vector<std::optional<containers::ContainerId>> found;
    KoanWorkspaceController* spaces = GetSpaces(browser());
    for (const auto& space : spaces->GetSpaces()) {
      if (!space.name.starts_with(u"Work")) {
        continue;
      }
      for (int i = 0; i < tab_strip->count(); ++i) {
        tabs::TabInterface* each = tab_strip->GetTabAtIndex(i);
        if (each->GetGroup() == space.id && IsEmptyTab(each)) {
          found.push_back(containers::ContainerOf(each->GetContents()));
        }
      }
    }
    return found;
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto found = empty_tab_containers();
    return !found.empty() && std::ranges::all_of(found, [&](const auto& c) {
      return c == IdOf(work);
    });
  }));
  const size_t empty_tabs = empty_tab_containers().size();
  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, base::DoNothing()));
  // At once: nothing ran between.
  EXPECT_EQ(std::vector<std::optional<containers::ContainerId>>(empty_tabs),
            empty_tab_containers());
  EXPECT_TRUE(base::test::RunUntil([&] { return TabsIn(work) == 0; }));
}

// The notifications a deleted profile shows close; another profile's and the
// default profile's stay.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       DeleteClosesItsNotifications) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  NotificationDisplayServiceTester notifications(profile());
  const std::string work = AddProfile(u"Work");
  const std::string home = AddProfile(u"Home");
  // Service workers' notifications: the id names the partition.
  const auto show = [&](const std::string& container) {
    const std::string domain =
        container.empty()
            ? std::string()
            : containers::PartitionConfigFor(profile(), IdOf(container))
                  .partition_domain();
    const GURL origin = Page("a.test").DeprecatedGetOriginAsURL();
    const std::string id = containers::TagPersistentNotificationId(
        "p#" + origin.spec() + "#0" + container, domain);
    NotificationDisplayServiceFactory::GetForProfile(profile())->Display(
        NotificationHandler::Type::WEB_PERSISTENT,
        message_center::Notification(
            message_center::NOTIFICATION_TYPE_SIMPLE, id, u"Hi", u"",
            ui::ImageModel(), u"", origin, message_center::NotifierId(origin),
            message_center::RichNotificationData(), nullptr),
        /*metadata=*/nullptr);
    return id;
  };
  const std::string in_work = show(work);
  const std::string in_home = show(home);
  const std::string in_default = show("");
  // A page's own, from a tab of the profile.
  HostContentSettingsMapFactory::GetForProfile(profile())
      ->SetContentSettingDefaultScope(Page("b.test"), GURL(),
                                      ContentSettingsType::NOTIFICATIONS,
                                      CONTENT_SETTING_ALLOW);
  content::WebContents* page =
      OpenInNewSpace(browser(), u"Work", work, Page("b.test"));
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::ExecJs(page, "new Notification('Page')"));
  const auto shown = [&](NotificationHandler::Type type) {
    std::set<std::string> ids;
    for (const message_center::Notification& notification :
         notifications.GetDisplayedNotificationsForType(type)) {
      ids.insert(notification.id());
    }
    return ids;
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    return shown(NotificationHandler::Type::WEB_NON_PERSISTENT).size() == 1;
  }));
  ASSERT_EQ((std::set<std::string>{in_work, in_home, in_default}),
            shown(NotificationHandler::Type::WEB_PERSISTENT));

  ASSERT_TRUE(DeleteSpaceProfile(profile(), work, base::DoNothing()));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return shown(NotificationHandler::Type::WEB_PERSISTENT) ==
               std::set<std::string>{in_home, in_default} &&
           shown(NotificationHandler::Type::WEB_NON_PERSISTENT).empty();
  }));
}

// Visits a crash kept from a deletion go at the next start; the profile stays
// marked until a start finds none (the history database writes some seconds
// after a removal). A listed profile's history is never forgotten that way.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       PRE_PRE_CrashKeptVisitsGoAtTheNextStart) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  ui_test_utils::WaitForHistoryToLoad(history());
  const std::string ghost = AddProfile(u"Ghost");
  const std::string home = AddProfile(u"Home");
  AddVisit(Page("g.test"), ghost);
  AddVisit(Page("h.test"), home);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !Pages(ghost).empty() && !Pages(home).empty(); }));
  // As a crash leaves it: out of the list, marked, its visits still there.
  {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    ASSERT_TRUE(containers::RemoveUserContainer(update.Get(), IdOf(ghost)));
  }
  base::ListValue marked;
  marked.Append(ghost);
  marked.Append(home);
  profile()->GetPrefs()->SetList(prefs::kDeletedContainersHistory,
                                 std::move(marked));
}

IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       PRE_CrashKeptVisitsGoAtTheNextStart) {
  ui_test_utils::WaitForHistoryToLoad(history());
  const std::string home = GetSpaceProfiles(profile()).at(0).id;
  // Home is listed: unmarked at once, its history kept.
  const std::vector<std::string> marked = DeletedHistory();
  ASSERT_EQ(1u, marked.size());
  ASSERT_NE(home, marked[0]);
  const std::string ghost = marked[0];
  EXPECT_TRUE(base::test::RunUntil([&] { return Pages(ghost).empty(); }));
  EXPECT_EQ(std::set<GURL>{Page("h.test")}, Pages(home));
  // Its visits were there this time: still marked.
  EXPECT_EQ(std::vector<std::string>{ghost}, DeletedHistory());
}

IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       CrashKeptVisitsGoAtTheNextStart) {
  ui_test_utils::WaitForHistoryToLoad(history());
  EXPECT_TRUE(base::test::RunUntil([&] { return DeletedHistory().empty(); }));
  const std::string home = GetSpaceProfiles(profile()).at(0).id;
  EXPECT_EQ(std::set<GURL>{Page("h.test")}, Pages(home));
}

}  // namespace koan
