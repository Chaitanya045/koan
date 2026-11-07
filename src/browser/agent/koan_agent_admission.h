// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_ADMISSION_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_ADMISSION_H_

#include <stddef.h>
#include <stdint.h>

#include <map>

// How many connections the gateway keeps open (.agent/notes/agent-control.md
// section 4.1). A connection is admitted when it is accepted, before anything
// is read from it or anyone asked who it is, and counted until it is closed.
// Connections that have not authenticated yet are limited more tightly than the
// total, so a process that opens connections and says nothing cannot take every
// slot from an agent that is already working.
//
// The cap is not per user: the listening socket refuses every uid but the
// browser's before this is asked (it is in a 0700 directory as well), so every
// connection here is the browser's own user's and the total is the per-user
// cap.
namespace koan::agent {

class ConnectionAdmission {
 public:
  using Id = uint64_t;
  // No connection has this id.
  static constexpr Id kRefused = 0;

  static constexpr size_t kMaxConnections = 8;
  static constexpr size_t kMaxUnauthenticated = 4;

  ConnectionAdmission();
  ~ConnectionAdmission();

  // The id of a new connection, or kRefused if the gateway is full or too many
  // connections have not authenticated.
  Id TryAdmit();
  // The connection proved who it is: it no longer counts against the limit on
  // unauthenticated connections.
  void MarkAuthenticated(Id id);
  // The connection is closed. Unknown ids are ignored.
  void Release(Id id);

  size_t open() const { return connections_.size(); }
  size_t unauthenticated() const;

 private:
  Id next_id_ = 1;
  // id -> authenticated.
  std::map<Id, bool> connections_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_ADMISSION_H_
