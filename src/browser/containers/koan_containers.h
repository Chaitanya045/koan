// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_

#include <memory>
#include <string>
#include <vector>

#include "koan/browser/containers/koan_container_partition.h"

class Profile;

namespace content {
class NavigationEntry;
class WebContents;
}  // namespace content

// The container core's hooks into the rest of the browser (the API agents use
// is koan_agent_containers.h; the identity is koan_container_partition.h).
// Header-only target; the definitions are in :impl, linked through
// //koan/browser/ui/views:impl.
namespace koan::containers {

// KoanWorkspaceController, when a tab enters a window: a container's tab
// records its container in the session, at once, so that a crash can't
// restore it without; any other tab drops a stale record.
void OnTabInserted(content::WebContents* contents);

// The TabRestoreHook for kContainerSessionKey (koan_session_data.cc): a tab
// that was in a container is never loaded in the default partition. A saved
// user container's tab that SiteInstanceForRestoredTab() put back in its
// container keeps its pages. An agent container's tab, a throwaway user
// container's, any tab that isn't in the container its record names, and a
// malformed record all get one about:blank entry instead of their pages, and
// the blank tab closes once it is in its window (unless it is the window's
// only tab).
void OnRestoringContainerTab(
    content::WebContents* contents,
    const std::string& data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation);

// After OnTabInserted(): a tab a page in an agent container's tab opened
// (a link, window.open; still on its first navigation) joins its container's
// Space in that window. It is already in the container (the hooks of
// koan_container_partition.h). A tab the user moves keeps its place.
void PlaceInContainerSpace(content::WebContents* contents);

// Installs the containers as agent control's koan::agent::AgentTabOpener
// (koan/browser/agent/koan_agent_tab_hooks.h). Called by every window with
// Spaces (KoanWorkspaceController); once is enough.
void InstallAgentTabOpener();

// GarbageCollectStoragePartitionsCommand::DoGarbageCollection(), before it
// lists the partitions to keep: loads `profile`'s saved containers, because a
// loaded partition is never collected and nothing else lists Koan's.
void LoadSavedContainers(Profile* profile);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
