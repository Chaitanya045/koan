// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Zen's containers per Space (#318, .agent/notes/upstream/space-containers.md):
// a Space's profile is stored with it, its new tabs and its empty tab open in
// its container, a page's tabs keep the page's partition, moved tabs keep
// theirs, logins stay apart, and all of it comes back after a restart.

#include <optional>
#include <string>
#include <vector>

#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "net/cookies/canonical_cookie.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Pages that aren't blank, so never taken for an empty tab.
constexpr char kPage[] = "data:text/html,<title>Page</title>";
constexpr char kOtherPage[] = "data:text/html,<title>Other</title>";

class KoanSpaceContainersBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
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

  // Opens `url` in a new foreground tab: like Cmd+T or the URL bar without a
  // `source`, like a link's Cmd-click from `source` with one.
  content::WebContents* OpenNewTab(const GURL& url,
                                   content::WebContents* source = nullptr) {
    NavigateParams params(
        browser(), url,
        source ? ui::PAGE_TRANSITION_LINK : ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    if (source) {
      params.source_contents = source;
      params.source_site_instance =
          source->GetPrimaryMainFrame()->GetSiteInstance();
      params.is_renderer_initiated = true;
      params.initiator_origin =
          source->GetPrimaryMainFrame()->GetLastCommittedOrigin();
    }
    ui_test_utils::NavigateToURL(&params);
    return params.navigated_or_inserted_contents;
  }

  // The Space's empty tab, or null.
  content::WebContents* EmptyTabOf(const tab_groups::TabGroupId& space) {
    TabStripModel* tab_strip = browser()->GetTabStripModel();
    for (int index : TabsOfSpace(tab_strip, space)) {
      if (IsEmptyTab(tab_strip->GetTabAtIndex(index))) {
        return tab_strip->GetWebContentsAt(index);
      }
    }
    return nullptr;
  }

  // Whether the Space's empty tab is in `container` (none: the default).
  bool EmptyTabIsIn(const tab_groups::TabGroupId& space,
                    const std::string& container) {
    content::WebContents* empty = EmptyTabOf(space);
    return empty && containers::ContainerOf(empty) ==
                        containers::SpaceContainerFromString(container);
  }

  // The names of the cookies of `container`'s partition (none: the
  // default).
  std::vector<std::string> CookieNames(const std::string& container) {
    const std::optional<containers::ContainerId> id =
        containers::SpaceContainerFromString(container);
    content::StoragePartition* partition =
        id ? profile()->GetStoragePartition(
                 containers::PartitionConfigFor(profile(), *id))
           : profile()->GetDefaultStoragePartition();
    base::test::TestFuture<const std::vector<net::CanonicalCookie>&> cookies;
    partition->GetCookieManagerForBrowserProcess()->GetAllCookies(
        cookies.GetCallback());
    std::vector<std::string> names;
    for (const net::CanonicalCookie& cookie : cookies.Get()) {
      names.push_back(cookie.Name());
    }
    return names;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest,
                       NewTabsTakeTheirSpacesContainer) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  ASSERT_FALSE(work.empty());

  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();
  EXPECT_EQ(work, spaces->GetSpaceContainer(work_space));
  EXPECT_EQ(work, spaces->GetSpace(work_space)->container);
  // The new Space's empty tab, made before the Space had its profile, moves
  // into it.
  EXPECT_TRUE(
      base::test::RunUntil([&] { return EmptyTabIsIn(work_space, work); }));

  // Cmd+T, the URL bar: in the Space's container.
  content::WebContents* in_work = OpenNewTab(GURL(kPage));
  ASSERT_TRUE(in_work);
  EXPECT_EQ(containers::SpaceContainerFromString(work),
            containers::ContainerOf(in_work));
  EXPECT_EQ(work_space,
            browser()->GetTabStripModel()->GetActiveTab()->GetGroup());

  // The first Space has none: the default partition.
  spaces->SwitchTo(spaces->GetSpaces()[0].id);
  content::WebContents* in_default = OpenNewTab(GURL(kPage));
  ASSERT_TRUE(in_default);
  EXPECT_EQ(std::nullopt, containers::ContainerOf(in_default));
  EXPECT_EQ("", spaces->GetSpaceContainer(spaces->GetSpaces()[0].id));
}

// A page's tabs keep the page's partition (Firefox's opener inheritance), and
// a tab moved to another Space keeps its own (Zen).
IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest,
                       PagesAndMovedTabsKeepTheirPartition) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  const tab_groups::TabGroupId first = spaces->GetSpaces()[0].id;
  content::WebContents* plain = OpenNewTab(GURL(kPage));
  ASSERT_TRUE(plain);
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId work_space = *spaces->GetActiveSpace();

  // Moved into Work, the plain tab keeps the default partition, and Work its
  // profile.
  spaces->MoveTabsToSpace({tabs::TabInterface::GetFromContents(plain)},
                          work_space);
  EXPECT_EQ(work_space, tabs::TabInterface::GetFromContents(plain)->GetGroup());
  EXPECT_EQ(std::nullopt, containers::ContainerOf(plain));
  EXPECT_EQ(work, spaces->GetSpaceContainer(work_space));

  // Its links stay in the default partition; a Work tab's stay in Work's.
  content::WebContents* plain_link = OpenNewTab(GURL(kOtherPage), plain);
  ASSERT_TRUE(plain_link);
  EXPECT_EQ(std::nullopt, containers::ContainerOf(plain_link));
  content::WebContents* in_work = OpenNewTab(GURL(kPage));
  content::WebContents* work_link = OpenNewTab(GURL(kOtherPage), in_work);
  ASSERT_TRUE(work_link);
  EXPECT_EQ(containers::SpaceContainerFromString(work),
            containers::ContainerOf(work_link));

  // A Work tab moved to the first Space keeps Work's container, and the
  // first Space stays without one.
  spaces->MoveTabsToSpace({tabs::TabInterface::GetFromContents(in_work)},
                          first);
  EXPECT_EQ(containers::SpaceContainerFromString(work),
            containers::ContainerOf(in_work));
  EXPECT_EQ("", spaces->GetSpaceContainer(first));
}

// Set Profile changes where the Space's new tabs open; its tabs keep theirs,
// and its empty tab follows.
IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest,
                       SetProfileMovesOnlyTheEmptyTab) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {});
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const tab_groups::TabGroupId space = *spaces->GetActiveSpace();
  ASSERT_TRUE(base::test::RunUntil([&] { return EmptyTabIsIn(space, ""); }));
  content::WebContents* before = OpenNewTab(GURL(kPage));
  ASSERT_TRUE(before);

  spaces->SetSpaceContainer(space, work);
  EXPECT_EQ(work, spaces->GetSpaceContainer(space));
  EXPECT_TRUE(base::test::RunUntil([&] { return EmptyTabIsIn(space, work); }));
  EXPECT_EQ(std::nullopt, containers::ContainerOf(before));
  EXPECT_EQ(containers::SpaceContainerFromString(work),
            containers::ContainerOf(OpenNewTab(GURL(kOtherPage))));

  // Back to Default.
  spaces->SetSpaceContainer(space, std::string());
  EXPECT_EQ("", spaces->GetSpaceContainer(space));
  EXPECT_TRUE(base::test::RunUntil([&] { return EmptyTabIsIn(space, ""); }));
  // A container that isn't one of the user's saved ones is no container.
  spaces->SetSpaceContainer(space, "agent:tsomething");
  EXPECT_EQ("", spaces->GetSpaceContainer(space));
}

// The reviewer's check: logged in in one Space, not in another with its own
// profile; the same profile shares the login; logging out in one leaves the
// other's.
IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest, LoginsStayApart) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string a = AddProfile(u"A");
  const std::string b = AddProfile(u"B");
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  const auto open_in = [&](const std::u16string& name,
                           const std::string& container) {
    spaces->CreateSpace(name, u"", {}, container);
    return OpenNewTab(page);
  };

  content::WebContents* in_a = open_in(u"A", a);
  ASSERT_TRUE(content::ExecJs(in_a,
                              "document.cookie = 'login=alice; max-age=3600';"
                              "localStorage.setItem('theme', 'dark');"));
  // (Read back in the page that set it: the cookie store has it then.)
  EXPECT_EQ("login=alice", content::EvalJs(in_a, "document.cookie"));
  content::WebContents* in_b = open_in(u"B", b);
  EXPECT_EQ("", content::EvalJs(in_b, "document.cookie"));
  EXPECT_EQ(true,
            content::EvalJs(in_b, "localStorage.getItem('theme') === null"));
  content::WebContents* also_a = open_in(u"A too", a);
  EXPECT_EQ("login=alice", content::EvalJs(also_a, "document.cookie"));
  EXPECT_EQ("dark", content::EvalJs(also_a, "localStorage.getItem('theme')"));
  content::WebContents* in_default = open_in(u"None", "");
  EXPECT_EQ("", content::EvalJs(in_default, "document.cookie"));

  ASSERT_TRUE(
      content::ExecJs(in_b, "document.cookie = 'login=bob; max-age=3600';"));
  ASSERT_TRUE(content::ExecJs(in_a, "document.cookie = 'login=; max-age=0';"));
  EXPECT_EQ("", content::EvalJs(in_a, "document.cookie"));
  EXPECT_EQ("", content::EvalJs(also_a, "document.cookie"));
  EXPECT_EQ("login=bob", content::EvalJs(in_b, "document.cookie"));
}

// The Space's profile is in the stored Spaces, for a start without a
// session restore.
IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest, StoredSpacesKeepIt) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  EXPECT_TRUE(base::test::RunUntil([&] {
    for (const base::Value& entry :
         profile()->GetPrefs()->GetList(prefs::kWorkspaceList)) {
      const std::string* container = entry.GetDict().FindString("container");
      if (container && *container == work) {
        return true;
      }
    }
    return false;
  }));
}

// After a restart: the Space keeps its profile, its tab is back in the
// container, and the login is still there, in the container only.
IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest,
                       PRE_ContainerTabsComeBack) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  const std::string work = AddProfile(u"Work");
  spaces->CreateSpace(u"Work", u"", {}, work);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  content::WebContents* tab =
      OpenNewTab(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(
      content::ExecJs(tab, "document.cookie = 'login=alice; max-age=3600';"));
  EXPECT_EQ("login=alice", content::EvalJs(tab, "document.cookie"));
  EXPECT_EQ(std::vector<std::string>{"login"}, CookieNames(work));
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  for (int i = 0; i < tab_strip->count(); ++i) {
    EXPECT_TRUE(content::WaitForLoadStop(tab_strip->GetWebContentsAt(i)));
  }
  FlushSession(browser());
}

IN_PROC_BROWSER_TEST_F(KoanSpaceContainersBrowserTest, ContainerTabsComeBack) {
  KoanWorkspaceController* spaces = GetSpaces(browser());
  ASSERT_TRUE(spaces);
  ASSERT_TRUE(WaitForSpaceCount(browser(), 2));
  const std::vector<containers::UserContainer> listed =
      containers::ParseUserContainers(
          profile()->GetPrefs()->GetList(prefs::kUserContainers));
  ASSERT_EQ(1u, listed.size());
  const std::string work = containers::Serialize(listed[0].id);
  const tab_groups::TabGroupId work_space = spaces->GetSpaces()[1].id;
  EXPECT_EQ(work, spaces->GetSpaceContainer(work_space));

  TabStripModel* tab_strip = browser()->GetTabStripModel();
  content::WebContents* restored = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (int index : TabsOfSpace(tab_strip, work_space)) {
      if (tab_strip->GetWebContentsAt(index)->GetVisibleURL().path() ==
          "/title1.html") {
        restored = tab_strip->GetWebContentsAt(index);
      }
    }
    return restored != nullptr;
  }));
  EXPECT_EQ(listed[0].id, containers::ContainerOf(restored));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return EmptyTabIsIn(work_space, work); }));
  EXPECT_EQ(std::vector<std::string>{"login"}, CookieNames(work));
  EXPECT_TRUE(CookieNames("").empty());
}

}  // namespace koan
