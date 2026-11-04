// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_

#include "base/memory/raw_ptr.h"
#include "koan/browser/ui/views/koan_swipe_tracker.h"
#include "ui/events/event.h"

namespace koan {

class KoanWorkspaceController;

// KoanWorkspaceController's side of Zen's two-finger swipe between Spaces
// (koan_swipe_tracker.h): translates the touchpad's ui::ScrollEvents.
class KoanSpaceSwipe final : public KoanSwipeTracker::Delegate {
 public:
  explicit KoanSpaceSwipe(KoanWorkspaceController* controller);
  KoanSpaceSwipe(const KoanSpaceSwipe&) = delete;
  KoanSpaceSwipe& operator=(const KoanSpaceSwipe&) = delete;
  ~KoanSpaceSwipe();

  // Whether `event`, a touchpad scroll over the sidebar, belongs to a
  // horizontal swipe (see KoanSwipeTracker::OnScroll()). Mouse wheels aren't
  // swipes. A swipe that starts where the controller can't follow it (one
  // Space, another Space still sliding in) is still taken: left to upstream's
  // swipe controller it would switch Space, twice.
  bool OnScrollEvent(const ui::ScrollEvent& event);

  // Forgets the gesture in progress (see KoanSwipeTracker::Cancel()).
  void Cancel() { tracker_.Cancel(); }

  // KoanSwipeTracker::Delegate:
  void UpdateSwipe(float amount) override;
  void EndSwipe(float amount, float velocity) override;

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
  KoanSwipeTracker tracker_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
