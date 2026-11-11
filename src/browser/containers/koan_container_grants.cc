// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_grants.h"

#include "base/no_destructor.h"

namespace koan::containers {

ContainerGrants::ContainerGrants() = default;
ContainerGrants::~ContainerGrants() = default;

// static
ContainerGrants& ContainerGrants::Get() {
  static base::NoDestructor<ContainerGrants> grants;
  return *grants;
}

void ContainerGrants::Set(const Client& client,
                          std::string_view container_id,
                          bool granted) {
  if (client.id.empty() || container_id.empty()) {
    return;
  }
  auto key = std::make_pair(client.id, std::string(container_id));
  if (granted) {
    grants_.insert(std::move(key));
    labels_[client.id] = client.label;
    return;
  }
  grants_.erase(key);
  bool holds_any = false;
  for (const auto& [id, container] : grants_) {
    holds_any |= id == client.id;
  }
  if (!holds_any) {
    labels_.erase(client.id);
  }
}

bool ContainerGrants::IsGranted(std::string_view client_id,
                                std::string_view container_id) const {
  return !client_id.empty() && !container_id.empty() &&
         grants_.contains(
             std::make_pair(std::string(client_id), std::string(container_id)));
}

std::vector<std::pair<std::string, std::string>> ContainerGrants::All() const {
  return {grants_.begin(), grants_.end()};
}

std::vector<ContainerGrants::Client> ContainerGrants::Clients() const {
  std::vector<Client> clients;
  for (const auto& [id, label] : labels_) {
    clients.push_back({.id = id, .label = label});
  }
  return clients;
}

void ContainerGrants::RemoveContainer(std::string_view container_id) {
  std::erase_if(
      grants_, [&](const auto& grant) { return grant.second == container_id; });
  std::erase_if(labels_, [&](const auto& label) {
    for (const auto& [id, container] : grants_) {
      if (id == label.first) {
        return false;
      }
    }
    return true;
  });
}

void ContainerGrants::Clear() {
  grants_.clear();
  labels_.clear();
}

ContainerChoice ChooseContainer(
    const ContainerGrants& grants,
    std::string_view client_id,
    std::string_view requested_name,
    const std::map<std::string, std::string, std::less<>>& saved,
    const std::set<std::string, std::less<>>& in_use) {
  using Kind = ContainerChoice::Kind;
  if (requested_name.empty()) {
    return {.kind = Kind::kThrowaway};
  }
  auto it = saved.find(requested_name);
  if (it == saved.end() || !grants.IsGranted(client_id, it->second) ||
      in_use.contains(it->second)) {
    return {.kind = Kind::kRefused};
  }
  return {.kind = Kind::kSaved, .container_id = it->second};
}

TaskPlace ChooseTaskPlace(ContainerChoice::Kind choice,
                          bool strict,
                          bool logins) {
  switch (choice) {
    case ContainerChoice::Kind::kRefused:
      return TaskPlace::kRefused;
    case ContainerChoice::Kind::kSaved:
      return TaskPlace::kSaved;
    case ContainerChoice::Kind::kThrowaway:
      break;
  }
  if (strict) {
    return TaskPlace::kStrict;
  }
  return logins ? TaskPlace::kUserPartition : TaskPlace::kThrowaway;
}

}  // namespace koan::containers
