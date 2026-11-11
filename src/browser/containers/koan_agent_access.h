// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_H_

#include <string>

#include "koan/browser/containers/koan_agent_access_prefs.h"

class PrefService;

namespace content {
class BrowserContext;
class RenderFrameHost;
class RenderProcessHost;
class WebContents;
}  // namespace content

// The user's "What agents can use" switches (koan_agent_access_prefs.h), as
// every agent block reads them (.agent/notes/agent-control.md section 7.2):
// a block applies only to an agent tab, and only while the user has turned
// its switch off. Blocks without a switch (site permissions, pickers,
// passkeys, downloads, open-tab sync, ...) keep calling IsAgent*()
// (koan_container_partition.h) directly.
namespace koan::containers {

// Turns every switch off in `prefs`: a strict task's hidden profile, which
// shares nothing of the user's.
void DenyAllAgentAccess(PrefService* prefs);

// Whether the user lets agents use `capability`. Fails closed: false without
// prefs, or if the switch isn't registered.
bool AgentMay(const PrefService* prefs, AgentCapability capability);
bool AgentMay(content::BrowserContext* context, AgentCapability capability);

// Whether a take-over is limited to the tab's site (kAgentLimitTakeOverToSite).
// Fails closed: true without prefs, or if the pref isn't registered.
bool LimitsTakeOverToSite(const PrefService* prefs);
bool LimitsTakeOverToSite(content::BrowserContext* context);

// The hooks. Whether to block `capability` for:
// - a tab: IsAgentContents() (an agent's own tab, or a user's tab an agent
//   leases) and the switch is off;
bool BlocksAgent(const content::WebContents* contents,
                 AgentCapability capability);
// - a frame: IsAgentTabFrame() (in an agent container, or in an agent's own
//   tab in the user's partition) and the switch is off;
bool BlocksAgentFrame(content::RenderFrameHost* frame,
                      AgentCapability capability);
// - a renderer process: IsAgentProcess() (an agent container's) and the
//   switch is off. An agent tab in the user's partition shares the user's
//   processes, so what is decided per process (extensions' content scripts)
//   can't be blocked there.
bool BlocksAgentProcess(content::RenderProcessHost* process,
                        AgentCapability capability);

// The approval prompt's sentence on what an agent's tabs get, from `prefs`
// (the user's profile) and whether strict mode is on.
std::u16string DescribeAgentAccess(const PrefService* prefs, bool strict);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_AGENT_ACCESS_H_
