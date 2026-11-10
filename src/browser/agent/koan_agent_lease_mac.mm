// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_lease.h"

#import <Foundation/Foundation.h>

namespace koan::agent {

bool IsLowPowerModeEnabled() {
  return NSProcessInfo.processInfo.lowPowerModeEnabled;
}

}  // namespace koan::agent
