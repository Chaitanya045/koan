// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_strip_on_share.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan {
namespace {

std::string Strip(const char* url) {
  return StripTrackingParameters(GURL(url)).spec();
}

TEST(KoanStripOnShareTest, DropsGlobalParametersAndKeepsTheRest) {
  EXPECT_EQ(Strip("https://example.com/a?utm_source=x&id=7&fbclid=y&b=2#top"),
            "https://example.com/a?id=7&b=2#top");
}

TEST(KoanStripOnShareTest, DropsTheQuestionMarkWithNothingLeft) {
  EXPECT_EQ(Strip("https://example.com/?utm_medium=a&gclid=b"),
            "https://example.com/");
}

TEST(KoanStripOnShareTest, SiteParametersOnlyOnTheirSites) {
  EXPECT_EQ(Strip("https://open.spotify.com/track/1?si=abc&t=3"),
            "https://open.spotify.com/track/1?t=3");
  EXPECT_EQ(Strip("https://example.com/?si=abc"),
            "https://example.com/?si=abc");
  // A different site that ends the same way isn't the site.
  EXPECT_EQ(Strip("https://notspotify.com/?si=abc"),
            "https://notspotify.com/?si=abc");
}

TEST(KoanStripOnShareTest, NamesAreComparedAsWritten) {
  EXPECT_EQ(Strip("https://example.com/?UTM_SOURCE=a&ScCid=b"),
            "https://example.com/?UTM_SOURCE=a");
  EXPECT_EQ(Strip("https://www.facebook.com/p?__cft__%5B0%5D=z&x=1"),
            "https://www.facebook.com/p?x=1");
}

TEST(KoanStripOnShareTest, LeavesOtherUrlsAlone) {
  EXPECT_EQ(Strip("https://example.com/?q=utm_source"),
            "https://example.com/?q=utm_source");
  EXPECT_EQ(Strip("chrome://settings/?utm_source=a"),
            "chrome://settings/?utm_source=a");
  EXPECT_EQ(Strip("https://example.com/path"), "https://example.com/path");
}

}  // namespace
}  // namespace koan
