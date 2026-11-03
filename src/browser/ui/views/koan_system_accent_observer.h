// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_OBSERVER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_OBSERVER_H_

#include "base/functional/callback.h"

namespace koan {

// Runs `callback` on the UI thread whenever the system's accent colour may
// have changed (macOS: NSSystemColorsDidChangeNotification, posted when the
// user picks another accent colour in System Settings). Lasts for the
// process. Does nothing where the platform has no such notification.
void ObserveSystemAccentChanges(base::RepeatingClosure callback);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_ACCENT_OBSERVER_H_
