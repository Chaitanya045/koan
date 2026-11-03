// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_system_accent_observer.h"

namespace koan {

// Koan ships on macOS; elsewhere the accent is read when the look loads.
void ObserveSystemAccentChanges(base::RepeatingClosure callback) {}

}  // namespace koan
