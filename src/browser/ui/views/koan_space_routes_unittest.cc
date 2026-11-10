// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_routes.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

SpaceRoute Route(std::string reference,
                 SpaceRouteMatch match,
                 std::u16string space) {
  SpaceRoute route;
  route.reference = std::move(reference);
  route.match = match;
  route.space = std::move(space);
  return route;
}

}  // namespace

TEST(KoanSpaceRoutesTest, ContainsIgnoresCase) {
  const SpaceRoute route =
      Route("YouTube.com", SpaceRouteMatch::kContains, u"Media");
  EXPECT_TRUE(IsSpaceRouteMatching("https://www.youtube.com/watch", route));
  EXPECT_FALSE(IsSpaceRouteMatching("https://vimeo.com/", route));
}

TEST(KoanSpaceRoutesTest, EqualToIgnoresSchemeWwwAndFinalSlash) {
  const SpaceRoute route =
      Route("https://example.com/", SpaceRouteMatch::kEqualTo, u"Work");
  EXPECT_TRUE(IsSpaceRouteMatching("http://www.example.com", route));
  EXPECT_TRUE(IsSpaceRouteMatching("https://EXAMPLE.com/", route));
  EXPECT_FALSE(IsSpaceRouteMatching("https://example.com/a", route));
}

TEST(KoanSpaceRoutesTest, RegexIsCaseInsensitiveAndInvalidMatchesNothing) {
  EXPECT_TRUE(IsSpaceRouteMatching(
      "https://GitHub.com/x",
      Route("github\\.com/.*", SpaceRouteMatch::kRegex, u"Work")));
  // RE2 has no lookahead.
  EXPECT_FALSE(IsValidSpaceRouteRegex("a(?=b)"));
  EXPECT_FALSE(IsSpaceRouteMatching(
      "https://ab.com", Route("a(?=b)", SpaceRouteMatch::kRegex, u"Work")));
  EXPECT_TRUE(IsValidSpaceRouteRegex("(a|b)\\.com"));
}

TEST(KoanSpaceRoutesTest, EmptyReferenceMatchesNothing) {
  EXPECT_FALSE(IsSpaceRouteMatching(
      "https://a.com", Route("  ", SpaceRouteMatch::kContains, u"Work")));
}

TEST(KoanSpaceRoutesTest, FirstMatchingRouteWins) {
  const std::vector<SpaceRoute> routes = {
      Route("mail", SpaceRouteMatch::kContains, u"Work"),
      Route("google", SpaceRouteMatch::kContains, u"Home")};
  EXPECT_EQ(u"Work",
            RouteUrlToSpace("https://mail.google.com", routes, false, u""));
  EXPECT_EQ(u"Home",
            RouteUrlToSpace("https://www.google.com", routes, false, u""));
  EXPECT_FALSE(RouteUrlToSpace("https://a.com", routes, false, u"Home"));
}

TEST(KoanSpaceRoutesTest, ExternalLinksTakeTheDefault) {
  const std::vector<SpaceRoute> routes = {
      Route("a.com", SpaceRouteMatch::kContains, u"")};
  EXPECT_EQ(u"Inbox", RouteUrlToSpace("https://b.com", routes, true, u"Inbox"));
  // A route to the most recent Space wins over the default.
  EXPECT_FALSE(RouteUrlToSpace("https://a.com", routes, true, u"Inbox"));
  EXPECT_FALSE(RouteUrlToSpace("https://b.com", routes, true, u""));
}

TEST(KoanSpaceRoutesTest, RouteForHosts) {
  EXPECT_FALSE(RouteForHosts({}));
  const std::optional<SpaceRoute> one = RouteForHosts({"a.com", "a.com"});
  ASSERT_TRUE(one);
  EXPECT_EQ("a.com", one->reference);
  EXPECT_EQ(SpaceRouteMatch::kContains, one->match);
  const std::optional<SpaceRoute> two = RouteForHosts({"a.com", "b.org"});
  ASSERT_TRUE(two);
  EXPECT_EQ("(a\\.com|b\\.org)", two->reference);
  EXPECT_EQ(SpaceRouteMatch::kRegex, two->match);
  EXPECT_TRUE(IsSpaceRouteMatching("https://www.b.org/x", *two));
}

TEST(KoanSpaceRoutesTest, RenameFollowsTheSpace) {
  std::vector<SpaceRoute> routes = {
      Route("a", SpaceRouteMatch::kContains, u"Work"),
      Route("b", SpaceRouteMatch::kContains, u""),
      Route("c", SpaceRouteMatch::kContains, u"Home")};
  EXPECT_TRUE(RenameSpaceInRoutes(routes, u"Work", u"Job"));
  EXPECT_EQ(u"Job", routes[0].space);
  EXPECT_EQ(u"", routes[1].space);
  EXPECT_EQ(u"Home", routes[2].space);
  EXPECT_FALSE(RenameSpaceInRoutes(routes, u"Gone", u"X"));
  EXPECT_FALSE(RenameSpaceInRoutes(routes, u"", u"X"));
}

TEST(KoanSpaceRoutesTest, RoundTripsAndDropsEmptyReferences) {
  const std::vector<SpaceRoute> routes = {
      Route("a.com", SpaceRouteMatch::kEqualTo, u"Work"),
      Route("", SpaceRouteMatch::kContains, u"Home"),
      Route("b", SpaceRouteMatch::kRegex, u"")};
  const std::vector<SpaceRoute> parsed =
      ParseSpaceRoutes(SpaceRoutesToValue(routes));
  ASSERT_EQ(2u, parsed.size());
  EXPECT_EQ(routes[0], parsed[0]);
  EXPECT_EQ(routes[2], parsed[1]);
}

}  // namespace koan
