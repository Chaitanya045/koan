// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_url_pill_buttons.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

UrlPillButtonsState PageState() {
  UrlPillButtonsState state;
  state.has_page = true;
  state.show_copy_url = true;
  return state;
}

TEST(KoanUrlPillButtonsTest, PageShowsBothFadedOutAtRest) {
  const UrlPillButtons buttons = GetUrlPillButtons(PageState());
  EXPECT_TRUE(buttons.copy_url);
  EXPECT_TRUE(buttons.extensions);
  EXPECT_FALSE(buttons.revealed);
}

TEST(KoanUrlPillButtonsTest, EmptyTabHasNeither) {
  UrlPillButtonsState state = PageState();
  state.has_page = false;
  state.sidebar_hovered = true;
  const UrlPillButtons buttons = GetUrlPillButtons(state);
  EXPECT_FALSE(buttons.copy_url);
  EXPECT_FALSE(buttons.extensions);
}

TEST(KoanUrlPillButtonsTest, EditingHidesBoth) {
  UrlPillButtonsState state = PageState();
  state.editing = true;
  const UrlPillButtons buttons = GetUrlPillButtons(state);
  EXPECT_FALSE(buttons.copy_url);
  EXPECT_FALSE(buttons.extensions);
}

TEST(KoanUrlPillButtonsTest, CopyUrlFollowsItsSetting) {
  UrlPillButtonsState state = PageState();
  state.show_copy_url = false;
  const UrlPillButtons buttons = GetUrlPillButtons(state);
  EXPECT_FALSE(buttons.copy_url);
  EXPECT_TRUE(buttons.extensions);
}

// Zen reveals them on the sidebar's hover, not only the pill's.
TEST(KoanUrlPillButtonsTest, RevealedBySidebarHoverFocusOrOpenPanel) {
  for (int i = 0; i < 4; ++i) {
    UrlPillButtonsState state = PageState();
    state.sidebar_hovered = i == 0;
    state.pill_hovered = i == 1;
    state.button_focused = i == 2;
    state.menu_open = i == 3;
    EXPECT_TRUE(GetUrlPillButtons(state).revealed) << i;
  }
}

}  // namespace
}  // namespace koan
