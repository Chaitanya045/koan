// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/auto_reset.h"
#include "base/memory/stack_allocated.h"
#include "base/values.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/common/child_process_id.h"
#include "koan/browser/containers/koan_container_partition.h"

namespace content {
class BrowserContext;
class RenderProcessHost;
class WebContents;
}  // namespace content

// Extensions per Space profile (#473,
// .agent/notes/upstream/space-extensions.md): each of the user's containers (a
// Space's profile) has the extensions that are off in it. An extension that is
// off in a profile is never loaded into its renderer processes (no content
// scripts, no extension APIs in its pages), sees none of its requests
// (webRequest, declarativeNetRequest; navigations too), doesn't see its tabs
// (tabs.query, windows' tab lists, tab and webNavigation events, tab objects)
// and can't address them (tab ids, the active tab, capture, the debugger,
// activeTab, its action, commands and context menu items there), and gets
// none of its entries in the stores every profile shares (history, closed
// tabs, top sites, downloads: #544). Default: every extension is on
// everywhere.
//
// This is the part the hooks in //extensions and //chrome/browser/extensions
// call, so it reads the list itself: //base, //components/prefs,
// //components/user_prefs and //content/public/browser only, as the rest of
// :partition. The dialog that edits the list is koan_space_extensions.h.
namespace koan::containers {

// The pref the user's containers are saved in: prefs::kUserContainers
// (koan_prefs.h, which the hooks can't depend on). koan_space_extensions.cc
// static_asserts that the two are the same.
inline constexpr char kUserContainersPref[] = "koan.containers.user";

// The key of a container's entry in that list: the ids of the extensions
// that are off in it.
inline constexpr char kExtensionsOffKey[] = "extensions_off";

// What an extension's call gets for a tab of a profile it is off in, where
// upstream has no "No tab with id" to give (tabs.captureVisibleTab).
inline constexpr char kExtensionOffError[] =
    "The extension is turned off in this tab's profile.";

// --- The list (pure) ---

// The extensions that are off in `container`, per `list` (kUserContainersPref).
// Empty for a container the list doesn't have and for anything but a saved
// user container. Only extension ids (32 letters a-p) count.
std::set<std::string> ExtensionsOffIn(const base::ListValue& list,
                                      const ContainerId& container);
bool IsExtensionOffIn(const base::ListValue& list,
                      const ContainerId& container,
                      std::string_view extension_id);

// Makes `off` (extension ids; others are dropped) the extensions that are off
// in `container`. Returns whether `list` changed: false when it already says
// so, and when the list doesn't have the container.
bool SetExtensionsOff(base::ListValue& list,
                      const ContainerId& container,
                      const std::set<std::string>& off);

// --- The hooks ---

// Whether `extension_id` is off in the partition `config` of `context`: a
// user container's whose list has it. False for the default partition, an
// extension's own and an agent container's (B3 blocks every extension there,
// koan_agent_access.h). No allocation for those.
bool IsExtensionOffInPartition(content::BrowserContext* context,
                               const content::StoragePartitionConfig& config,
                               std::string_view extension_id);

// The same for a renderer process (RendererStartupHelper: whether to load the
// extension into it) and for a tab. Null is not.
bool IsExtensionOffInProcess(content::RenderProcessHost* process,
                             std::string_view extension_id);
bool IsExtensionOffInContents(content::WebContents* contents,
                              std::string_view extension_id);

// `contents`, or null if `extension_id` is off in its profile: what the tabs
// functions that act on the current window's active tab when given no tab id
// (tabs.update, reload, goBack, getZoom, detectLanguage, ...) get for it, so
// they answer "No selected tab" as without one.
content::WebContents* TabUnlessExtensionOff(content::WebContents* contents,
                                            std::string_view extension_id);

// Every extension that is off in `contents`'s profile, for a hook that decides
// later, per extension, about something of this tab (its events, its context
// menu). Empty for a null tab, the default partition and with the flag off.
std::set<std::string> ExtensionsOffInContents(content::WebContents* contents);

// --- webRequest and declarativeNetRequest ---

// The partition of a request, decided once when the request is made
// (WebRequestInfoInitParams, as upstream decides `is_privileged` there): for a
// navigation, the partition of the tab whose frame `navigation_frame` is
// navigating (main frame, subframe, a new tab whose renderer doesn't exist
// yet, prerender, service worker navigation preload: their request has no
// renderer process, `process_id` is invalid); for anything else, the
// partition of the process that made it. Nullopt if that is gone, or is the
// default partition.
std::optional<content::StoragePartitionConfig> PartitionOfRequest(
    content::FrameTreeNodeId navigation_frame,
    content::ChildProcessId process_id);

// Whether `extension_id` is off in `partition` (PartitionOfRequest()) of
// `context`: WebRequestEventRouter::ListenerMatchesRequest() and
// RulesetManager::ShouldEvaluateRulesetForRequest().
bool IsExtensionOffForRequest(
    content::BrowserContext* context,
    const std::optional<content::StoragePartitionConfig>& partition,
    std::string_view extension_id);

// The extension whose API function is running on this thread:
// ExtensionFunction::RunWithValidation() sets it around Run(), so that
// ExtensionTabUtil::GetTabById(), which has no extension parameter, can refuse
// a tab of a profile the caller is off in. Empty for a function no extension
// calls (WebUI). Scopes nest.
class ScopedExtensionCall {
  STACK_ALLOCATED();

 public:
  explicit ScopedExtensionCall(std::string_view extension_id);
  ScopedExtensionCall(const ScopedExtensionCall&) = delete;
  ScopedExtensionCall& operator=(const ScopedExtensionCall&) = delete;
  ~ScopedExtensionCall();

 private:
  const std::string extension_id_;
  base::AutoReset<const std::string*> reset_;
};

// Whether the extension whose function is running (ScopedExtensionCall) is
// off in `contents`'s profile. False outside a function, so browser-internal
// tab lookups are unchanged.
bool IsCallingExtensionOffIn(content::WebContents* contents);

// The extension whose function is running (ScopedExtensionCall), or empty.
std::string_view CallingExtensionId();

// --- The stores every profile shares (#544) ---
//
// History, recently closed tabs, top sites and downloads are the browser
// profile's, shared by every Space profile. What an extension that is off in a
// profile gets of them leaves out that profile's entries: a visit by its tag
// (koan_history_profiles.h), a closed tab by the container in its session data
// (kContainerSessionKey), a download by its partition. The hooks are in
// koan_extension_events.h and koan_extension_history.h.

// The profiles `extension_id` is off in: Serialize()d saved user containers,
// as a Space, a visit's tag and a closed tab's session data name them.
std::set<std::string> ProfilesExtensionIsOffIn(const base::ListValue& list,
                                               std::string_view extension_id);
// The same in `context`. Empty with the flag off.
std::set<std::string> ProfilesExtensionIsOffIn(content::BrowserContext* context,
                                               std::string_view extension_id);

// Whether `extension_id` is off in the profile `session_data` names: a closed
// tab's (sessions::tab_restore::Tab::extra_data). A tab of the default
// partition (no container) has none.
bool IsExtensionOffInSessionData(
    const base::ListValue& list,
    const std::map<std::string, std::string>& session_data,
    std::string_view extension_id);
bool IsExtensionOffInSessionData(
    content::BrowserContext* context,
    const std::map<std::string, std::string>& session_data,
    std::string_view extension_id);

// Every extension that is off in the profile of a visit with `app_id` (its
// tag, VisitRow::app_id). Empty for the default profile's visits.
std::set<std::string> ExtensionsOffForVisit(
    content::BrowserContext* context,
    const std::optional<std::string>& app_id);

// Every extension that is off in the partition `config` of `context`.
std::set<std::string> ExtensionsOffInPartition(
    content::BrowserContext* context,
    const content::StoragePartitionConfig& config);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_
