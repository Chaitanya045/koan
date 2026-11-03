// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_library_logic.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::library {

TEST(KoanLibraryLogicTest, FormatUrlDropsSchemeWwwAndTrailingSlash) {
  EXPECT_EQ(u"example.com", FormatUrl(GURL("https://www.example.com/")));
  EXPECT_EQ(u"example.com/a", FormatUrl(GURL("https://www.example.com/a/")));
  EXPECT_EQ(u"example.com/a?b=1#c",
            FormatUrl(GURL("http://example.com/a?b=1#c")));
}

TEST(KoanLibraryLogicTest, FormatUrlKeepsOtherSchemes) {
  EXPECT_EQ(u"chrome://version", FormatUrl(GURL("chrome://version/")));
  EXPECT_EQ(u"file:///tmp/a.html", FormatUrl(GURL("file:///tmp/a.html")));
  // "www." goes only after http(s).
  EXPECT_EQ(u"ftp://www.example.com",
            FormatUrl(GURL("ftp://www.example.com/")));
}

TEST(KoanLibraryLogicTest, SiteIsTheHostWithoutWww) {
  EXPECT_EQ(u"example.com", SiteOf(GURL("https://www.example.com/a")));
  EXPECT_EQ(u"docs.example.com", SiteOf(GURL("http://docs.example.com/")));
  // Only a leading "www.".
  EXPECT_EQ(u"awww.example.com", SiteOf(GURL("https://awww.example.com/")));
}

TEST(KoanLibraryLogicTest, SiteOfPagesNotOnTheWebIsEmpty) {
  EXPECT_EQ(u"", SiteOf(GURL("chrome://version/")));
  EXPECT_EQ(u"", SiteOf(GURL("file:///tmp/a.html")));
  EXPECT_EQ(u"", SiteOf(GURL()));
}

TEST(KoanLibraryLogicTest, DropSlotCountsTheCardsPassed) {
  // Four cards 100 apart; card 1 is dragged.
  const std::vector<int> centers = {50, 150, 250, 350};
  EXPECT_EQ(0, DropSlot(centers, 1, 10));
  // Its own centre doesn't count: still slot 1 between 50 and 250.
  EXPECT_EQ(1, DropSlot(centers, 1, 150));
  EXPECT_EQ(1, DropSlot(centers, 1, 200));
  EXPECT_EQ(2, DropSlot(centers, 1, 300));
  EXPECT_EQ(3, DropSlot(centers, 1, 900));
}

TEST(KoanLibraryLogicTest, DropSlotNeedsToPassACentre) {
  const std::vector<int> centers = {50, 150, 250};
  // Exactly on another card's centre has not passed it.
  EXPECT_EQ(0, DropSlot(centers, 2, 50));
  EXPECT_EQ(1, DropSlot(centers, 2, 51));
}

TEST(KoanLibraryLogicTest, DropSlotOfOneCardIsZero) {
  EXPECT_EQ(0, DropSlot(std::vector<int>{50}, 0, 1000));
}

TEST(KoanLibraryLogicTest, CardsStepAsideTowardsTheVacatedPlace) {
  constexpr int kStep = 266;
  // Card 1 dragged right over slot 3: cards 2 and 3 move left.
  EXPECT_EQ(0, CardShift(0, 1, 3, kStep));
  EXPECT_EQ(0, CardShift(1, 1, 3, kStep));
  EXPECT_EQ(-kStep, CardShift(2, 1, 3, kStep));
  EXPECT_EQ(-kStep, CardShift(3, 1, 3, kStep));
  EXPECT_EQ(0, CardShift(4, 1, 3, kStep));
  // Card 3 dragged left over slot 1: cards 1 and 2 move right.
  EXPECT_EQ(0, CardShift(0, 3, 1, kStep));
  EXPECT_EQ(kStep, CardShift(1, 3, 1, kStep));
  EXPECT_EQ(kStep, CardShift(2, 3, 1, kStep));
  EXPECT_EQ(0, CardShift(3, 3, 1, kStep));
  EXPECT_EQ(0, CardShift(4, 3, 1, kStep));
}

TEST(KoanLibraryLogicTest, NothingShiftsWhileOverItsOwnSlot) {
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(0, CardShift(i, 2, 2, 266)) << i;
  }
}

}  // namespace koan::library
