// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_DIRECTION_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_DIRECTION_H_

#include "ui/events/event.h"

namespace koan {

// Whether `event`'s offsets already go the way the swipe should follow. On
// macOS that is the fingers' way: the offsets come from NSEvent's
// scrollingDelta, which the system turns round when natural scrolling is off
// (-[NSEvent isDirectionInvertedFromDevice] is true when it is on, and the
// offsets then follow the fingers); ui::ScrollEvent does not carry the bit, so
// this asks the native event. True where the event has no native event (tests)
// and off macOS. On Linux the desktop's natural scrolling setting is already in
// the offsets (libinput and the compositor or X driver apply it), and Chromium
// doesn't say which it is (Wayland's axis_relative_direction is not
// implemented), so the swipe takes them as they come and follows the desktop's
// setting: with natural scrolling on the strip moves with the fingers, with it
// off it moves as scrolled content does.
bool ScrollFollowsFingers(const ui::ScrollEvent& event);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SCROLL_DIRECTION_H_
