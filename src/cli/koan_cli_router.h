// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_ROUTER_H_
#define KOAN_CLI_KOAN_CLI_ROUTER_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// The bookkeeping of the holder: the one process that keeps a client's
// connection to the browser open for its tasks' lives (the browser closes a
// client's tabs when its last connection goes, design section 4.2), and that
// each `koan` command talks to. Pure: the holder's poll loop
// (koan_cli_main.cc) moves the bytes.
//
// Every request from a command gets an id of the holder's own on the way to
// the browser, and its answer goes back to the command that asked with that
// command's id. Tab sessions are kept: a second Target.attachToTarget for the
// same tab is answered from the cache, so that commands share one session per
// tab. Events are kept in a short ring for `koan status`, and go to every
// `koan watch`.
namespace koan::cli {

class Router {
 public:
  // A command's connection.
  using ConnectionId = uint64_t;

  // The events kept for status.
  static constexpr size_t kMaxEvents = 64;

  Router();
  Router(const Router&) = delete;
  Router& operator=(const Router&) = delete;
  ~Router();

  struct Outgoing {
    // To the browser, with the holder's id.
    std::optional<std::string> to_browser;
    // Straight back to the command (a cached session).
    std::optional<std::string> to_command;
  };
  // A request from a command. nullopt-both if it is not a request.
  Outgoing FromCommand(ConnectionId connection, std::string_view json);

  struct Delivery {
    ConnectionId connection = 0;
    std::string json;
  };
  // A message from the browser: an answer goes to the command that asked
  // (empty if it is gone); an event to every watcher.
  std::vector<Delivery> FromBrowser(std::string_view json);

  void AddWatcher(ConnectionId connection);
  void RemoveConnection(ConnectionId connection);

  // The tasks the client opened tabs in through this holder (from
  // Koan.openTab's answers), without the ones Koan.endTask ended.
  const std::set<std::string>& tasks() const { return tasks_; }
  // The latest events, oldest first (JSON).
  const std::deque<std::string>& events() const { return events_; }
  bool has_pending() const { return !pending_.empty(); }

 private:
  struct Pending {
    ConnectionId connection = 0;
    int command_id = 0;
    std::string method;
    // Koan.openTab and endTask: the task; attachToTarget: the target.
    std::string subject;
  };

  int next_id_ = 1;
  std::map<int, Pending> pending_;
  // Target id -> sessionId.
  std::map<std::string, std::string> sessions_;
  std::set<std::string> tasks_;
  std::set<ConnectionId> watchers_;
  std::deque<std::string> events_;
};

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_ROUTER_H_
