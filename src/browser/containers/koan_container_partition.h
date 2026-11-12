// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PARTITION_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PARTITION_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "koan/browser/containers/koan_partition_domain.h"

class GURL;

namespace content {
class BrowserContext;
class RenderFrameHost;
class RenderProcessHost;
class SiteInstance;
class StoragePartitionConfig;
class WebContents;
}  // namespace content

// A container is a StoragePartition of its own (cookies, storage, cache and
// renderer processes) inside the user's profile: a tab in one shares nothing
// with the user's other tabs but the profile's settings and services
// (.agent/notes/agent-control.md section 7,
// .agent/notes/upstream/containers.md). Koan has two kinds:
//
// - Agent containers, where agents work. Upstream code asks IsAgent*() below
//   and blocks what an agent must never reach (permissions, passwords,
//   extensions, ...).
// - User containers, for Zen's containers per Space (#318,
//   koan_user_containers.h). Nothing is blocked in them.
//
// The kind and the id are in the partition's domain ("koanagent<id>"), so code
// that has a process, a frame or a partition can tell an agent container
// without asking a service. That is what lets the hooks in components/ and
// extensions/ depend on this target only (//base and //content/public/browser).
// One domain per container is what lets
// BrowserContext::AsyncObliterateStoragePartition() delete exactly one.
//
// This file is the part of the core that needs nothing but content: the
// identity, and the SiteInstances upstream hooks put new tabs in.
namespace koan::containers {

enum class ContainerKind {
  kUser,
  kAgent,
};

// Which container a tab is in.
struct ContainerId {
  ContainerKind kind = ContainerKind::kUser;
  // [a-z]{2,40}, starting with 't' (throwaway) or 's' (saved):
  // NewContainerId() makes one.
  std::string id;

  // Saved containers are on disk; throwaway ones are in memory, gone with the
  // browser (and cleared earlier, when their task ends).
  bool persistent() const { return !id.empty() && id.front() == 's'; }

  friend bool operator==(const ContainerId&, const ContainerId&) = default;
};

// Whether `id` is a valid ContainerId::id.
bool IsValidContainerId(std::string_view id);

// A new random id: 't' or 's', then 26 random letters (about 122 bits).
std::string NewContainerId(bool persistent);

// The partition of `container` in `context`: domain "koanagent<id>" or
// "koanuser<id>", empty name, in memory unless saved.
content::StoragePartitionConfig PartitionConfigFor(
    content::BrowserContext* context,
    const ContainerId& container);

// The container `config` is, or nullopt (the default partition, an extension's,
// an isolated app's, a WebView's, or a Koan domain that doesn't parse).
std::optional<ContainerId> ContainerFromPartitionConfig(
    const content::StoragePartitionConfig& config);

// Whether `config` is an agent container's partition. No allocation.
bool IsAgentPartition(const content::StoragePartitionConfig& config);

// Whether the process, frame or tab is in an agent container. Null is not.
// IsAgentContents() asks the primary main frame, which a tab always has.
// The agent blocks call these from upstream (agent-control.md section 7.2):
// permissions, choosers and pickers, extensions, passwords, autofill,
// downloads, history and the omnibox, open-tab sync, WebAuthn. Those the user
// can switch off in Settings call them through BlocksAgent*()
// (koan_agent_access.h).
bool IsAgentProcess(content::RenderProcessHost* process);
bool IsAgentFrame(content::RenderFrameHost* frame);
// True for an agent's own tab (IsAgentTab()), and for a user's own tab while
// an agent holds a lease on it (a take-over, SetAgentLeased()): its password,
// autofill, history, downloads and sync blocks apply there too. (Permissions
// and extensions, keyed on the process, stay the user's in their own tab.)
bool IsAgentContents(const content::WebContents* contents);

// Whether `contents` is an agent's own tab: in an agent container, or opened
// for an agent in the user's own partition (SetAgentTab(), "Your logins" on).
// False for a user's tab an agent leases. For surfaces that list the agent's
// own tabs (Recently Closed, tab resumption, window sync), where a leased tab
// is still the user's.
bool IsAgentTab(const content::WebContents* contents);

// Whether `frame` is in an agent container (IsAgentFrame()) or in an agent's
// own tab in the user's partition. For the frame-keyed blocks that must also
// hold in the user's partition: pickers, passkeys, protocol handlers, search
// engines.
bool IsAgentTabFrame(content::RenderFrameHost* frame);

// Marks `contents`, a tab in the user's own partition, as an agent's own tab
// of the task whose id is `task` (an agent ContainerId that no partition has:
// ContainerOf() returns it). Called by the agent tab opener for a task with
// "Your logins" on, and for the tabs such a tab opens
// (koan::containers::MarkAgentTab(), koan_containers.h). A tab stays marked
// for life.
void SetAgentTab(content::WebContents* contents, const ContainerId& task);

// Marks `contents`, a user's tab, as leased to an agent, or not. Called by
// agent control's lease (koan_agent_lease.cc) for tabs the agent doesn't own.
void SetAgentLeased(content::WebContents* contents, bool leased);

// Whether the frame, tab or SiteInstance is in a container of either kind.
// Paths that would load its pages in the default partition, with the user's
// cookies, check this: NoStatePrefetch, the loading predictor's prewarming,
// web-app navigation capturing. Null is not.
bool IsContainerFrame(content::RenderFrameHost* frame);
bool IsContainerContents(const content::WebContents* contents);
bool IsContainerSiteInstance(content::SiteInstance* site_instance);

// The container `contents` is in, or for an agent's own tab in the user's
// partition its task's id (SetAgentTab()); otherwise nullopt. What a Space,
// the session and the end of a task group an agent's tabs by.
std::optional<ContainerId> ContainerOf(const content::WebContents* contents);

// The session form of a ContainerId ("agent:t..." / "user:s...") and back.
// Parse() returns nullopt for anything else.
std::string Serialize(const ContainerId& container);
std::optional<ContainerId> Parse(std::string_view text);

// The per-tab session data key (koan_session_data.h) of a container's tab: its
// Serialize()d ContainerId.
inline constexpr char kContainerSessionKey[] = "koan.container";

// --- SiteInstances for new tabs ---

// A SiteInstance in a new BrowsingInstance fixed to `container`'s partition:
// a WebContents created with it, and every navigation in it, stays there.
scoped_refptr<content::SiteInstance> CreateSiteInstance(
    content::BrowserContext* context,
    const ContainerId& container,
    const GURL& url);

// CreateTargetContents() in browser_navigator.cc, for a new tab with no
// opener: if `source` (the tab or frame the navigation came from: Cmd-click,
// middle-click, the context menu's "Open link in new tab") is in a container,
// a SiteInstance for `url` in that container; otherwise null (upstream
// decides).
scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    content::BrowserContext* context,
    content::SiteInstance* source,
    const GURL& url);

// CreateTargetContents() in browser_navigator.cc, for every new tab it makes:
// a tab that an agent's own tab in the user's partition opened (Cmd-click,
// middle-click, the context menu) is the agent's too, marked here before its
// tab helpers are attached (MarkAgentTab() then follows it, from the source's
// DidOpenRequestedURL). Returns `contents`.
std::unique_ptr<content::WebContents> InheritAgentTab(
    const content::WebContents* source,
    std::unique_ptr<content::WebContents> contents);

// TabLifecycleUnit::FinishDiscard(): the SiteInstance for the WebContents that
// replaces a discarded `old_contents`, so the tab reloads in its container;
// null when it isn't in one.
scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents);

// CreateRestoredTab() in browser_tabrestore.cc, before it creates the tab:
// for a saved user container's tab (`extra_data`'s kContainerSessionKey), a
// SiteInstance fixed to that container, so that the tab restores into it
// (#111); null for every other tab. An agent container's tab never comes back
// in its container (OnRestoringContainerTab() in koan_containers.h blanks it).
scoped_refptr<content::SiteInstance> SiteInstanceForRestoredTab(
    content::BrowserContext* context,
    const std::map<std::string, std::string>& extra_data);

// BrowserWebContentsDelegate::IsPrerender2Supported(): whether `contents` may
// prerender. Not in a container: content builds a prerendered page in the
// default partition and swaps it into the tab.
bool AllowsPrerender(const content::WebContents& contents);

// --- Agent blocks (.agent/notes/agent-control.md section 7.2) ---

// DownloadTargetDeterminer: where an agent container's downloads go, a
// folder in the user's download folder (`download_dir`). Created on the
// first download.
base::FilePath AgentDownloadDirectory(const base::FilePath& download_dir);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PARTITION_H_
