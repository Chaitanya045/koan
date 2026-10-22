// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_SWITCHES_H_
#define KOAN_BROWSER_KOAN_SWITCHES_H_

namespace koan::switches {

// Renderer switch: the Glance trigger method from the profile's
// koan.glance.activation_method pref ("alt", "ctrl", "shift" or "meta"), or
// "off" with Glance turned off in Koan's settings. Set by
// koan::AppendRendererSwitches, read by koan::features::
// IsGlanceActivationClick.
inline constexpr char kGlanceActivationMethod[] =
    "koan-glance-activation-method";
inline constexpr char kGlanceOff[] = "off";

// For e2e tests of the per-window session data (koan_session_data.h): each
// window without test data gets a random value of its own under
// koan::kTestWindowDataKey, which a restored or reopened window keeps.
inline constexpr char kTestWindowData[] = "koan-test-window-data";

}  // namespace koan::switches

#endif  // KOAN_BROWSER_KOAN_SWITCHES_H_
