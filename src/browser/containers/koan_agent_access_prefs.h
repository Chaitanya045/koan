// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_PREFS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_PREFS_H_

// What agents may use of the user's browser: Settings > Agent Control > What
// agents can use (.agent/notes/agent-control.md section 7.0). Agents use the
// browser as the user does unless the user turns a part off. Profile prefs,
// all on by default, registered by koan::prefs::RegisterProfilePrefs()
// (through RegisterAgentAccessPrefs(); this target depends on
// //components/prefs only). The blocks read them through
// koan::containers::AgentMay() (koan_agent_access.h).
class PrefRegistrySimple;

namespace koan::containers {

enum class AgentCapability {
  // Agent tabs open in the user's own partition, with their cookies and
  // sessions (still agent tabs: their own Space, lease and indicator). Off:
  // in a container (throwaway, or a saved one). Read when a task starts.
  kLogins,
  // The password manager fills and saves in agent tabs.
  kPasswords,
  // Addresses, cards and autocomplete fill in agent tabs.
  kAutofill,
  // The user's extensions run in agent tabs: content scripts and extension
  // APIs in a container's processes, webRequest, chrome.debugger, tab ids.
  kExtensions,
  // Agent visits go into History, and so into history-based suggestions.
  kHistory,
};

inline constexpr AgentCapability kAgentCapabilities[] = {
    AgentCapability::kLogins,   AgentCapability::kPasswords,
    AgentCapability::kAutofill, AgentCapability::kExtensions,
    AgentCapability::kHistory,
};

inline constexpr char kAgentMayUseLogins[] = "koan.agent_control.use.logins";
inline constexpr char kAgentMayUsePasswords[] =
    "koan.agent_control.use.passwords";
inline constexpr char kAgentMayUseAutofill[] =
    "koan.agent_control.use.autofill";
inline constexpr char kAgentMayUseExtensions[] =
    "koan.agent_control.use.extensions";
inline constexpr char kAgentMayUseHistory[] = "koan.agent_control.use.history";

// The pref of `capability`.
constexpr const char* AgentAccessPref(AgentCapability capability) {
  switch (capability) {
    case AgentCapability::kLogins:
      return kAgentMayUseLogins;
    case AgentCapability::kPasswords:
      return kAgentMayUsePasswords;
    case AgentCapability::kAutofill:
      return kAgentMayUseAutofill;
    case AgentCapability::kExtensions:
      return kAgentMayUseExtensions;
    case AgentCapability::kHistory:
      return kAgentMayUseHistory;
  }
  return kAgentMayUseLogins;
}

// "Limit a taken-over tab to its site", off by default: a user's tab an
// agent took over (after the user's yes) stays driven wherever it goes. On,
// the agent drives it on the site the user was asked about only
// (agent-control.md section 9.2). Read when the user is asked.
inline constexpr char kAgentLimitTakeOverToSite[] =
    "koan.agent_control.limit_take_over_to_site";

// Registers the switches: the AgentCapability ones on, the take-over limit
// off.
void RegisterAgentAccessPrefs(PrefRegistrySimple* registry);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_PREFS_H_
