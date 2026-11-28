// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "koan/browser/containers/koan_container_partition.h"

class Profile;

namespace content {
class BrowsingDataFilterBuilder;
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

// Marks `contents`, a tab in the user's own partition, as an agent's own tab
// of `task` (SetAgentTab(), koan_container_partition.h): the agent tab opener
// does it for a task with "Your logins" on. The mark then follows the tab's
// pages into the tabs they open (links, window.open, target=_blank, the
// context menu: WebContentsObserver::DidOpenRequestedURL), as a container
// does through its partition, and into the tab that replaces it when it is
// discarded (AboutToBeDiscarded). A tab already in a window records it in the
// session at once (OnTabInserted()), so it is never restored.
void MarkAgentTab(content::WebContents* contents, const ContainerId& task);

// Installs the containers as agent control's koan::agent::AgentTabOpener
// (koan/browser/agent/koan_agent_tab_hooks.h). Called by every window with
// Spaces (KoanWorkspaceController); once is enough.
void InstallAgentTabOpener();

// GarbageCollectStoragePartitionsCommand::DoGarbageCollection(), before it
// lists the partitions to keep: loads `profile`'s saved containers (agents'
// and the user's own, koan_user_containers.h), because a loaded partition is
// never collected and nothing else lists Koan's.
void LoadSavedContainers(Profile* profile);

// --- The user's own containers: Settings > Tab Management > Profiles (#477,
// .agent/notes/upstream/space-containers.md "Managing profiles") ---

// Called by every window with Spaces (KoanWorkspaceController), before its
// tabs restore; once is enough: SetUserContainerCheck() with a check against
// the profile's list (prefs::kUserContainers).
void InstallUserContainerCheck();

// Clear this profile's data: everything `container` (a user container of
// `profile`) holds, of all time (cookies, every kind of site storage, service
// workers, the HTTP, code and HTTP auth caches), and nothing outside its
// partition. Its open tabs stay open. Runs `done` when it is all cleared.
void ClearUserContainerData(Profile* profile,
                            const ContainerId& container,
                            base::OnceClosure done);

// Deleting a profile, once it is out of the list and its tabs are closing:
// clears it as ClearUserContainerData() does if its partition is loaded, then
// deletes its folder (BrowserContext::AsyncObliterateStoragePartition()). A
// folder still in use stays until the next start, when nothing loads it and
// Chromium's storage partition garbage collection deletes it; this asks for
// that collection (prefs::kShouldGarbageCollectStoragePartitions) and writes
// it to disk before deleting anything, so a crash in between leaves the folder
// to the collection too. Runs `done` when the data is gone.
void DeleteUserContainerData(Profile* profile,
                             const ContainerId& container,
                             base::OnceClosure done);

// ChromeBrowsingDataRemoverDelegate::RemoveEmbedderData(), for a removal that
// names no partition (Clear browsing data, the browsingData extension API;
// the Library's Forget About This Site names each partition itself, #474):
// the same removal, for the types kept in a
// partition (cookies, site data, caches), in each of the user's containers,
// as Chromium does for isolated web apps.
void RemoveUserContainersData(Profile* profile,
                              base::Time delete_begin,
                              base::Time delete_end,
                              uint64_t remove_mask,
                              uint64_t origin_type_mask,
                              content::BrowsingDataFilterBuilder& filter);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINERS_H_
