// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs, ZenSpaceAddSwipe.mjs, ZenLibrary.mjs (the values the
//   tests check)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Which edge action a swipe over the sidebar turns into, for each position
// and direction, and the Library's and the add-Space badge's progress.

#include "koan/browser/ui/views/koan_swipe_edge_actions.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

// Fingers moving left (the strip moves right, towards a Library on the left)
// and right.
constexpr float kLeft = 0.1f;
constexpr float kRight = -0.1f;

SwipeEdgeContext Context(size_t index, size_t count) {
  SwipeEdgeContext context;
  context.index = index;
  context.count = count;
  context.edge_actions = true;
  return context;
}

SwipeEdgeAction Decide(const SwipeEdgeContext& context, float amount) {
  return DecideSwipeEdgeAction(AllowedSwipeEdgeActions(context), amount,
                               context.library_on_screen_right);
}

}  // namespace

// The Library on the left (Zen's default): it is past the first Space, a new
// Space past the last.
TEST(KoanSwipeEdgeActionsTest, LibraryOnTheLeft) {
  // First of three: towards the Library opens it; the other way is the next
  // Space.
  EXPECT_EQ(Decide(Context(0, 3), kLeft), SwipeEdgeAction::kLibrary);
  EXPECT_EQ(Decide(Context(0, 3), kRight), SwipeEdgeAction::kNone);
  // In the middle, only Spaces.
  EXPECT_EQ(Decide(Context(1, 3), kLeft), SwipeEdgeAction::kNone);
  EXPECT_EQ(Decide(Context(1, 3), kRight), SwipeEdgeAction::kNone);
  // Last: past it a new Space; back is the previous Space.
  EXPECT_EQ(Decide(Context(2, 3), kRight), SwipeEdgeAction::kAddSpace);
  EXPECT_EQ(Decide(Context(2, 3), kLeft), SwipeEdgeAction::kNone);
}

// Tabs (and the Library) on the right: the ends swap.
TEST(KoanSwipeEdgeActionsTest, LibraryOnTheRight) {
  SwipeEdgeContext first = Context(0, 3);
  first.library_on_screen_right = true;
  SwipeEdgeContext last = Context(2, 3);
  last.library_on_screen_right = true;
  EXPECT_EQ(Decide(last, kRight), SwipeEdgeAction::kLibrary);
  EXPECT_EQ(Decide(last, kLeft), SwipeEdgeAction::kNone);
  EXPECT_EQ(Decide(first, kLeft), SwipeEdgeAction::kAddSpace);
  EXPECT_EQ(Decide(first, kRight), SwipeEdgeAction::kNone);
}

// A right-to-left UI turns the swipe round: fingers moving left go to the
// next Space, so the Library on the screen's left is past the last one.
TEST(KoanSwipeEdgeActionsTest, RightToLeft) {
  SwipeEdgeContext first = Context(0, 3);
  first.rtl = true;
  SwipeEdgeContext last = Context(2, 3);
  last.rtl = true;
  EXPECT_EQ(Decide(last, kLeft), SwipeEdgeAction::kLibrary);
  EXPECT_EQ(Decide(first, kRight), SwipeEdgeAction::kAddSpace);
  EXPECT_EQ(Decide(first, kLeft), SwipeEdgeAction::kNone);
}

// With one Space both ends are its own.
TEST(KoanSwipeEdgeActionsTest, OneSpace) {
  EXPECT_EQ(Decide(Context(0, 1), kLeft), SwipeEdgeAction::kLibrary);
  EXPECT_EQ(Decide(Context(0, 1), kRight), SwipeEdgeAction::kAddSpace);
}

// Edge actions off (or no Library in the window): swipes only switch Space,
// and wrap around as before.
TEST(KoanSwipeEdgeActionsTest, Off) {
  SwipeEdgeContext first = Context(0, 3);
  first.edge_actions = false;
  SwipeEdgeContext last = Context(2, 3);
  last.edge_actions = false;
  EXPECT_EQ(Decide(first, kLeft), SwipeEdgeAction::kNone);
  EXPECT_EQ(Decide(last, kRight), SwipeEdgeAction::kNone);
}

// The Create-a-Space form already open: no second one, the Library still.
TEST(KoanSwipeEdgeActionsTest, CreatingSpace) {
  SwipeEdgeContext only = Context(0, 1);
  only.creating_space = true;
  EXPECT_EQ(Decide(only, kRight), SwipeEdgeAction::kNone);
  EXPECT_EQ(Decide(only, kLeft), SwipeEdgeAction::kLibrary);
}

// An open Library takes every swipe from its start, whichever the Space.
TEST(KoanSwipeEdgeActionsTest, LibraryOpen) {
  EXPECT_EQ(InitialSwipeEdgeAction(/*library_open=*/true),
            SwipeEdgeAction::kLibrary);
  EXPECT_EQ(InitialSwipeEdgeAction(/*library_open=*/false),
            SwipeEdgeAction::kNone);
  SwipeEdgeContext middle = Context(1, 3);
  middle.library_open = true;
  EXPECT_TRUE(AllowedSwipeEdgeActions(middle).library);
  middle.edge_actions = false;
  EXPECT_TRUE(AllowedSwipeEdgeActions(middle).library);
}

// Nothing is decided before the strip moves.
TEST(KoanSwipeEdgeActionsTest, NoAmount) {
  EXPECT_EQ(Decide(Context(0, 1), 0), SwipeEdgeAction::kNone);
}

// ZenLibrary.swipeProgress(): 0.8 of translateX / width, from where the
// Library was, rubber-banded past the ends.
TEST(KoanSwipeEdgeActionsTest, LibraryProgress) {
  EXPECT_FLOAT_EQ(LibrarySwipeProgress(0, 125, 250, false), 0.4f);
  EXPECT_FLOAT_EQ(LibrarySwipeProgress(0, -125, 250, true), 0.4f);
  // Swiping an open Library back closes it.
  EXPECT_FLOAT_EQ(LibrarySwipeProgress(1, -125, 250, false), 0.6f);
  // Past the ends it barely moves.
  const float over = LibrarySwipeProgress(1, 250, 250, false);
  EXPECT_GT(over, 1.f);
  EXPECT_LT(over, 1.2f);
  const float under = LibrarySwipeProgress(0, -250, 250, false);
  EXPECT_LT(under, 0.f);
  EXPECT_GT(under, -0.2f);
  // A succeeded swipe opens it if it went its way, else closes it.
  EXPECT_TRUE(LibrarySwipeOpens(kLeft, false));
  EXPECT_FALSE(LibrarySwipeOpens(kRight, false));
  EXPECT_TRUE(LibrarySwipeOpens(kRight, true));
}

// ZenSpaceAddSwipe: ready at 0.8 of travel away from the Library, which is
// where a Space gets created; the haptic tick at that point, both ways.
TEST(KoanSwipeEdgeActionsTest, AddSpaceProgress) {
  EXPECT_FLOAT_EQ(AddSpaceSwipeProgress(-0.4f, false), 0.5f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeProgress(0.4f, true), 0.5f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeProgress(-0.8f, false), 1.f);
  EXPECT_TRUE(AddSpaceSwipeSucceeds(AddSpaceSwipeProgress(-0.8f, false)));
  EXPECT_FALSE(AddSpaceSwipeSucceeds(AddSpaceSwipeProgress(-0.79f, false)));
  // The other way it rubber-bands below 0 and never succeeds.
  EXPECT_LT(AddSpaceSwipeProgress(0.8f, false), 0.f);
  EXPECT_FALSE(AddSpaceSwipeSucceeds(AddSpaceSwipeProgress(2.f, false)));
  EXPECT_TRUE(AddSpaceSwipeCrossedReady(0.9f, 1.f));
  EXPECT_TRUE(AddSpaceSwipeCrossedReady(1.05f, 0.95f));
  EXPECT_FALSE(AddSpaceSwipeCrossedReady(0.5f, 0.9f));
  EXPECT_FALSE(AddSpaceSwipeCrossedReady(1.f, 1.1f));
  // Its look: in by 0.75, the ring eased, the Space pushed 25 units.
  EXPECT_FLOAT_EQ(AddSpaceSwipeEntry(0.375f), 0.5f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeEntry(0.9f), 1.f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeArc(0.5f), 0.5f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeArc(1.2f), 1.f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeSpaceTranslate(1, false), -25.f);
  EXPECT_FLOAT_EQ(AddSpaceSwipeSpaceTranslate(1, true), 25.f);
}

}  // namespace koan
