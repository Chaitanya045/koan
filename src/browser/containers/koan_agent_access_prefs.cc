// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_access_prefs.h"

#include "components/prefs/pref_registry_simple.h"

namespace koan::containers {

void RegisterAgentAccessPrefs(PrefRegistrySimple* registry) {
  for (AgentCapability capability : kAgentCapabilities) {
    registry->RegisterBooleanPref(AgentAccessPref(capability), true);
  }
  registry->RegisterBooleanPref(kAgentLimitTakeOverToSite, false);
}

}  // namespace koan::containers
