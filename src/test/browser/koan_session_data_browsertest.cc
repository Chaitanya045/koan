// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/test/browser/koan_browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

namespace koan {

using KoanSessionDataBrowserTest = InProcessBrowserTest;

namespace {

// The test's own keys of a tab's data. Features write theirs to every tab
// too (Zen's empty-tab mark, window sync's ids, a Space's data), so only
// these are compared.
std::map<std::string, std::string> TestKeys(content::WebContents* contents) {
  std::map<std::string, std::string> data = GetTabSessionData(contents);
  std::erase_if(data, [](const auto& entry) {
    return entry.first != "koan.one" && entry.first != "koan.two";
  });
  return data;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest, TabDataIsPerFeatureAndTab) {
  TabStripModel* tab_strip = browser()->GetTabStripModel();
  // Not the window's first tab, which is Zen's empty tab (that is data too).
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  content::WebContents* first = tab_strip->GetWebContentsAt(1);
  EXPECT_EQ("", GetTabSessionData(first, "koan.one"));
  EXPECT_TRUE(TestKeys(first).empty());

  SetTabSessionData(first, "koan.one", "1");
  SetTabSessionData(first, "koan.two", "{\"a\":2}");
  EXPECT_EQ("1", GetTabSessionData(first, "koan.one"));
  EXPECT_EQ("{\"a\":2}", GetTabSessionData(first, "koan.two"));
  EXPECT_THAT(TestKeys(first),
              testing::ElementsAre(testing::Pair("koan.one", "1"),
                                   testing::Pair("koan.two", "{\"a\":2}")));

  // Another tab has none of it.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  EXPECT_TRUE(TestKeys(tab_strip->GetWebContentsAt(2)).empty());
  EXPECT_EQ("1", GetTabSessionData(first, "koan.one"));
}

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest, EmptyDataRemovesTheEntry) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(1);
  SetTabSessionData(contents, "koan.one", "1");
  SetTabSessionData(contents, "koan.one", "");
  EXPECT_EQ("", GetTabSessionData(contents, "koan.one"));
  EXPECT_FALSE(GetTabSessionData(contents).contains("koan.one"));
}

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest, NullContentsHaveNoData) {
  EXPECT_EQ("", GetTabSessionData(nullptr, "koan.one"));
  EXPECT_TRUE(GetTabSessionData(nullptr).empty());
}

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest, WindowData) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  EXPECT_EQ("", GetWindowSessionData(browser(), "koan.one"));

  SetWindowSessionData(browser(), "koan.one", "w1");
  SetWindowSessionData(browser(), "koan.two", "w2");
  EXPECT_EQ("w1", GetWindowSessionData(browser(), "koan.one"));
  EXPECT_THAT(GetWindowSessionData(browser()),
              testing::ElementsAre(testing::Pair("koan.one", "w1"),
                                   testing::Pair("koan.two", "w2")));

  SetWindowSessionData(browser(), "koan.one", "");
  EXPECT_EQ("", GetWindowSessionData(browser(), "koan.one"));
  EXPECT_THAT(GetWindowSessionData(browser()),
              testing::ElementsAre(testing::Pair("koan.two", "w2")));
}

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest,
                       RestoringATabTakesKoansEntries) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(1);
  std::vector<std::unique_ptr<content::NavigationEntry>> entries;
  int selected = 0;
  // Only "koan." entries with data are Koan's.
  OnRestoringTab(contents,
                 {{"koan.one", "1"},
                  {"koan.empty", ""},
                  {"other.thing", "x"},
                  {"koan.two", "2"}},
                 /*from_session_restore=*/true, entries, selected);
  EXPECT_THAT(GetTabSessionData(contents),
              testing::ElementsAre(testing::Pair("koan.one", "1"),
                                   testing::Pair("koan.two", "2")));
  EXPECT_TRUE(entries.empty());
}

IN_PROC_BROWSER_TEST_F(KoanSessionDataBrowserTest, RestoringAWindowMerges) {
  ASSERT_TRUE(WaitForSpaceCount(browser(), 1));
  SetWindowSessionData(browser(), "koan.own", "mine");
  SetWindowSessionData(browser(), "koan.both", "old");

  // A session restored into an open window: its data comes in, the window's
  // own stays.
  OnRestoringWindow(browser(),
                    {{"koan.both", "new"}, {"koan.theirs", "t"}, {"x", "y"}},
                    /*from_session_restore=*/true);
  EXPECT_THAT(GetWindowSessionData(browser()),
              testing::ElementsAre(testing::Pair("koan.both", "new"),
                                   testing::Pair("koan.own", "mine"),
                                   testing::Pair("koan.theirs", "t")));
}

}  // namespace koan
