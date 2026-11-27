// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_

#include <set>
#include <string>
#include <string_view>

#include "base/auto_reset.h"
#include "base/memory/stack_allocated.h"
#include "base/values.h"
#include "content/public/common/child_process_id.h"
#include "koan/browser/containers/koan_container_partition.h"

namespace content {
class BrowserContext;
class RenderProcessHost;
class StoragePartitionConfig;
class WebContents;
}  // namespace content

// Extensions per Space profile (#473,
// .agent/notes/upstream/space-extensions.md): each of the user's containers (a
// Space's profile) has the extensions that are off in it. An extension that is
// off in a profile is never loaded into its renderer processes (no content
// scripts, no extension APIs in its pages), sees none of its requests
// (webRequest, declarativeNetRequest), and can't address its tabs (tab ids,
// capture, the debugger). Default: every extension is on everywhere.
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
// extension into it; webRequest and declarativeNetRequest: a request's
// process) and for a tab. Null is not.
bool IsExtensionOffInProcess(content::RenderProcessHost* process,
                             std::string_view extension_id);
// A request's process (WebRequestInfo::global_id.child_id), which may be gone.
bool IsExtensionOffInProcessId(content::ChildProcessId process_id,
                               std::string_view extension_id);
bool IsExtensionOffInContents(content::WebContents* contents,
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

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_EXTENSIONS_H_
