// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_RELAY_GATE_H_
#define KOAN_CLI_KOAN_RELAY_GATE_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"

// The decisions of `koan-relay` (.agent/notes/agent-control.md section 11):
// the session-scoped loopback endpoint that lets agent-browser, which can
// only dial a WebSocket, reach agent control's unix socket. Pure: the relay
// (koan_relay_main.cc) moves the bytes.
//
// The endpoint serves one path, /cdp/<128-bit secret>, to one WebSocket at a
// time, never to a page: a request with any Origin header is refused (a web
// page always sends one), and Host must be 127.0.0.1:<port> exactly (DNS
// rebinding). The first connection must come within 30 seconds of the
// relay's start; after that the same daemon may reconnect while the relay
// session lives. The secret is never printed: it is written to a 0600 env
// file that `koan agent-browser` hands to agent-browser at spawn.
namespace koan::cli {

// No first connection by then: the secret is no longer good and the relay
// exits.
inline constexpr base::TimeDelta kRelayFirstConnection = base::Seconds(30);
// No frame either way for this long: the relay exits (shorter than
// agent-browser's 1 h daemon idle, so a stale daemon fails fast).
inline constexpr base::TimeDelta kRelayIdle = base::Minutes(15);

class RelayGate {
 public:
  enum class Decision {
    kAccept,
    kNotFound,
    kOriginRefused,
    kBadHost,
    kBusy,
    kExpired,
  };

  // `secret`: 32 lower-case hex characters. `port`: where it listens.
  RelayGate(std::string secret, uint16_t port, base::TimeTicks started);
  RelayGate(const RelayGate&) = delete;
  RelayGate& operator=(const RelayGate&) = delete;
  ~RelayGate();

  // A WebSocket upgrade request. `origin` is nullopt when the request had no
  // Origin header. On kAccept the connection is the active one until
  // OnClosed().
  Decision OnWebSocketRequest(int connection_id,
                              std::string_view path,
                              std::string_view host,
                              const std::optional<std::string>& origin,
                              base::TimeTicks now);
  void OnClosed(int connection_id);

  // The connection frames go to, if any.
  std::optional<int> active() const { return active_; }
  bool used() const { return used_; }
  // No first connection in time: the relay should exit.
  bool FirstConnectionMissed(base::TimeTicks now) const;

  // ws://127.0.0.1:<port>/cdp/<secret>.
  std::string Url() const;

 private:
  const std::string secret_;
  const uint16_t port_;
  const base::TimeTicks started_;
  std::optional<int> active_;
  bool used_ = false;
};

std::string_view DecisionName(RelayGate::Decision decision);

// An agent-browser session name the relay accepts: 1 to 64 of
// [A-Za-z0-9._-].
bool IsValidSessionName(std::string_view name);

// The agent-browser session `args` run in: their --session, else the
// AGENT_BROWSER_SESSION value given, else "default". nullopt (and `error`) if
// it is not a valid name.
std::optional<std::string> SessionFromArgs(const std::vector<std::string>& args,
                                           const char* env_session,
                                           std::string* error);

// ~/.koan/relay: the relay's own clients (clients/<id>/) and its sessions'
// files, named by a digest of the session so paths stay short.
base::FilePath RelayHome(const base::FilePath& koan_home);
base::FilePath RelayControlSocket(const base::FilePath& relay_home,
                                  std::string_view session);
base::FilePath RelayEnvFile(const base::FilePath& relay_home,
                            std::string_view session);

// The env file's contents: AGENT_BROWSER_CDP=<url>.
std::string RelayEnvContents(std::string_view url);
// The URL in an env file, or nullopt.
std::optional<std::string> ParseRelayEnv(std::string_view contents);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_RELAY_GATE_H_
