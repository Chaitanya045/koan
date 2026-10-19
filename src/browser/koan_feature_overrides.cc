// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/koan_feature_overrides.h"

#include "chrome/browser/ui/ui_features.h"
#include "components/variations/feature_overrides.h"

namespace koan {

void RegisterFeatureOverrides(variations::FeatureOverrides& overrides) {
  // Workspaces are tab groups shown one at a time: upstream's group focus
  // mode hides every other group and scopes tab cycling to the focused one.
  overrides.EnableFeature(features::kTabGroupsFocusing);
}

}  // namespace koan
