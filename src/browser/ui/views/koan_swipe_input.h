// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains values from Gecko, Copyright the Mozilla contributors, MPL-2.0:
//   modules/libpref/init/StaticPrefList.yaml (widget.swipe.pixel-size off
//   macOS, apz.gtk.pangesture.pixel_delta_mode_multiplier)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_INPUT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_INPUT_H_

#include "koan/browser/ui/views/koan_swipe_tracker.h"
#include "ui/events/event_constants.h"
#include "ui/events/types/event_type.h"

namespace koan {

// How each platform's touchpad scroll events (ui::ScrollEvent) feed the swipe
// tracker (koan_swipe_tracker.h). Plain functions, so that the mappings can be
// tested without a window.

// macOS: ui::ScrollEvent carries no scroll phase there (it stays kNone, see the
// TODO in ui/events/event.cc); macOS puts the stream's phases in the momentum
// phase instead (ui/events/cocoa/events_mac.mm): MAY_BEGIN as the fingers go
// down, NONE as they move, END at the lift, INERTIAL_UPDATE for the system's
// momentum and END again when it is over.
SwipePhase MacSwipePhase(ui::EventMomentumPhase momentum_phase);

// Linux, Wayland and X11 alike: there are no phases (momentum and scroll phase
// stay NONE and kNone), only the event's type.
//
//   Wayland (ui/ozone/platform/wayland/host/wayland_event_source.cc): each
//   pointer frame of a finger (or continuous) axis source is a kScroll with
//   its offsets; the lift (axis_stop with nothing else in the frame) is a
//   kScrollFlingStart carrying the fling's velocity; fingers touching the
//   touchpad again while a fling is still on (a hold gesture, or the next
//   gesture's first axis frame) send a kScrollFlingCancel first. So every
//   gesture after the first begins with a kScrollFlingCancel.
//
//   X11 (ui/events/x/x11_event_translation.cc): XI2 smooth scrolling from a
//   touchpad is a kScroll per motion event; the zero offsets the driver sends
//   at the lift become a kScrollFlingStart with no velocity. Nothing marks a
//   gesture's beginning: the first kScroll after a lift is one.
//
// A mouse wheel comes as a ui::MouseWheelEvent on both, never as one of these.
// Chromium makes no momentum stream of its own for the browser's views, so a
// swipe ends at the lift.
//
// Returns the phase of an event of `type`, given whether the fingers were
// lifted (the last such event was a lift: true before the first event), and
// updates `lifted`:
//
//   kScrollFlingCancel        kBegin    (fingers down, a fling stopped)
//   kScroll after a lift      kBegin    (X11's beginning, or Wayland's first)
//   kScroll                   kUpdate
//   kScrollFlingStart         kEnd      (fingers up)
SwipePhase LinuxSwipePhase(ui::EventType type, bool& lifted);

// The Ozone platform a Linux browser runs on.
enum class LinuxScrollBackend { kWayland, kX11 };

// The travel of one gesture amount on Linux, in Chromium's touchpad scroll
// offsets on `backend`. Gecko on GTK (what Zen runs on) counts a whole swipe as
// widget.swipe.pixel-size, 1100 (not macOS's 550), of GTK's smooth scroll
// deltas times apz.gtk.pangesture.pixel_delta_mode_multiplier, 40: 27.5 GTK
// scroll units. One unit is, in Chromium's offsets:
//
//   Wayland  120: the compositor's 10 per wheel click, scaled to
//            ui::MouseWheelEvent::kWheelDelta (WaylandPointer::OnAxis(); GTK
//            divides by 10), so 3300.
//   X11      53: one increment of the scroll valuator (GTK's unit), times
//            Chromium's kWheelScrollAmount (events_x_utils.cc), so 1457.5.
float LinuxSwipePixelSize(LinuxScrollBackend backend);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SWIPE_INPUT_H_
