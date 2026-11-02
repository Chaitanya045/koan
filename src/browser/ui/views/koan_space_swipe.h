// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "ui/events/event.h"

namespace koan {

class KoanWorkspaceController;

// Zen's two-finger swipe between Spaces (ZenSpacesSwipe): the Space follows
// the fingers across the sidebar, and lifting them switches Space or lets it
// spring back (KoanWorkspaceController::UpdateSwipe(), EndSwipe()). It
// reads the touchpad's scroll events, with their phases and momentum as macOS
// sends them; the pointer over a page isn't here, so the page keeps
// Chromium's swipe back and forward.
class KoanSpaceSwipe {
 public:
  explicit KoanSpaceSwipe(KoanWorkspaceController* controller);
  KoanSpaceSwipe(const KoanSpaceSwipe&) = delete;
  KoanSpaceSwipe& operator=(const KoanSpaceSwipe&) = delete;
  ~KoanSpaceSwipe();

  // Whether `event`, a scroll event over the sidebar, belongs to a
  // horizontal swipe: the caller then handles it (nothing else, like
  // upstream's own swipe, sees it). Mouse wheels aren't swipes.
  bool OnScrollEvent(const ui::ScrollEvent& event);

 private:
  enum class State {
    // Between gestures.
    kIdle,
    // A gesture that hasn't gone far enough to tell which way it goes.
    kUndecided,
    // A horizontal swipe, following the fingers.
    kSwiping,
    // The swipe ended (fingers lifted); the momentum that follows is its.
    kEnded,
    // Anything else (scrolling the tabs): not ours.
    kPassthrough,
  };

  void Reset();

  const raw_ptr<KoanWorkspaceController> controller_;
  State state_ = State::kIdle;
  float total_x_ = 0;
  float total_y_ = 0;
  // Pixels per second along x, smoothed over the last events.
  float velocity_x_ = 0;
  base::TimeTicks last_event_time_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
