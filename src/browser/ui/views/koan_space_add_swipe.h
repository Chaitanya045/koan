// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenSpaceAddSwipe.mjs, zen-single-components.css
//   (.zen-swipe-add-space-*), zen-animations.css (zen-swipe-add-icon-jump)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ADD_SWIPE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ADD_SWIPE_H_

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

namespace koan {

// Zen's add-Space swipe badge (ZenSpaceAddSwipe): while a swipe goes past the
// Space at the far end from the Library (koan_swipe_edge_actions.h), a round
// badge with a plus comes in from the sidebar's edge on a glow, its ring
// filling as the swipe goes on. When it is ready (the swipe has gone far
// enough to create a Space) the ring fills the badge and the badge jumps.
//
// KoanWorkspaceController lays it over the sidebar's tab list, ignored by its
// layout, and keeps it hidden between swipes. It takes no events.
class KoanSpaceAddSwipeView : public views::View,
                              public gfx::AnimationDelegate {
  METADATA_HEADER(KoanSpaceAddSwipeView, views::View)

 public:
  KoanSpaceAddSwipeView();
  KoanSpaceAddSwipeView(const KoanSpaceAddSwipeView&) = delete;
  KoanSpaceAddSwipeView& operator=(const KoanSpaceAddSwipeView&) = delete;
  ~KoanSpaceAddSwipeView() override;

  // Shows it at progress 0 on the screen's right (with the Library on the
  // left) or left edge.
  void Start(bool on_right);
  // The swipe's progress (AddSpaceSwipeProgress()).
  void SetProgress(float progress);
  float progress() const { return progress_; }
  // The swipe failed: the badge goes back out over `duration`, as Gecko
  // animates the swipe back, then hides.
  void AnimateOut(base::TimeDelta duration);
  // Hides it at once. Safe while its tab strip is being torn down (a swipe
  // let go of because the sidebar went): the view itself only goes invisible
  // in a task after.
  void Stop();

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  bool ready() const;
  void HideIfStopped();
  float RingThickness() const;
  float JumpScale() const;

  // Between Start() and Stop().
  bool shown_ = false;
  bool on_right_ = true;
  float progress_ = 0;

  // The ring's thickness eases to the badge's size and back as it becomes
  // ready or not (Zen: transition 0.2s ease-in-out), and the badge jumps as
  // it becomes ready.
  gfx::LinearAnimation ready_animation_{this};
  float thickness_from_ = 0;
  float thickness_to_ = 0;
  bool jumping_ = false;

  // AnimateOut(): the progress going back to 0 from `out_from_`.
  gfx::LinearAnimation out_animation_{this};
  float out_from_ = 0;

  base::WeakPtrFactory<KoanSpaceAddSwipeView> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_ADD_SWIPE_H_
