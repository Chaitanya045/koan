// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_FEATURE_OVERRIDES_H_
#define KOAN_BROWSER_KOAN_FEATURE_OVERRIDES_H_

namespace variations {
class FeatureOverrides;
}

namespace koan {

// Changes the default state of upstream features Koan builds on (e.g. tab
// group focus mode for workspaces). These rank like built-in defaults:
// --enable-features / --disable-features and chrome://flags still win.
// Called from ChromeBrowserFieldTrials::RegisterFeatureOverrides().
void RegisterFeatureOverrides(variations::FeatureOverrides& overrides);

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_FEATURE_OVERRIDES_H_
