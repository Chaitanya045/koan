// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs (the edge actions and when they are allowed),
//   ZenSpaceAddSwipe.mjs (the add-Space swipe's progress and threshold),
//   ZenLibrary.mjs (readySwipeOpenLibrary(), swipeProgress(), stopSwipe())
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_EDGE_ACTIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_EDGE_ACTIONS_H_

#include <cstddef>

namespace koan {

// Zen's swipe edge actions (zen.workspaces.swipe-actions.edge-actions, on):
// a two-finger swipe over the sidebar (koan_swipe_tracker.h) that would go
// past the Space next to the Library opens the Library instead, and one past
// the Space at the other end offers to create a Space. Everything here works
// on Koan's gesture amount (positive: fingers moving left, after
// zen.workspaces.natural-scroll), in screen terms. Plain C++, so that it can
// be tested without a window.

// What a swipe does instead of switching Space (Zen's ZenSpacesSwipe.ACTIONS).
enum class SwipeEdgeAction { kNone, kLibrary, kAddSpace };

// What a swipe starting now may turn into (Zen's swipeState.allowed).
struct SwipeEdgeActions {
  bool library = false;
  bool add_space = false;
};

// The window as a swipe starts.
struct SwipeEdgeContext {
  // The active Space, of `count` (0 if none is).
  size_t index = 0;
  size_t count = 0;
  // kWorkspacesSwipeEdgeActions, and the window has a Library (Zen asks
  // zen.library.enabled for both actions).
  bool edge_actions = false;
  bool library_open = false;
  // Whether the Library (the sidebar's side) is on the screen's right.
  bool library_on_screen_right = false;
  // A right-to-left UI, which turns the swipe round (fingers moving left go
  // to the next Space).
  bool rtl = false;
  // The Create-a-Space form is open (Zen's creatingWorkspaceId).
  bool creating_space = false;
};

// Whether a strip dragged by `amount` moves towards the Library, which it
// uncovers: the strip moves against the fingers, so to the right (towards a
// Library on the left) for a positive amount. ZenSpacesSwipe's
// towardsLibrary.
bool SwipeTowardsLibrary(float amount, bool library_on_screen_right);

// Zen's readySwipeOpenLibrary() and readySwipeAddSpace(): the Library while it
// is open, or from the Space at the Library's end of the list (the first with
// the Library on the left); a new Space from the Space at the other end (the
// last). With one Space, both. Nothing with edge actions off or no Library.
SwipeEdgeActions AllowedSwipeEdgeActions(const SwipeEdgeContext& context);

// What a swipe is from its start: the Library's while it is open (a swipe
// then only moves the Library), else nothing yet.
SwipeEdgeAction InitialSwipeEdgeAction(bool library_open);

// Zen's #decideAction(), asked while a swipe still only moves Spaces: the
// Library if it may open and the swipe goes its way, a new Space if that may
// come and the swipe goes the other way, else kNone (the next update asks
// again). Nothing at an amount of 0.
SwipeEdgeAction DecideSwipeEdgeAction(const SwipeEdgeActions& allowed,
                                      float amount,
                                      bool library_on_screen_right);

// Zen's rubber band past the ends of a swipe's progress: `offset` past the
// end, damped towards at most `dimension`.
float SwipeRubberBand(float offset, float dimension, float constant);

// ZenLibrary.swipeProgress(): the Library's open progress (0 closed, 1 open)
// for a swipe that found it at `start` and has dragged the strip by Zen's
// `translate` (SwipeTranslate()) over a sidebar `strip_width` wide: 0.8 of
// translate / strip_width towards the Library's side, rubber-banded past
// closed and open.
float LibrarySwipeProgress(float start,
                           float translate,
                           float strip_width,
                           bool library_on_screen_right);

// ZenLibrary.stopSwipe(): whether a swipe that succeeded (SwipeSucceeds(),
// Zen's 0.25 and 0.5) leaves the Library open: it went towards it.
bool LibrarySwipeOpens(float amount, bool library_on_screen_right);

// ZenSpaceAddSwipe.swipeProgress(): the add-Space badge's progress for a
// swipe at `amount`, away from the Library: 1 at Zen's threshold (0.8, 440pt
// of travel), rubber-banded past 0 and 1.
float AddSpaceSwipeProgress(float amount, bool library_on_screen_right);

// Whether a swipe whose badge reached `progress` creates a Space: Zen's 0.8
// threshold with no weight for the velocity (SUCCESS_VELOCITY_CONTRIBUTION 0),
// which is where the badge shows it is ready.
bool AddSpaceSwipeSucceeds(float progress);

// Whether the badge crossed its ready point between `from` and `to` (Zen ticks
// the trackpad then, both ways).
bool AddSpaceSwipeCrossedReady(float from, float to);

// The badge's look at `progress` (ZenSpaceAddSwipe's #progress setter):
// - entry: it and its glow come in over the first 0.75 of the way;
// - the ring's arc, eased in and out, as a fraction of the circle;
// - how far the Space is pushed away from it, in Zen's translateX units
//   (-25 at 1, towards the Library; SwipeOffsetPixels() makes it pixels).
float AddSpaceSwipeEntry(float progress);
float AddSpaceSwipeArc(float progress);
float AddSpaceSwipeSpaceTranslate(float progress, bool library_on_screen_right);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_EDGE_ACTIONS_H_
