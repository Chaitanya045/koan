// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_TEST_FAKES_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_TEST_FAKES_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "koan/browser/remote/koan_ssh_askpass.h"
#include "koan/browser/remote/koan_ssh_tunnel.h"

namespace koan::remote {

// The prompt UI: records what it's asked to show and cancel.
class FakeSshPromptDelegate : public SshPromptDelegate {
 public:
  struct Shown {
    Shown();
    Shown(Shown&&);
    Shown& operator=(Shown&&);
    ~Shown();

    int id = 0;
    std::string destination;
    SshPrompt prompt;
    SshPromptReplyCallback reply;
  };

  FakeSshPromptDelegate();
  ~FakeSshPromptDelegate() override;

  // SshPromptDelegate:
  void ShowSshPrompt(int id,
                     std::string_view destination,
                     const SshPrompt& prompt,
                     SshPromptReplyCallback reply) override;
  void CancelSshPrompt(int id) override;

  // Answers the newest prompt (that has a reply).
  void Answer(SshPromptReply reply);

  std::vector<Shown>& shown() { return shown_; }
  const std::vector<int>& cancelled() const { return cancelled_; }

 private:
  std::vector<Shown> shown_;
  std::vector<int> cancelled_;
};

// The system: launches are recorded and driven by the test; ports and SOCKS
// probes answer at once. A tunnel connects only after EmitListening() and a
// probe that finds the port ready.
class FakeSshTunnelPlatform : public SshTunnelPlatform {
 public:
  struct Launched {
    Launched();
    Launched(Launched&&);
    Launched& operator=(Launched&&);
    ~Launched();

    SshDestination destination;
    uint16_t port = 0;
    ProcessCallbacks callbacks;
    bool alive = true;
  };

  FakeSshTunnelPlatform();
  ~FakeSshTunnelPlatform() override;

  // SshTunnelPlatform:
  std::unique_ptr<Process> Launch(const SshDestination& destination,
                                  uint16_t port,
                                  ProcessCallbacks callbacks) override;
  void PickPort(
      std::optional<uint16_t> preferred,
      base::OnceCallback<void(std::optional<uint16_t>)> callback) override;
  void ProbeSocks(uint16_t port,
                  base::OnceCallback<void(bool)> callback) override;

  // The ports PickPort() hands out when the preferred one isn't free (or
  // none is preferred); after the last, 40000 and up.
  void set_free_ports(std::deque<uint16_t> ports) {
    free_ports_ = std::move(ports);
  }
  // Whether the preferred port is free.
  void set_preferred_free(bool free) { preferred_free_ = free; }
  void set_socks_ready(bool ready) { socks_ready_ = ready; }

  const std::vector<Launched>& launched() const { return launched_; }
  const std::vector<std::optional<uint16_t>>& preferred_ports() const {
    return preferred_ports_;
  }
  int probes() const { return probes_; }

  // Drive the newest launch, which must be alive.
  void EmitStderr(std::string line);
  // What ssh prints once its -D listener is up: "Local forwarding listening
  // on 127.0.0.1 port <port>." and "Entering interactive session.".
  void EmitListening();
  // A kNotice comes without a reply callback.
  void EmitPrompt(SshPrompt prompt);
  void Exit(int exit_code);

  // What the tunnel answered, in order.
  std::vector<SshPromptReply>& replies() { return replies_; }

 private:
  class FakeProcess;

  Launched& current();
  void OnReply(SshPromptReply reply);

  std::vector<Launched> launched_;
  std::vector<std::optional<uint16_t>> preferred_ports_;
  std::deque<uint16_t> free_ports_;
  uint16_t next_port_ = 40000;
  bool preferred_free_ = true;
  bool socks_ready_ = false;
  int probes_ = 0;
  std::vector<SshPromptReply> replies_;
  base::WeakPtrFactory<FakeSshTunnelPlatform> weak_factory_{this};
};

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_TEST_FAKES_H_
