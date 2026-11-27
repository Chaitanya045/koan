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
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
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
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_color.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/browsing_data_remover_test_util.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_space_profile_menu.h"
#include "koan/browser/ui/views/koan_space_profiles.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/cookies/canonical_cookie.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
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

// Forget About This Site forgets the site in every profile, and only it.
IN_PROC_BROWSER_TEST_F(KoanSpaceProfilesBrowserTest,
                       ForgetAboutThisSiteForgetsItInEveryProfile) {
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

  // What the Library's Forget About This Site asks the remover for.
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

}  // namespace koan
