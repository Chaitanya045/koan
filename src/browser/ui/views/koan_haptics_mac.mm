// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   src/zen/toolkit/common/cocoa/ZenHapticFeedback.mm
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_haptics.h"

#import <AppKit/AppKit.h>

#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"

namespace koan {

void PlayHapticFeedback() {
  // Zen's zen.haptic-feedback.enabled (ZenCommonUtils.cpp checks it first).
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || !local_state->GetBoolean(prefs::kHapticFeedback)) {
    return;
  }
  [NSHapticFeedbackManager.defaultPerformer
      performFeedbackPattern:NSHapticFeedbackPatternAlignment
             performanceTime:NSHapticFeedbackPerformanceTimeDefault];
}

}  // namespace koan
