// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_CAPTURE_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_CAPTURE_H_

#include <memory>

#include "ui/events/event.h"
#include "ui/gfx/native_ui_types.h"

namespace koan {

// Lets the owner of a touchpad gesture have all of its scroll events, wherever
// the system delivers them.
//
// On macOS the Library's swipe shows the Library's window (a child window of
// the browser's) under the fingers while the gesture goes on, and what follows
// went to the page instead of the sidebar that began it (#453: with the
// sidebar on the right the first swipe never opened the Library). Where AppKit
// sends the rest of a gesture whose window order changed is not something to
// depend on, so the events are taken before AppKit routes them, from an
// application-wide NSEvent monitor, for the browser window and its child
// windows. Elsewhere the events always reach the sidebar, and there is no
// capture (Create() returns null).
class KoanScrollCapture {
 public:
  class Delegate {
   public:
    // Whether a gesture is owned: asked for every scroll event of the
    // application, so cheap.
    virtual bool IsCapturing() const = 0;
    // Takes `event`, a touchpad scroll (a mouse wheel is never offered);
    // returns whether it did, and then nothing else sees it.
    virtual bool TakeScroll(const ui::ScrollEvent& event) = 0;

   protected:
    ~Delegate() = default;
  };

  // Captures scroll events sent to `window` or its child windows. `delegate`
  // outlives the capture.
  static std::unique_ptr<KoanScrollCapture> Create(gfx::NativeWindow window,
                                                   Delegate* delegate);

  KoanScrollCapture(const KoanScrollCapture&) = delete;
  KoanScrollCapture& operator=(const KoanScrollCapture&) = delete;
  virtual ~KoanScrollCapture() = default;

 protected:
  KoanScrollCapture() = default;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_CAPTURE_H_
