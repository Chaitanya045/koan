// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_extensions.h"

#include <algorithm>
#include <iterator>
#include <optional>

#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_partition_domain.h"
#include "koan/browser/koan_features.h"

namespace koan::containers {

namespace {

// The container's id in its entry (koan_user_containers.cc).
constexpr char kIdKey[] = "id";

// An extension id: 32 letters a-p (crx_file::id_util, which :partition
// doesn't depend on).
constexpr size_t kExtensionIdLength = 32;

constinit thread_local const std::string* g_calling_extension = nullptr;

bool IsExtensionId(std::string_view id) {
  return id.size() == kExtensionIdLength &&
         std::ranges::all_of(id, [](char c) { return c >= 'a' && c <= 'p'; });
}

// The list's entry of `container`, if it is a saved user container.
const base::DictValue* FindEntry(const base::ListValue& list,
                                 const ContainerId& container) {
  if (container.kind != ContainerKind::kUser || !container.persistent()) {
    return nullptr;
  }
  for (const base::Value& entry : list) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* id = dict ? dict->FindString(kIdKey) : nullptr;
    if (id && *id == container.id) {
      return dict;
    }
  }
  return nullptr;
}

// The user's containers in `context`'s prefs, or null.
const base::ListValue* UserContainersOf(content::BrowserContext* context) {
  if (!context || !user_prefs::UserPrefs::IsInitialized(context)) {
    return nullptr;
  }
  const PrefService::Preference* pref =
      user_prefs::UserPrefs::Get(context)->FindPreference(kUserContainersPref);
  return pref && pref->GetValue()->is_list() ? &pref->GetValue()->GetList()
                                             : nullptr;
}

bool IsOffIn(content::BrowserContext* context,
             const std::optional<ContainerId>& container,
             std::string_view extension_id) {
  if (!container || container->kind != ContainerKind::kUser ||
      !base::FeatureList::IsEnabled(features::kKoanSpaceExtensions)) {
    return false;
  }
  const base::ListValue* list = UserContainersOf(context);
  return list && IsExtensionOffIn(*list, *container, extension_id);
}

}  // namespace

std::set<std::string> ExtensionsOffIn(const base::ListValue& list,
                                      const ContainerId& container) {
  std::set<std::string> off;
  const base::DictValue* entry = FindEntry(list, container);
  const base::ListValue* ids =
      entry ? entry->FindList(kExtensionsOffKey) : nullptr;
  if (!ids) {
    return off;
  }
  for (const base::Value& id : *ids) {
    if (id.is_string() && IsExtensionId(id.GetString())) {
      off.insert(id.GetString());
    }
  }
  return off;
}

bool IsExtensionOffIn(const base::ListValue& list,
                      const ContainerId& container,
                      std::string_view extension_id) {
  // Asked for every webRequest listener of a container's requests: no copy.
  const base::DictValue* entry = FindEntry(list, container);
  const base::ListValue* ids =
      entry ? entry->FindList(kExtensionsOffKey) : nullptr;
  return ids && IsExtensionId(extension_id) &&
         std::ranges::any_of(*ids, [extension_id](const base::Value& id) {
           return id.is_string() && id.GetString() == extension_id;
         });
}

bool SetExtensionsOff(base::ListValue& list,
                      const ContainerId& container,
                      const std::set<std::string>& off) {
  if (!FindEntry(list, container)) {
    return false;
  }
  std::set<std::string> valid;
  std::ranges::copy_if(off, std::inserter(valid, valid.end()), IsExtensionId);
  if (valid == ExtensionsOffIn(list, container)) {
    return false;
  }
  for (base::Value& entry : list) {
    base::DictValue* dict = entry.GetIfDict();
    const std::string* id = dict ? dict->FindString(kIdKey) : nullptr;
    if (!id || *id != container.id) {
      continue;
    }
    if (valid.empty()) {
      dict->Remove(kExtensionsOffKey);
    } else {
      base::ListValue ids;
      for (const std::string& extension_id : valid) {
        ids.Append(extension_id);
      }
      dict->Set(kExtensionsOffKey, std::move(ids));
    }
    break;
  }
  return true;
}

bool IsExtensionOffInPartition(content::BrowserContext* context,
                               const content::StoragePartitionConfig& config,
                               std::string_view extension_id) {
  // The default partition, an extension's own, a WebView's, an agent
  // container's: no list, and nothing to parse.
  if (config.is_default() ||
      !config.partition_domain().starts_with(kUserPartitionDomainPrefix)) {
    return false;
  }
  return IsOffIn(context, ContainerFromPartitionConfig(config), extension_id);
}

bool IsExtensionOffInProcess(content::RenderProcessHost* process,
                             std::string_view extension_id) {
  return process &&
         IsExtensionOffInPartition(process->GetBrowserContext(),
                                   process->GetStoragePartition()->GetConfig(),
                                   extension_id);
}

bool IsExtensionOffInContents(content::WebContents* contents,
                              std::string_view extension_id) {
  return contents && IsOffIn(contents->GetBrowserContext(),
                             ContainerOf(contents), extension_id);
}

content::WebContents* TabUnlessExtensionOff(content::WebContents* contents,
                                            std::string_view extension_id) {
  return IsExtensionOffInContents(contents, extension_id) ? nullptr : contents;
}

std::set<std::string> ExtensionsOffInContents(content::WebContents* contents) {
  const std::optional<ContainerId> container =
      contents ? ContainerOf(contents) : std::nullopt;
  if (!container || container->kind != ContainerKind::kUser ||
      !base::FeatureList::IsEnabled(features::kKoanSpaceExtensions)) {
    return {};
  }
  const base::ListValue* list = UserContainersOf(contents->GetBrowserContext());
  return list ? ExtensionsOffIn(*list, *container) : std::set<std::string>();
}

std::optional<content::StoragePartitionConfig> PartitionOfRequest(
    content::FrameTreeNodeId navigation_frame,
    content::ChildProcessId process_id) {
  content::StoragePartition* partition = nullptr;
  if (!navigation_frame.is_null()) {
    // A navigation's request has no renderer process (WebRequestAPI's proxy
    // gets -1 for it): the frame it navigates decides. A frame keeps its
    // tab's partition, so the current RenderFrameHost of the node (the
    // initial one in a new tab, the prerendered page's, the frame a service
    // worker preloads for) is in the partition the navigation commits in.
    content::WebContents* contents =
        content::WebContents::FromFrameTreeNodeId(navigation_frame);
    content::RenderFrameHost* frame =
        contents ? contents->UnsafeFindFrameByFrameTreeNodeId(navigation_frame)
                 : nullptr;
    partition = frame ? frame->GetStoragePartition() : nullptr;
  } else if (content::RenderProcessHost* process =
                 content::RenderProcessHost::FromID(process_id)) {
    partition = process->GetStoragePartition();
  }
  if (!partition || partition->GetConfig().is_default()) {
    return std::nullopt;
  }
  return partition->GetConfig();
}

bool IsExtensionOffForRequest(
    content::BrowserContext* context,
    const std::optional<content::StoragePartitionConfig>& partition,
    std::string_view extension_id) {
  return partition &&
         IsExtensionOffInPartition(context, *partition, extension_id);
}

ScopedExtensionCall::ScopedExtensionCall(std::string_view extension_id)
    : extension_id_(extension_id),
      reset_(&g_calling_extension,
             extension_id_.empty() ? nullptr : &extension_id_) {}

ScopedExtensionCall::~ScopedExtensionCall() = default;

bool IsCallingExtensionOffIn(content::WebContents* contents) {
  return g_calling_extension &&
         IsExtensionOffInContents(contents, *g_calling_extension);
}

}  // namespace koan::containers
