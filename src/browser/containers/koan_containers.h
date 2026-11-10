// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_

#include <memory>
#include <string>
#include <vector>

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

// The per-tab session data key (koan_session_data.h) of a container's tab: its
// Serialize()d ContainerId.
inline constexpr char kContainerSessionKey[] = "koan.container";

// KoanWorkspaceController, when a tab enters a window: a container's tab
// records its container in the session, at once, so that a crash can't
// restore it without; any other tab drops a stale record.
void OnTabInserted(content::WebContents* contents);

// The TabRestoreHook for kContainerSessionKey (koan_session_data.cc): a tab
// that was in a container is never loaded in the default partition. An agent
// container's tab, a container's tab that can't come back in it, and a
// malformed record all get one about:blank entry instead of their pages, and
// the blank tab closes once it is in its window (unless it is the window's
// only tab).
void OnRestoringContainerTab(
    content::WebContents* contents,
    const std::string& data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation);

// GarbageCollectStoragePartitionsCommand::DoGarbageCollection(), before it
// lists the partitions to keep: loads `profile`'s saved containers, because a
// loaded partition is never collected and nothing else lists Koan's.
void LoadSavedContainers(Profile* profile);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
