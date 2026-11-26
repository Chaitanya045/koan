// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_scroll_capture.h"

#import <AppKit/AppKit.h>

#include "base/apple/owned_objc.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/events/event_utils.h"

namespace koan {

namespace {

class KoanScrollCaptureMac final : public KoanScrollCapture {
 public:
  KoanScrollCaptureMac(NSWindow* window, Delegate* delegate)
      : window_(window), delegate_(delegate) {
    // (Like views::EventMonitorMac: the block checks the weak pointer, as
    // another monitor may delete this while it runs.)
    base::WeakPtr<KoanScrollCaptureMac> weak_this = weak_factory_.GetWeakPtr();
    monitor_ = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                     handler:^NSEvent*(NSEvent* event) {
                                       return weak_this
                                                  ? weak_this->Filter(event)
                                                  : event;
                                     }];
  }

  ~KoanScrollCaptureMac() override { [NSEvent removeMonitor:monitor_]; }

 private:
  // Returns the event, or nil if the delegate took it.
  NSEvent* Filter(NSEvent* event) {
    NSWindow* window = window_;
    NSWindow* source = event.window;
    if (!window || !source ||
        (source != window && source.parentWindow != window) ||
        !delegate_->IsCapturing()) {
      return event;
    }
    ui::ScrollEvent scroll((base::apple::OwnedNSEvent(event)));
    // A mouse wheel has ticks; the touchpad's scrolling doesn't.
    if (scroll.type() != ui::EventType::kScroll ||
        !ui::GetMouseWheelTick120ths(scroll.native_event()).IsZero()) {
      return event;
    }
    if (delegate_->TakeScroll(scroll)) {
      return nil;
    }
    return event;
  }

  NSWindow* __weak window_ = nil;
  const raw_ptr<Delegate> delegate_;
  id __strong monitor_ = nil;
  base::WeakPtrFactory<KoanScrollCaptureMac> weak_factory_{this};
};

}  // namespace

// static
std::unique_ptr<KoanScrollCapture> KoanScrollCapture::Create(
    gfx::NativeWindow window,
    Delegate* delegate) {
  NSWindow* ns_window = window.GetNativeNSWindow();
  // (A PWA's window lives in its own process: nothing to monitor here.)
  if (!ns_window) {
    return nullptr;
  }
  return std::make_unique<KoanScrollCaptureMac>(ns_window, delegate);
}

}  // namespace koan
