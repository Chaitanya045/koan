// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_

#include <cstddef>
#include <string>
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

// Whether `domain` is a user container's (a Space's profile). Fails closed for
// the refusals that use it: any domain with the prefix, valid or not. The
// push hook in //content/browser (#478).
constexpr bool IsUserPartitionDomain(std::string_view domain) {
  return domain.starts_with(kUserPartitionDomainPrefix);
}

// Whether `domain` is any Koan container's, an agent's or a Space's profile.
// Fails closed like the two above. The push hook in //content/browser that
// keeps a container's service-worker events away from the profile's push
// subscriptions (#596).
constexpr bool IsContainerPartitionDomain(std::string_view domain) {
  return IsAgentPartitionDomain(domain) || IsUserPartitionDomain(domain);
}

// --- Service-worker notifications in a Space's profile (#530) ---
//
// Content finds a persistent notification's data and service worker by its
// origin's home partition, the default one (NotificationEventDispatcherImpl::
// DispatchNotificationEvent()). In a Space's profile the notification's id
// names its partition instead, so its click, action buttons and close reach
// that profile's service worker, also after a restart (macOS keeps the id in
// its Notification Center). Upstream's id is "p[b]#<origin>#<0|1><tag|n>"
// (NotificationIdGenerator); a user container's puts its domain before the
// first '#': "p[b]koanuser<id>#<origin>#...". The domain is lowercase letters
// and comes before the origin, so nothing a site sets (its tag, at the end)
// can make or change it.

// The longest user container domain: the prefix and a container id
// ([a-z]{2,40}, koan_container_partition.h).
inline constexpr size_t kMaxUserPartitionDomainLength =
    sizeof(kUserPartitionDomainPrefix) - 1 + 40;

// Whether `domain` is a well-formed user container domain: the prefix, then
// 2 to 40 lowercase letters.
constexpr bool IsWellFormedUserPartitionDomain(std::string_view domain) {
  if (!IsUserPartitionDomain(domain) ||
      domain.size() < sizeof(kUserPartitionDomainPrefix) - 1 + 2 ||
      domain.size() > kMaxUserPartitionDomainLength) {
    return false;
  }
  for (const char c : domain) {
    if (c < 'a' || c > 'z') {
      return false;
    }
  }
  return true;
}

// PlatformNotificationContextImpl::DoWriteNotificationData(): `id`, a new
// persistent notification's upstream id, with `domain` (its partition's) in
// it when that is a user container's; otherwise `id` as it is.
inline std::string TagPersistentNotificationId(std::string id,
                                               std::string_view domain) {
  if (!IsWellFormedUserPartitionDomain(domain) || !id.starts_with('p')) {
    return id;
  }
  // Right after "p" or "pb": upstream's form.
  const size_t separator = id.starts_with("pb#") ? 2 : 1;
  if (id.size() <= separator || id[separator] != '#') {
    return id;
  }
  id.insert(separator, domain);
  return id;
}

// NotificationEventDispatcherImpl::DispatchNotificationEvent(): the user
// container domain a persistent notification's `id` names, or empty for
// upstream's id (the origin's home partition).
constexpr std::string_view PartitionDomainOfNotificationId(
    std::string_view id) {
  if (!id.starts_with('p')) {
    return {};
  }
  id.remove_prefix(1);
  if (id.starts_with('b')) {
    id.remove_prefix(1);
  }
  const size_t separator = id.find('#');
  if (separator == std::string_view::npos) {
    return {};
  }
  const std::string_view domain = id.substr(0, separator);
  return IsWellFormedUserPartitionDomain(domain) ? domain : std::string_view();
}

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_PARTITION_DOMAIN_H_
