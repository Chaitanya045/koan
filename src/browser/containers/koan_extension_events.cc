// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_extension_events.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/values.h"
#include "components/download/public/common/download_item.h"
#include "components/sessions/core/tab_restore_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/storage_partition_config.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/extension.h"
#include "koan/browser/containers/koan_container_extensions.h"

namespace koan::containers {

namespace {

constinit thread_local const ScopedEventSkip* g_event_skip = nullptr;

bool WillDispatchUnlessSkipped(
    const std::set<std::string>& off,
    const ScopedEventSkip::Filter& filter,
    const extensions::Event::WillDispatchCallback& next,
    content::BrowserContext* context,
    extensions::mojom::ContextType target_context,
    const extensions::Extension* extension,
    const base::DictValue* listener_filter,
    std::optional<base::ListValue>& event_args_out,
    extensions::mojom::EventFilteringInfoPtr& event_filtering_info_out,
    bool* dispatch_separate_event_out) {
  if (extension) {
    if (off.contains(extension->id())) {
      return false;
    }
    if (!filter.is_null() &&
        !filter.Run(context, extension->id(), event_args_out)) {
      return false;
    }
  }
  return next.is_null() ||
         next.Run(context, target_context, extension, listener_filter,
                  event_args_out, event_filtering_info_out,
                  dispatch_separate_event_out);
}

void Skip(extensions::Event& event,
          std::set<std::string> off,
          ScopedEventSkip::Filter filter) {
  if (off.empty() && filter.is_null()) {
    return;
  }
  event.will_dispatch_callback = base::BindRepeating(
      &WillDispatchUnlessSkipped, std::move(off), std::move(filter),
      std::move(event.will_dispatch_callback));
}

// The tabs of a recently closed entry, or the entry itself for a tab.
std::vector<const sessions::tab_restore::Tab*> TabsOf(
    const sessions::tab_restore::Entry& entry) {
  std::vector<const sessions::tab_restore::Tab*> tabs;
  const auto add = [&tabs](const auto& list) {
    for (const auto& tab : list) {
      tabs.push_back(tab.get());
    }
  };
  switch (entry.type) {
    case sessions::tab_restore::Type::TAB:
      tabs.push_back(static_cast<const sessions::tab_restore::Tab*>(&entry));
      break;
    case sessions::tab_restore::Type::WINDOW:
      add(static_cast<const sessions::tab_restore::Window&>(entry).tabs);
      break;
    case sessions::tab_restore::Type::GROUP:
      add(static_cast<const sessions::tab_restore::Group&>(entry).tabs);
      break;
    case sessions::tab_restore::Type::SPLIT:
      add(static_cast<const sessions::tab_restore::Split&>(entry).tabs);
      break;
  }
  return tabs;
}

}  // namespace

void SkipExtensionsOffIn(extensions::Event& event,
                         content::WebContents* contents) {
  Skip(event, ExtensionsOffInContents(contents), ScopedEventSkip::Filter());
}

ScopedEventSkip::ScopedEventSkip() : reset_(&g_event_skip, this) {}

ScopedEventSkip::ScopedEventSkip(content::WebContents* tab)
    : off_(ExtensionsOffInContents(tab)), reset_(&g_event_skip, this) {}

ScopedEventSkip::ScopedEventSkip(std::set<std::string> off)
    : off_(std::move(off)), reset_(&g_event_skip, this) {}

ScopedEventSkip::ScopedEventSkip(Filter filter)
    : filter_(std::move(filter)), reset_(&g_event_skip, this) {}

ScopedEventSkip::~ScopedEventSkip() = default;

void ScopedEventSkip::Add(content::WebContents* tab) {
  off_.merge(ExtensionsOffInContents(tab));
}

void SkipExtensionsInScope(extensions::Event& event) {
  if (g_event_skip) {
    Skip(event, g_event_skip->off(), g_event_skip->filter());
  }
}

std::set<std::string> ExtensionsOffInDownload(
    const download::DownloadItem* download) {
  if (!download) {
    return {};
  }
  std::set<std::string> off;
  content::BrowserContext* context =
      content::DownloadItemUtils::GetBrowserContext(download);
  const std::string& data = download->GetSerializedEmbedderDownloadData();
  content::DownloadManager* manager =
      context ? context->GetDownloadManager() : nullptr;
  if (manager && !data.empty()) {
    // The partition the download was started in, also after a restart (the
    // downloads database keeps it).
    off = ExtensionsOffInPartition(
        context,
        manager->SerializedEmbedderDownloadDataToStoragePartitionConfig(data));
  }
  // And the tab that started it, while it is open.
  off.merge(ExtensionsOffInContents(
      content::DownloadItemUtils::GetOriginalWebContents(download)));
  return off;
}

bool IsCallingExtensionOffInDownload(const download::DownloadItem& download) {
  const std::string_view caller = CallingExtensionId();
  if (caller.empty()) {
    return false;
  }
  // Asked for every download a search goes through: nothing to parse for an
  // extension that is off nowhere.
  content::BrowserContext* context =
      content::DownloadItemUtils::GetBrowserContext(&download);
  if (!context || ProfilesExtensionIsOffIn(context, caller).empty()) {
    return false;
  }
  return ExtensionsOffInDownload(&download).contains(std::string(caller));
}

bool HidesClosedEntry(content::BrowserContext* context,
                      std::string_view extension_id,
                      const sessions::tab_restore::Entry& entry) {
  const std::vector<const sessions::tab_restore::Tab*> tabs = TabsOf(entry);
  if (tabs.empty() || ProfilesExtensionIsOffIn(context, extension_id).empty()) {
    return false;
  }
  for (const sessions::tab_restore::Tab* tab : tabs) {
    if (!IsExtensionOffInSessionData(context, tab->extra_data, extension_id)) {
      return false;
    }
  }
  return true;
}

bool HidesClosedId(
    content::BrowserContext* context,
    std::string_view extension_id,
    const std::list<std::unique_ptr<sessions::tab_restore::Entry>>& entries,
    int id) {
  for (const auto& entry : entries) {
    if (entry->id.id() == id) {
      return HidesClosedEntry(context, extension_id, *entry);
    }
    if (entry->type == sessions::tab_restore::Type::TAB) {
      continue;
    }
    for (const sessions::tab_restore::Tab* tab : TabsOf(*entry)) {
      if (tab->id.id() == id) {
        return HidesClosedEntry(context, extension_id, *tab);
      }
    }
  }
  return false;
}

}  // namespace koan::containers
