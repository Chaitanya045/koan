// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/types/expected.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

// The containers' lifecycle API. Agent control opens tabs through
// koan::agent::AgentTabOpener (koan/browser/agent/koan_agent_tab_hooks.h),
// which the containers implement (InstallAgentTabOpener(), koan_containers.h)
// on top of the functions below; it ends a task with EndAgentTask().
//
// What gives an agent somewhere to work
// (.agent/notes/agent-control.md section 7): an agent container, shown as a
// Space in the user's own sidebar, in the user's last active window.
//
//   auto container = koan::containers::StartAgentContainer(
//       profile, {.mode = AgentMode::kThrowaway, .space_name = u"Claude
//       Code"});
//   content::WebContents* tab = koan::containers::OpenAgentTab(
//       profile, *container, GURL("https://example.com"));
//   ...
//   koan::containers::EndAgentContainer(profile, *container);
//
// Every tab opened here is in the background: the active tab, the focused
// window and the frontmost app never change. Pages the agent opens from these
// tabs (links, window.open, target=_blank) stay in the container, also in the
// background. Agent containers block permissions, passwords, autofill,
// extensions, history and sync (koan_agent_blocks.h).
//
// The "your tab" mode (a lease on one of the user's tabs) is not a container
// and is not here: see the lease in agent-control.md section 8.
namespace koan::containers {

enum class AgentMode {
  // An in-memory container, new for this task and cleared when it ends. The
  // default.
  kThrowaway,
  // A named container on disk that keeps its cookies between tasks. Another
  // task gets it only if the user names it (`saved_name`).
  kSaved,
  // A separate hidden profile (not implemented yet: StartAgentContainer()
  // returns kNotSupported).
  kStrict,
};

struct AgentContainerOptions {
  AgentMode mode = AgentMode::kThrowaway;
  // kSaved: the saved container's name. An unknown name creates one.
  std::string saved_name;
  // The Space's name in the sidebar ("Claude Code · task").
  std::u16string space_name;
};

enum class AgentContainerError {
  // No window of `profile` with Spaces to show the container in, and none
  // could be opened. Spaces must be on (koan::IsWorkspacesEnabled()).
  kNoSpaces,
  // An off-the-record, guest or system profile: agents never work there.
  kProfileNotAllowed,
  // The mode is not available yet (kStrict).
  kNotSupported,
  // kSaved without a name.
  kInvalidName,
};

// Starts a container for an agent's task and its Space. The Space shows once
// the first tab opens in it.
base::expected<ContainerId, AgentContainerError> StartAgentContainer(
    Profile* profile,
    const AgentContainerOptions& options);

// Opens `url` in a new background tab of `container`'s Space (creating the
// Space with the container's first tab). Returns the tab, or null if
// `container` isn't a started agent container of `profile` or `url` isn't one
// an agent may open (http, https, about:blank).
content::WebContents* OpenAgentTab(Profile* profile,
                                   const ContainerId& container,
                                   const GURL& url);

// The open tabs of `container`, in every window of `profile`.
std::vector<content::WebContents*> GetContainerTabs(
    Profile* profile,
    const ContainerId& container);

// Ends a task agent control opened tabs for (AgentTabRequest's client and
// task, through koan::agent::GetAgentTabOpener()): as EndAgentContainer() for
// the container the task got. Nothing if it has none.
void EndAgentTask(const std::string& client_id, const std::string& task);

// Ends the task: closes the container's tabs (its Space goes with its last
// tab) and, for a throwaway container, clears all its data. A saved container
// keeps its data for the next task that names it.
void EndAgentContainer(Profile* profile, const ContainerId& container);

// The user's saved agent containers, by name (Settings > Agent control lists
// them; deleting one clears its data and removes it from disk).
struct SavedContainer {
  std::string name;
  ContainerId id;
};
std::vector<SavedContainer> GetSavedAgentContainers(Profile* profile);
void DeleteSavedAgentContainer(Profile* profile, const std::string& name);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_
