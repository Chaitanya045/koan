// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains values from Gecko, Copyright the Mozilla contributors, MPL-2.0:
//   modules/libpref/init/StaticPrefList.yaml (widget.swipe.pixel-size off
//   macOS, apz.gtk.pangesture.pixel_delta_mode_multiplier)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_swipe_input.h"

namespace koan {

namespace {

// Gecko off macOS: widget.swipe.pixel-size over
// apz.gtk.pangesture.pixel_delta_mode_multiplier, in GTK scroll units.
constexpr float kGtkUnitsPerAmount = 1100.f / 40.f;

// Chromium's touchpad offsets per GTK scroll unit (see LinuxSwipePixelSize()).
constexpr float kWaylandOffsetPerUnit = 120.f;
constexpr float kX11OffsetPerUnit = 53.f;

}  // namespace

SwipePhase MacSwipePhase(ui::EventMomentumPhase momentum_phase) {
  switch (momentum_phase) {
    case ui::EventMomentumPhase::BEGAN:
    case ui::EventMomentumPhase::MAY_BEGIN:
      return SwipePhase::kBegin;
    case ui::EventMomentumPhase::INERTIAL_UPDATE:
      return SwipePhase::kMomentum;
    case ui::EventMomentumPhase::END:
    case ui::EventMomentumPhase::BLOCKED:
      return SwipePhase::kEnd;
    case ui::EventMomentumPhase::NONE:
      return SwipePhase::kUpdate;
  }
  return SwipePhase::kUpdate;
}

SwipePhase LinuxSwipePhase(ui::EventType type, bool& lifted) {
  switch (type) {
    case ui::EventType::kScrollFlingCancel:
      lifted = false;
      return SwipePhase::kBegin;
    case ui::EventType::kScrollFlingStart:
      lifted = true;
      return SwipePhase::kEnd;
    default: {
      const bool begins = lifted;
      lifted = false;
      return begins ? SwipePhase::kBegin : SwipePhase::kUpdate;
    }
  }
}

float LinuxSwipePixelSize(LinuxScrollBackend backend) {
  switch (backend) {
    case LinuxScrollBackend::kWayland:
      return kGtkUnitsPerAmount * kWaylandOffsetPerUnit;
    case LinuxScrollBackend::kX11:
      return kGtkUnitsPerAmount * kX11OffsetPerUnit;
  }
  return kGtkUnitsPerAmount * kWaylandOffsetPerUnit;
}

void TouchpadWheels::OnScroll(base::TimeTicks time, bool taken) {
  last_scroll_ = time;
  taken_ = taken;
}

TouchpadWheels::Kind TouchpadWheels::Classify(base::TimeTicks time) const {
  if (last_scroll_.is_null() || time != last_scroll_) {
    return Kind::kMouseWheel;
  }
  return taken_ ? Kind::kTakenScroll : Kind::kLeftScroll;
}

}  // namespace koan
