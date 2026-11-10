// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_limits.h"

#include <vector>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

// T-limits-1.
TEST(KoanAgentLimitsTest, ReadLimitUsesTheStoredValueOnlyInRange) {
  for (const LimitSpec& spec :
       {kLeasedTabsLimit, kPromptsLimit, kFailuresLimit}) {
    for (int value = spec.min; value <= spec.max; ++value) {
      const base::Value stored(value);
      EXPECT_EQ(ReadLimit(&stored, spec), value);
    }
    std::vector<base::Value> bad;
    bad.emplace_back(spec.min - 1);
    bad.emplace_back(spec.max + 1);
    bad.emplace_back(0);
    bad.emplace_back(-3);
    bad.emplace_back(9999);
    bad.emplace_back("4");
    bad.emplace_back(4.0);
    bad.emplace_back(true);
    bad.emplace_back(base::ListValue());
    for (const base::Value& stored : bad) {
      EXPECT_EQ(ReadLimit(&stored, spec), spec.default_value)
          << stored.DebugString();
    }
    EXPECT_EQ(ReadLimit(nullptr, spec), spec.default_value);
  }
  EXPECT_EQ(kLeasedTabsLimit.default_value, 4);
  EXPECT_EQ(kPromptsLimit.default_value, 3);
  EXPECT_EQ(kFailuresLimit.default_value, 5);
}

TEST(KoanAgentLimitsTest, ReadSwitch) {
  const base::Value off(false);
  const base::Value number(0);
  EXPECT_FALSE(ReadSwitch(&off, true));
  EXPECT_TRUE(ReadSwitch(&number, true));
  EXPECT_TRUE(ReadSwitch(nullptr, true));
}

// T-power-2: halved only on battery *and* in Low Power Mode, never to 0.
TEST(KoanAgentLimitsTest, TheLeaseLimitInLowPowerMode) {
  const PowerState saving{.on_battery = true, .low_power_mode = true};
  EXPECT_EQ(EffectiveLeaseLimit(4, true, saving), 2);
  EXPECT_EQ(EffectiveLeaseLimit(4, true, {.on_battery = true}), 4);
  EXPECT_EQ(EffectiveLeaseLimit(4, true, {.low_power_mode = true}), 4);
  EXPECT_EQ(EffectiveLeaseLimit(4, false, saving), 4);
  EXPECT_EQ(EffectiveLeaseLimit(1, true, saving), 1);
  EXPECT_EQ(EffectiveLeaseLimit(5, true, saving), 2);
  EXPECT_EQ(EffectiveLeaseLimit(16, true, saving), 8);
  EXPECT_EQ(EffectiveLeaseLimit(0, true, {}), 1);
}

}  // namespace koan::agent
