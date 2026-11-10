// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_GRANTS_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_GRANTS_H_

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Which paired agent client may use which saved agent container, as the user
// chose in Settings > Agent Control. A saved container keeps its logins, so a
// task gets one only when its client holds a grant for it and the agent asked
// for it by name (ChooseContainer()); everything else gets a throwaway
// container.
//
// Grants are keyed by the client's stable id (ClientRecord::id) and the
// container's id (never its name: deleting a container and making another
// with the same name starts with no grants). They are in memory, like the
// pairings they refer to, so they reset when Koan quits; "Stop all agents"
// forgets every pairing, and a client that pairs again has a new id and no
// grants. Pure bookkeeping, //base only.
namespace koan::containers {

class ContainerGrants {
 public:
  struct Client {
    std::string id;
    // For Settings, while the client isn't connected.
    std::string label;
  };

  ContainerGrants();
  ContainerGrants(const ContainerGrants&) = delete;
  ContainerGrants& operator=(const ContainerGrants&) = delete;
  ~ContainerGrants();

  // The browser's grants (UI thread).
  static ContainerGrants& Get();

  // Grants or revokes `container_id` for `client_id`. Empty ids do nothing.
  void Set(const Client& client, std::string_view container_id, bool granted);
  bool IsGranted(std::string_view client_id,
                 std::string_view container_id) const;

  // Every (client id, container id) granted, sorted.
  std::vector<std::pair<std::string, std::string>> All() const;
  // The clients that hold a grant, by id, with the label they were granted
  // under.
  std::vector<Client> Clients() const;

  // A deleted container's grants go with it.
  void RemoveContainer(std::string_view container_id);
  void Clear();

 private:
  std::set<std::pair<std::string, std::string>, std::less<>> grants_;
  std::map<std::string, std::string, std::less<>> labels_;
};

// What a task's tabs open in.
struct ContainerChoice {
  enum class Kind {
    // A new throwaway container: the agent asked for no saved one.
    kThrowaway,
    // The saved container `container_id`.
    kSaved,
    // Nothing: the agent asked for a saved container it may not use (not
    // granted, no such container, or another task of any client has it
    // open).
    kRefused,
  };
  Kind kind = Kind::kThrowaway;
  std::string container_id;

  friend bool operator==(const ContainerChoice&,
                         const ContainerChoice&) = default;
};

// `requested_name` is the saved container the agent asked for (empty for
// none); `saved` the profile's saved containers, name to id; `in_use` the
// container ids other tasks have open. A name never creates a container: only
// the user does, in Settings.
ContainerChoice ChooseContainer(
    const ContainerGrants& grants,
    std::string_view client_id,
    std::string_view requested_name,
    const std::map<std::string, std::string, std::less<>>& saved,
    const std::set<std::string, std::less<>>& in_use);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_CONTAINER_GRANTS_H_
