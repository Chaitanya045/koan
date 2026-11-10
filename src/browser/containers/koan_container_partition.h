// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PARTITION_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_PARTITION_H_

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
// - User containers, for Zen's containers per Space (#111). Nothing is blocked
//   in them.
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
// downloads, history and the omnibox, open-tab sync, WebAuthn.
bool IsAgentProcess(content::RenderProcessHost* process);
bool IsAgentFrame(content::RenderFrameHost* frame);
// Also true for a user's own tab while an agent holds a lease on it (a
// take-over, SetAgentLeased()): its password, autofill, history, downloads and
// sync blocks apply there too, so the user's saved password is never filled
// into a page the agent can read. (Permissions and extensions, keyed on the
// process, stay the user's in their own tab.)
bool IsAgentContents(const content::WebContents* contents);

// Whether `contents` itself is in an agent container: false for a user's tab
// an agent leases. For surfaces that list the agent's own tabs (Recently
// Closed, tab resumption), where a leased tab is still the user's.
bool IsAgentContainerContents(const content::WebContents* contents);

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

// The container `contents` is in, or nullopt.
std::optional<ContainerId> ContainerOf(const content::WebContents* contents);

// The session form of a ContainerId ("agent:t..." / "user:s...") and back.
// Parse() returns nullopt for anything else.
std::string Serialize(const ContainerId& container);
std::optional<ContainerId> Parse(std::string_view text);

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

// TabLifecycleUnit::FinishDiscard(): the SiteInstance for the WebContents that
// replaces a discarded `old_contents`, so the tab reloads in its container;
// null when it isn't in one.
scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents);

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
