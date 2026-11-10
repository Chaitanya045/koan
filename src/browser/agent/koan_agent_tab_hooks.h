// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}

// The calls upstream code makes into agent control
// (.agent/notes/agent-control.md section 14, hook #2), kept in a target that
// depends on //content only so that the tab delegate can use it. The browser
// side (AgentLeaseManager) installs the delegate; with none installed every
// call does nothing.
namespace koan::agent {

class TabHooksDelegate {
 public:
  virtual ~TabHooksDelegate();

  // Whether an agent is attached to `contents` (it holds a lease).
  virtual bool IsAgentTab(content::WebContents* contents) = 0;
  // A page in the agent tab `source` opened `opened` (window.open, a
  // target=_blank link, a popup): it joins the same agent and task.
  virtual void OnOpenedFromAgentTab(content::WebContents* source,
                                    content::WebContents* opened) = 0;
};

// How agent control opens a tab for an agent (Koan.openTab, a task's first
// tab). The agent containers (storage-partition containers in the user's
// profile, a separate change) implement it; agent control only codes against
// this. The tab must open in the background, never activated, never raising
// or creating a focused window (T-focus-1, T-win-1, T-win-2), in the original
// profile (never Incognito).
struct AgentTabRequest {
  std::string client_id;
  // The task, which names the agent container ("Throwaway" by default).
  std::string task;
  // The saved agent container the agent asked for by name, or empty for a
  // throwaway one. The tab opens in it only if the user granted it to
  // `client_id` (Settings > Agent Control, koan_container_grants.h);
  // otherwise no tab opens.
  std::string saved_container;
  // Already checked with IsAllowedNavigationUrl().
  GURL url;
};

class AgentTabOpener {
 public:
  virtual ~AgentTabOpener();

  // Runs `done` with the new tab, or null if none could be opened.
  virtual void OpenAgentTab(
      const AgentTabRequest& request,
      base::OnceCallback<void(content::WebContents*)> done) = 0;
};

// Installs or (with null) removes the opener; null until the containers
// install one, and Koan.openTab answers Koan.unavailable meanwhile. Not owned.
void SetAgentTabOpener(AgentTabOpener* opener);
AgentTabOpener* GetAgentTabOpener();

// Installs or (with null) removes the delegate. Not owned.
void SetTabHooksDelegate(TabHooksDelegate* delegate);

// Design section 9.4: what a page in an agent tab opens goes to a background
// tab, never a foreground tab, a popup window or a new window, so nothing
// takes the user's focus. Changes `*disposition` and returns true when it did.
// BrowserWebContentsDelegate::AddNewContents and OpenURLFromTab call it.
bool OpensInBackgroundForAgent(content::WebContents* source,
                               WindowOpenDisposition* disposition);

// Tells agent control that `opened` came from the agent tab `source`. Does
// nothing if `source` is not an agent tab or `opened` is null.
void OnOpenedFromAgentTab(content::WebContents* source,
                          content::WebContents* opened);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_
