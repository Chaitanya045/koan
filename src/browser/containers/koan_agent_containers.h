// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
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
//   koan::containers::StartAgentContainer(
//       profile, {.space_name = u"Claude Code"},
//       base::BindOnce([](AgentContainerResult container) {
//         if (!container.has_value()) return;
//         EnsureAgentWindow(container->profile.get(), base::BindOnce(...));
//         // then:
//         content::WebContents* tab = koan::containers::OpenAgentTab(
//             container->profile.get(), container->id,
//             GURL("https://example.com"));
//       }));
//   ...
//   koan::containers::EndAgentContainer(profile, id);
//
// Every tab opened here is in the background: the active tab, the focused
// window and the frontmost app never change. Pages the agent opens from these
// tabs (links, window.open, target=_blank) stay in the container, also in the
// background. Agent tabs block what the user turned off in "What agents can
// use" (passwords, autofill, extensions, history: koan_agent_access.h) and,
// in a container, site permissions; open-tab sync is blocked in every agent
// tab, and pickers and passkeys too, except that while an agent holds a tab
// outside a container they wait for the user (agent-control.md sections 7.2
// and 9.4).
//
// The "your tab" mode (a lease on one of the user's tabs) is not a container
// and is not here: see the lease in agent-control.md section 8.
namespace koan::containers {

enum class AgentMode {
  // An in-memory container, new for this task and cleared when it ends. The
  // default.
  kThrowaway,
  // A named container on disk that keeps its cookies between tasks. The user
  // makes it in Settings, and agent control hands it only to a client the
  // user granted it to (koan_container_grants.h).
  kSaved,
  // A throwaway agent container in a profile of its own: a new hidden one
  // (ephemeral and omitted, so no profile list, picker or menu shows it),
  // deleted when the task ends. Nothing of the user's profile is shared
  // (settings, extensions, history, permissions). Settings > Agent Control >
  // Strict (prefs::kAgentControlStrict) makes it the default for tasks that
  // ask for no saved container.
  kStrict,
  // No container: the task's tabs open in the user's own partition, with
  // their cookies and sessions, and are marked as the agent's own
  // (MarkAgentTab(), koan_containers.h), so they still get the agent's Space,
  // lease, indicator, session and end-of-task handling. "What agents can
  // use" > Your logins (koan_agent_access_prefs.h, on by default) makes it
  // the default for tasks that ask for no saved container, unless strict
  // mode is on. The id is the task's, and no partition has it.
  kUserPartition,
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
  // kStrict: the hidden profile could not be created.
  kProfileFailed,
  // kSaved without a name.
  kInvalidName,
};

// A started container and the profile it is in: `profile` itself, or for
// kStrict the task's hidden profile (gone once the task ends).
struct AgentContainer {
  base::WeakPtr<Profile> profile;
  ContainerId id;
};
using AgentContainerResult =
    base::expected<AgentContainer, AgentContainerError>;

// Starts a container for an agent's task, in `profile` (the user's) or, for
// kStrict, in a new hidden profile, and runs `done` with it, always
// asynchronously. The Space shows once the first tab opens in it.
void StartAgentContainer(Profile* profile,
                         const AgentContainerOptions& options,
                         base::OnceCallback<void(AgentContainerResult)> done);

// Makes sure `profile` has a normal window with Spaces for agents' tabs,
// opening one in the background if not (never activated, behind the window
// the user last used), then runs `ready`, always asynchronously. Requests
// that come while a window is opening wait for the same one.
void EnsureAgentWindow(Profile* profile, base::OnceClosure ready);

// Opens `url` in a new background tab of `container`'s Space (creating the
// Space with the container's first tab): in the container, or for a
// kUserPartition task in the user's partition. Returns the tab, or null if
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
// keeps its data for the next task that names it. A strict task's hidden
// profile closes its windows and lets go of the profile, which Chromium then
// deletes (an ephemeral profile goes with its last keep-alive; one left by a
// crash goes at the next start).
void EndAgentContainer(Profile* profile, const ContainerId& container);

// The user's saved agent containers, by name (Settings > Agent Control lists
// them; deleting one clears its data, its grants
// (koan_container_grants.h) and removes it from disk).
struct SavedContainer {
  std::string name;
  ContainerId id;
};
std::vector<SavedContainer> GetSavedAgentContainers(Profile* profile);
void DeleteSavedAgentContainer(Profile* profile, const std::string& name);

// Settings > Agent Control's "Add": a new, empty saved container named
// `name`. Only the user makes saved containers; an agent's request for an
// unknown name opens nothing. False for an invalid or existing name.
bool CreateSavedAgentContainer(Profile* profile, const std::string& name);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_AGENT_CONTAINERS_H_
