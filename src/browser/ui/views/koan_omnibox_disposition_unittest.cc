// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_omnibox_disposition.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

using D = WindowOpenDisposition;

TEST(KoanOmniboxDispositionTest, OpenTabRowSwitchesWhereverTheBarWasOpened) {
  EXPECT_EQ(D::kSwitchToTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, /*is_open_tab_match=*/
                                       true, /*open_tab_exists=*/true,
                                       /*new_tab_session=*/true));
  EXPECT_EQ(D::kSwitchToTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, true, true,
                                       /*new_tab_session=*/false));
}

TEST(KoanOmniboxDispositionTest, GoneTabRowIsLikeAnyOtherRow) {
  // In a Cmd+T session it opens a new tab (it must not replace the page the
  // user was on); otherwise it navigates the current tab.
  EXPECT_EQ(D::kNewForegroundTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, true,
                                       /*open_tab_exists=*/false, true));
  EXPECT_EQ(D::kCurrentTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, true, false, false));
}

TEST(KoanOmniboxDispositionTest, OtherRowsOpenANewTabOnlyInANewTabSession) {
  EXPECT_EQ(D::kNewForegroundTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, false, false, true));
  EXPECT_EQ(D::kCurrentTab,
            AdjustedOmniboxDisposition(D::kCurrentTab, false, false, false));
}

TEST(KoanOmniboxDispositionTest, ModifierDispositionsStay) {
  for (D disposition : {D::kNewBackgroundTab, D::kNewWindow,
                        D::kNewForegroundTab, D::kSwitchToTab}) {
    EXPECT_EQ(disposition,
              AdjustedOmniboxDisposition(disposition, true, true, true));
    EXPECT_EQ(disposition,
              AdjustedOmniboxDisposition(disposition, false, false, false));
  }
}

}  // namespace
}  // namespace koan
