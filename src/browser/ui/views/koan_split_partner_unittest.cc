// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_partner.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

constexpr SplitPartnerCandidate kTab{.in_active_space = true};
constexpr SplitPartnerCandidate kOtherSpace{};
constexpr SplitPartnerCandidate kEssential{.in_active_space = true,
                                           .pinned = true};

}  // namespace

TEST(KoanSplitPartnerTest, TakesTheNextTabOfTheSpace) {
  EXPECT_EQ(2u, PickSplitPartner({kOtherSpace, kTab, kTab, kTab}, 1));
}

// Issue #553: the last tab of a Space, with another Space after it in the
// strip, never takes that Space's first tab.
TEST(KoanSplitPartnerTest, NeverTakesATabOfAnotherSpace) {
  EXPECT_EQ(std::nullopt, PickSplitPartner({kTab, kOtherSpace}, 0));
  EXPECT_EQ(std::nullopt,
            PickSplitPartner({kOtherSpace, kTab, kOtherSpace}, 1));
}

TEST(KoanSplitPartnerTest, WrapsToTheFirstTabOfTheSpace) {
  // Zen's toggleShortcut(): past the last visible tab, the first one.
  EXPECT_EQ(1u, PickSplitPartner({kOtherSpace, kTab, kTab, kOtherSpace}, 2));
}

TEST(KoanSplitPartnerTest, SkipsHiddenAndSplitTabs) {
  const SplitPartnerCandidate hidden{.in_active_space = true, .hidden = true};
  const SplitPartnerCandidate split{.in_active_space = true, .split = true};
  EXPECT_EQ(3u, PickSplitPartner({kTab, hidden, split, kTab}, 0));
  EXPECT_EQ(std::nullopt, PickSplitPartner({kTab, hidden, split}, 0));
}

TEST(KoanSplitPartnerTest, EssentialsOnlyPairWithEssentials) {
  EXPECT_EQ(std::nullopt, PickSplitPartner({kEssential, kTab, kTab}, 0));
  EXPECT_EQ(1u, PickSplitPartner({kEssential, kEssential, kTab}, 0));
  // A normal tab wraps past the Essentials to its Space's first tab.
  EXPECT_EQ(2u, PickSplitPartner({kEssential, kEssential, kTab, kTab}, 3));
}

TEST(KoanSplitPartnerTest, NothingForABadIndex) {
  EXPECT_EQ(std::nullopt, PickSplitPartner({}, 0));
  EXPECT_EQ(std::nullopt, PickSplitPartner({kTab}, 0));
  EXPECT_EQ(std::nullopt, PickSplitPartner({kTab, kTab}, 2));
}

}  // namespace koan
