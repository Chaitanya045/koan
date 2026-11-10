// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_

#include <string_view>

// The containers' StoragePartitionConfig domains, with no dependency at all:
// //content/browser includes this (FedCM's hooks), and it can't depend on
// :partition, which depends on //content/public/browser.
namespace koan::containers {

// StoragePartitionConfig domains are [a-z]* (storage_partition_config.h). With
// a container id after them they can't be an extension id (32 letters a-p).
inline constexpr char kAgentPartitionDomainPrefix[] = "koanagent";
inline constexpr char kUserPartitionDomainPrefix[] = "koanuser";

// Whether `domain` (StoragePartitionConfig::partition_domain()) is an agent
// container's. Fails closed: any domain with the prefix is an agent's, even
// one that isn't a valid container.
constexpr bool IsAgentPartitionDomain(std::string_view domain) {
  return domain.starts_with(kAgentPartitionDomainPrefix);
}

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_
