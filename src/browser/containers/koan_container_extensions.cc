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

bool IsExtensionOffInProcessId(content::ChildProcessId process_id,
                               std::string_view extension_id) {
  return IsExtensionOffInProcess(content::RenderProcessHost::FromID(process_id),
                                 extension_id);
}

bool IsExtensionOffInContents(content::WebContents* contents,
                              std::string_view extension_id) {
  return contents && IsOffIn(contents->GetBrowserContext(),
                             ContainerOf(contents), extension_id);
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
