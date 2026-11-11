// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpacesSwipe.mjs, ZenSpaceAddSwipe.mjs, ZenLibrary.mjs (see the header)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_swipe_edge_actions.h"

#include <algorithm>
#include <cmath>

namespace koan {

namespace {

// ZenLibrary.swipeProgress() and ZenSpaceAddSwipe.swipeProgress().
constexpr float kDampingDimension = 0.2f;
constexpr float kRubberBandConstant = 0.08f;
// ZenLibrary: LIBRARY_SWIPE_FULL.
constexpr float kLibrarySwipeFull = 0.8f;
// ZenSpaceAddSwipe: SUCCESS_THRESHOLD, ENTRY_PROGRESS, SPACES_TRANSLATION.
constexpr float kAddSpaceThreshold = 0.8f;
constexpr float kAddSpaceEntryProgress = 0.75f;
constexpr float kAddSpaceSpacesTranslation = -25.f;

constexpr float kPi = 3.14159265358979f;

// 0 and 1 with a rubber band past them.
float Damp(float progress) {
  if (progress < 0) {
    return SwipeRubberBand(progress, kDampingDimension, kRubberBandConstant);
  }
  if (progress > 1) {
    return 1 + SwipeRubberBand(progress - 1, kDampingDimension,
                               kRubberBandConstant);
  }
  return progress;
}

}  // namespace

bool SwipeTowardsLibrary(float amount, bool library_on_screen_right) {
  return library_on_screen_right ? amount < 0 : amount > 0;
}

SwipeEdgeActions AllowedSwipeEdgeActions(const SwipeEdgeContext& context) {
  SwipeEdgeActions allowed;
  if (context.library_open) {
    allowed.library = true;
  }
  if (!context.edge_actions || context.count == 0 ||
      context.index >= context.count) {
    return allowed;
  }
  // A swipe towards the Library goes to the next Space when the Library is on
  // the side the next Space comes from: then the Library is past the last
  // Space, else past the first (Zen: the first, with the Library on the left).
  const bool library_past_last = context.library_on_screen_right != context.rtl;
  const size_t last = context.count - 1;
  const size_t library_end = library_past_last ? last : 0;
  const size_t add_space_end = library_past_last ? 0 : last;
  if (context.index == library_end) {
    allowed.library = true;
  }
  if (context.index == add_space_end && !context.creating_space) {
    allowed.add_space = true;
  }
  return allowed;
}

SwipeEdgeAction InitialSwipeEdgeAction(bool library_open) {
  return library_open ? SwipeEdgeAction::kLibrary : SwipeEdgeAction::kNone;
}

SwipeEdgeAction DecideSwipeEdgeAction(const SwipeEdgeActions& allowed,
                                      float amount,
                                      bool library_on_screen_right) {
  if (amount == 0) {
    return SwipeEdgeAction::kNone;
  }
  const bool towards_library =
      SwipeTowardsLibrary(amount, library_on_screen_right);
  if (allowed.library && towards_library) {
    return SwipeEdgeAction::kLibrary;
  }
  if (allowed.add_space && !towards_library) {
    return SwipeEdgeAction::kAddSpace;
  }
  return SwipeEdgeAction::kNone;
}

float SwipeRubberBand(float offset, float dimension, float constant) {
  if (offset == 0 || dimension == 0) {
    return 0;
  }
  const float damped =
      dimension * (1 - std::exp(-(std::abs(offset) * constant) / dimension));
  return offset < 0 ? -damped : damped;
}

float LibrarySwipeProgress(float start,
                           float translate,
                           float strip_width,
                           bool library_on_screen_right) {
  if (strip_width <= 0) {
    return start;
  }
  // The strip moving towards the Library pulls it in.
  const float towards = library_on_screen_right ? -translate : translate;
  return Damp(start + towards / strip_width * kLibrarySwipeFull);
}

bool LibrarySwipeOpens(float amount, bool library_on_screen_right) {
  return SwipeTowardsLibrary(amount, library_on_screen_right);
}

float AddSpaceSwipeProgress(float amount, bool library_on_screen_right) {
  // Away from the Library.
  const float away = library_on_screen_right ? amount : -amount;
  return Damp(away / kAddSpaceThreshold);
}

bool AddSpaceSwipeSucceeds(float progress) {
  return progress >= 1;
}

bool AddSpaceSwipeCrossedReady(float from, float to) {
  return (from < 1) != (to < 1);
}

float AddSpaceSwipeEntry(float progress) {
  return std::clamp(progress / kAddSpaceEntryProgress, 0.f, 1.f);
}

float AddSpaceSwipeArc(float progress) {
  const float x = std::clamp(progress, 0.f, 1.f);
  return -(std::cos(kPi * x) - 1) / 2;
}

float AddSpaceSwipeSpaceTranslate(float progress,
                                  bool library_on_screen_right) {
  // Towards the Library, away from the badge.
  return progress * kAddSpaceSpacesTranslation *
         (library_on_screen_right ? -1.f : 1.f);
}

}  // namespace koan
