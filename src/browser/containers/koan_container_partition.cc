// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_partition.h"

#include <iterator>
#include <memory>

#include "base/check.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace koan::containers {

namespace {

constexpr size_t kMinIdLength = 2;
constexpr size_t kMaxIdLength = 40;
constexpr size_t kRandomLetters = 26;
constexpr char kThrowawayMarker = 't';
constexpr char kSavedMarker = 's';
constexpr std::string_view kAgentSessionPrefix = "agent:";
constexpr std::string_view kUserSessionPrefix = "user:";
// The mark SetAgentLeased() puts on a user's tab while an agent leases it.
const int kAgentLeasedKey = 0;

std::string_view DomainPrefix(ContainerKind kind) {
  return kind == ContainerKind::kAgent ? kAgentPartitionDomainPrefix
                                       : kUserPartitionDomainPrefix;
}

const content::StoragePartitionConfig& ConfigOf(
    const content::WebContents* contents) {
  return contents->GetPrimaryMainFrame()
      ->GetSiteInstance()
      ->GetSecurityPrincipal()
      .GetStoragePartitionConfig();
}

// Any container, and any domain that only looks like an agent's (fails
// closed).
bool IsContainerConfig(const content::StoragePartitionConfig& config) {
  return ContainerFromPartitionConfig(config).has_value() ||
         IsAgentPartition(config);
}

}  // namespace

bool IsValidContainerId(std::string_view id) {
  if (id.size() < kMinIdLength || id.size() > kMaxIdLength) {
    return false;
  }
  if (id.front() != kThrowawayMarker && id.front() != kSavedMarker) {
    return false;
  }
  for (char c : id) {
    if (!base::IsAsciiLower(c)) {
      return false;
    }
  }
  return true;
}

std::string NewContainerId(bool persistent) {
  std::string id(1, persistent ? kSavedMarker : kThrowawayMarker);
  for (size_t i = 0; i < kRandomLetters; ++i) {
    id.push_back(static_cast<char>('a' + base::RandGenerator(26)));
  }
  return id;
}

content::StoragePartitionConfig PartitionConfigFor(
    content::BrowserContext* context,
    const ContainerId& container) {
  CHECK(IsValidContainerId(container.id));
  return content::StoragePartitionConfig::Create(
      context, base::StrCat({DomainPrefix(container.kind), container.id}),
      /*partition_name=*/std::string_view(),
      /*in_memory=*/!container.persistent());
}

std::optional<ContainerId> ContainerFromPartitionConfig(
    const content::StoragePartitionConfig& config) {
  if (config.is_default() || !config.partition_name().empty()) {
    return std::nullopt;
  }
  std::string_view domain = config.partition_domain();
  ContainerKind kind;
  // "koanagent" isn't a prefix of "koanuser" or the other way round.
  if (base::StartsWith(domain, kAgentPartitionDomainPrefix)) {
    kind = ContainerKind::kAgent;
    domain.remove_prefix(std::size(kAgentPartitionDomainPrefix) - 1);
  } else if (base::StartsWith(domain, kUserPartitionDomainPrefix)) {
    kind = ContainerKind::kUser;
    domain.remove_prefix(std::size(kUserPartitionDomainPrefix) - 1);
  } else {
    return std::nullopt;
  }
  if (!IsValidContainerId(domain)) {
    return std::nullopt;
  }
  ContainerId container{.kind = kind, .id = std::string(domain)};
  // A throwaway container on disk isn't one Koan made. (An off-the-record
  // profile makes every partition in memory, saved ones too.)
  if (!container.persistent() && !config.in_memory()) {
    return std::nullopt;
  }
  return container;
}

bool IsAgentPartition(const content::StoragePartitionConfig& config) {
  return IsAgentPartitionDomain(config.partition_domain());
}

bool IsAgentProcess(content::RenderProcessHost* process) {
  return process &&
         IsAgentPartition(process->GetStoragePartition()->GetConfig());
}

bool IsAgentFrame(content::RenderFrameHost* frame) {
  return frame && IsAgentPartition(frame->GetSiteInstance()
                                       ->GetSecurityPrincipal()
                                       .GetStoragePartitionConfig());
}

bool IsAgentContents(const content::WebContents* contents) {
  return contents && (IsAgentPartition(ConfigOf(contents)) ||
                      contents->GetUserData(&kAgentLeasedKey));
}

bool IsAgentContainerContents(const content::WebContents* contents) {
  return contents && IsAgentPartition(ConfigOf(contents));
}

void SetAgentLeased(content::WebContents* contents, bool leased) {
  if (!contents) {
    return;
  }
  if (leased) {
    contents->SetUserData(&kAgentLeasedKey,
                          std::make_unique<base::SupportsUserData::Data>());
  } else {
    contents->RemoveUserData(&kAgentLeasedKey);
  }
}

bool IsContainerFrame(content::RenderFrameHost* frame) {
  return frame && IsContainerSiteInstance(frame->GetSiteInstance());
}

bool IsContainerContents(const content::WebContents* contents) {
  return contents && IsContainerConfig(ConfigOf(contents));
}

bool IsContainerSiteInstance(content::SiteInstance* site_instance) {
  return site_instance &&
         IsContainerConfig(
             site_instance->GetSecurityPrincipal().GetStoragePartitionConfig());
}

std::optional<ContainerId> ContainerOf(const content::WebContents* contents) {
  if (!contents) {
    return std::nullopt;
  }
  return ContainerFromPartitionConfig(ConfigOf(contents));
}

std::string Serialize(const ContainerId& container) {
  return base::StrCat({container.kind == ContainerKind::kAgent
                           ? kAgentSessionPrefix
                           : kUserSessionPrefix,
                       container.id});
}

std::optional<ContainerId> Parse(std::string_view text) {
  ContainerKind kind;
  if (base::StartsWith(text, kAgentSessionPrefix)) {
    kind = ContainerKind::kAgent;
    text.remove_prefix(kAgentSessionPrefix.size());
  } else if (base::StartsWith(text, kUserSessionPrefix)) {
    kind = ContainerKind::kUser;
    text.remove_prefix(kUserSessionPrefix.size());
  } else {
    return std::nullopt;
  }
  if (!IsValidContainerId(text)) {
    return std::nullopt;
  }
  return ContainerId{.kind = kind, .id = std::string(text)};
}

scoped_refptr<content::SiteInstance> CreateSiteInstance(
    content::BrowserContext* context,
    const ContainerId& container,
    const GURL& url) {
  return content::SiteInstance::CreateForFixedStoragePartition(
      context, url, PartitionConfigFor(context, container));
}

scoped_refptr<content::SiteInstance> SiteInstanceForNewTab(
    content::BrowserContext* context,
    content::SiteInstance* source,
    const GURL& url) {
  if (!source || source->GetBrowserContext() != context) {
    return nullptr;
  }
  const content::StoragePartitionConfig& config =
      source->GetSecurityPrincipal().GetStoragePartitionConfig();
  if (!IsContainerConfig(config)) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(context, url,
                                                               config);
}

scoped_refptr<content::SiteInstance> SiteInstanceForReplacement(
    content::WebContents* old_contents) {
  const content::StoragePartitionConfig& config = ConfigOf(old_contents);
  if (!IsContainerConfig(config)) {
    return nullptr;
  }
  return content::SiteInstance::CreateForFixedStoragePartition(
      old_contents->GetBrowserContext(), GURL(), config);
}

bool AllowsPrerender(const content::WebContents& contents) {
  return !IsContainerContents(&contents);
}

base::FilePath AgentDownloadDirectory(const base::FilePath& download_dir) {
  return download_dir.Append(FILE_PATH_LITERAL("Koan Agent"));
}

}  // namespace koan::containers
