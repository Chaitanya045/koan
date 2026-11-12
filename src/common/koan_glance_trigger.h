// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_COMMON_KOAN_GLANCE_TRIGGER_H_
#define KOAN_COMMON_KOAN_GLANCE_TRIGGER_H_

#include <string_view>

#include "base/component_export.h"

namespace koan {

namespace switches {

// Renderer switch: the Glance trigger when the process starts, from the
// profile's koan.glance.activation_method pref ("alt", "ctrl", "shift" or
// "meta"), or "off" with Glance turned off in Koan's settings. Set by
// koan::AppendRendererSwitches(); later changes are pushed
// (koan::UpdateRendererSettings()).
inline constexpr char kGlanceActivationMethod[] =
    "koan-glance-activation-method";
inline constexpr char kGlanceOff[] = "off";

}  // namespace switches

// Which modifier+click opens Glance in a renderer process (Zen's
// zen.glance.activation-method), or none with Glance turned off.
enum class GlanceTrigger { kOff, kAlt, kCtrl, kShift, kMeta };

// Parses the browser's value for a renderer (the switch's, or a pushed one):
// "off", "alt", "ctrl", "shift" or "meta"; anything else is Zen's default,
// Alt.
COMPONENT_EXPORT(KOAN_GLANCE_TRIGGER)
GlanceTrigger ParseGlanceTrigger(std::string_view method);

// Whether a click with these modifiers is `trigger`: its modifier is the only
// one held.
COMPONENT_EXPORT(KOAN_GLANCE_TRIGGER)
bool IsGlanceTriggerClick(GlanceTrigger trigger,
                          bool ctrl,
                          bool shift,
                          bool alt,
                          bool meta);

// The renderer process's trigger: the last one the browser pushed
// (koan/renderer/koan_render_thread_observer.h), or until then the one on the
// command line (koan::switches::kGlanceActivationMethod). One copy per
// process: this is a component, as blink core (the reader) and chrome's
// renderer code (the writer) are different components in component builds.
COMPONENT_EXPORT(KOAN_GLANCE_TRIGGER) GlanceTrigger GetGlanceTrigger();
COMPONENT_EXPORT(KOAN_GLANCE_TRIGGER) void SetGlanceTrigger(GlanceTrigger);

}  // namespace koan

#endif  // KOAN_COMMON_KOAN_GLANCE_TRIGGER_H_
