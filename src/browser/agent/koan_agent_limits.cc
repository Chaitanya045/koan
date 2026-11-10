// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_limits.h"

#include <algorithm>

#include "base/values.h"

namespace koan::agent {

int ReadLimit(const base::Value* stored, const LimitSpec& spec) {
  if (!stored || !stored->is_int()) {
    return spec.default_value;
  }
  const int value = stored->GetInt();
  if (value < spec.min || value > spec.max || value <= 0) {
    return spec.default_value;
  }
  return value;
}

bool ReadSwitch(const base::Value* stored, bool default_value) {
  return stored && stored->is_bool() ? stored->GetBool() : default_value;
}

int EffectiveLeaseLimit(int setting,
                        bool halve_in_low_power,
                        PowerState power) {
  const int limit = std::max(setting, 1);
  if (!halve_in_low_power || !power.saving()) {
    return limit;
  }
  return std::max(limit / 2, 1);
}

}  // namespace koan::agent
