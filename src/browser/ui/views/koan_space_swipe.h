// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "koan/browser/ui/views/koan_scroll_capture.h"
#include "koan/browser/ui/views/koan_swipe_tracker.h"
#include "ui/events/event.h"
#include "ui/gfx/native_ui_types.h"

namespace koan {

class KoanWorkspaceController;

// KoanWorkspaceController's side of Zen's two-finger swipe between Spaces
// (koan_swipe_tracker.h): translates the touchpad's ui::ScrollEvents, on macOS
// and on Linux (koan_swipe_input.h).
class KoanSpaceSwipe final : public KoanSwipeTracker::Delegate,
                             public KoanScrollCapture::Delegate {
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
  void Cancel() {
    tracker_.Cancel();
    capturing_ = false;
  }

  // The touchpad's travel of one gesture amount, in its scroll offsets: 550 on
  // macOS, LinuxSwipePixelSize() for the Ozone platform on Linux.
  static float PixelSize();

  // The swipe that is under way, and the momentum that follows it, gets all of
  // its scroll events, whichever of `window`'s views or child windows the
  // system sends them to (KoanScrollCapture), until the next swipe begins or
  // Cancel(). For a swipe that puts a window of its own under the fingers.
  void CaptureScrolls(gfx::NativeWindow window);
  void ReleaseScrolls() { capturing_ = false; }

  // KoanSwipeTracker::Delegate:
  void UpdateSwipe(float amount) override;
  void EndSwipe(float amount, float velocity) override;

  // KoanScrollCapture::Delegate:
  bool IsCapturing() const override;
  bool TakeScroll(const ui::ScrollEvent& event) override;

 private:
  // The phase the tracker gets for `event`. On Linux it depends on the events
  // before it (LinuxSwipePhase()): `advance` takes `event` as the next one.
  SwipePhase GetPhase(const ui::ScrollEvent& event, bool advance);

  const raw_ptr<KoanWorkspaceController> controller_;
  KoanSwipeTracker tracker_{this, PixelSize()};
#if BUILDFLAG(IS_LINUX)
  // The last touchpad scroll event was the fingers lifting (or there was none
  // yet).
  bool lifted_ = true;
#endif
  std::unique_ptr<KoanScrollCapture> capture_;
  bool capturing_ = false;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_SWIPE_H_
