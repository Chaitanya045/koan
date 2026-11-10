// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_pinned_site.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan {
namespace {

TEST(KoanPinnedSiteTest, UrlBarStaysOnTheSameHost) {
  const GURL page("https://github.com/zen-browser/desktop");
  EXPECT_TRUE(
      StaysOnPinnedSite(page, GURL("https://github.com/notifications")));
  // Zen compares hosts only: the scheme and port don't count.
  EXPECT_TRUE(StaysOnPinnedSite(page, GURL("http://github.com/")));
  EXPECT_TRUE(StaysOnPinnedSite(page, GURL("https://github.com:8443/")));
}

TEST(KoanPinnedSiteTest, UrlBarOpensANewTabElsewhere) {
  const GURL page("https://github.com/");
  EXPECT_FALSE(StaysOnPinnedSite(page, GURL("https://apple.com/")));
  // A subdomain is another host, as in Zen.
  EXPECT_FALSE(StaysOnPinnedSite(page, GURL("https://gist.github.com/")));
  EXPECT_FALSE(StaysOnPinnedSite(GURL("https://www.github.com/"),
                                 GURL("https://github.com/")));
  // A search is a URL on the search engine's host.
  EXPECT_FALSE(
      StaysOnPinnedSite(page, GURL("https://search.brave.com/search?q=koan")));
  // Not http(s) on either side: a new tab.
  EXPECT_FALSE(StaysOnPinnedSite(GURL("chrome://newtab/"), page));
  EXPECT_FALSE(StaysOnPinnedSite(page, GURL("file:///tmp/a.html")));
  EXPECT_FALSE(StaysOnPinnedSite(page, GURL()));
}

TEST(KoanPinnedSiteTest, LinksToAnotherHostLeave) {
  const GURL page("https://mail.example.com/inbox");
  EXPECT_TRUE(LeavesPinnedSite(page, GURL("https://news.ycombinator.com/")));
  EXPECT_TRUE(LeavesPinnedSite(page, GURL("https://example.com/")));
  EXPECT_FALSE(LeavesPinnedSite(page, GURL("https://mail.example.com/x")));
  EXPECT_FALSE(LeavesPinnedSite(page, GURL("http://mail.example.com/x")));
}

TEST(KoanPinnedSiteTest, OnlyHttpLinksOpenInGlance) {
  const GURL page("https://example.com/");
  EXPECT_FALSE(LeavesPinnedSite(page, GURL("file:///tmp/a.html")));
  EXPECT_FALSE(LeavesPinnedSite(page, GURL("mailto:a@example.org")));
  EXPECT_FALSE(LeavesPinnedSite(page, GURL("chrome://settings/")));
  EXPECT_FALSE(LeavesPinnedSite(page, GURL()));
  // From a page that isn't http(s) any http(s) link leaves (Zen: about:).
  EXPECT_TRUE(LeavesPinnedSite(GURL("chrome://newtab/"), page));
  EXPECT_TRUE(LeavesPinnedSite(GURL(), page));
}

}  // namespace
}  // namespace koan
