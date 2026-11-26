// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_test_fakes.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"

namespace koan::remote {

FakeSshPromptDelegate::Shown::Shown() = default;
FakeSshPromptDelegate::Shown::Shown(Shown&&) = default;
FakeSshPromptDelegate::Shown& FakeSshPromptDelegate::Shown::operator=(Shown&&) =
    default;
FakeSshPromptDelegate::Shown::~Shown() = default;

FakeSshPromptDelegate::FakeSshPromptDelegate() = default;
FakeSshPromptDelegate::~FakeSshPromptDelegate() = default;

void FakeSshPromptDelegate::ShowSshPrompt(int id,
                                          std::string_view destination,
                                          const SshPrompt& prompt,
                                          SshPromptReplyCallback reply) {
  Shown shown;
  shown.id = id;
  shown.destination = std::string(destination);
  shown.prompt = prompt;
  shown.reply = std::move(reply);
  shown_.push_back(std::move(shown));
}

void FakeSshPromptDelegate::CancelSshPrompt(int id) {
  cancelled_.push_back(id);
}

void FakeSshPromptDelegate::Answer(SshPromptReply reply) {
  for (auto it = shown_.rbegin(); it != shown_.rend(); ++it) {
    if (it->reply) {
      std::move(it->reply).Run(std::move(reply));
      return;
    }
  }
  NOTREACHED();
}

FakeSshTunnelPlatform::Launched::Launched() = default;
FakeSshTunnelPlatform::Launched::Launched(Launched&&) = default;
FakeSshTunnelPlatform::Launched& FakeSshTunnelPlatform::Launched::operator=(
    Launched&&) = default;
FakeSshTunnelPlatform::Launched::~Launched() = default;

class FakeSshTunnelPlatform::FakeProcess : public SshTunnelPlatform::Process {
 public:
  FakeProcess(base::WeakPtr<FakeSshTunnelPlatform> platform, size_t index)
      : platform_(std::move(platform)), index_(index) {}
  ~FakeProcess() override {
    if (platform_) {
      platform_->launched_[index_].alive = false;
    }
  }

 private:
  base::WeakPtr<FakeSshTunnelPlatform> platform_;
  const size_t index_;
};

FakeSshTunnelPlatform::FakeSshTunnelPlatform() = default;
FakeSshTunnelPlatform::~FakeSshTunnelPlatform() = default;

std::unique_ptr<SshTunnelPlatform::Process> FakeSshTunnelPlatform::Launch(
    const SshDestination& destination,
    uint16_t port,
    ProcessCallbacks callbacks) {
  Launched launched;
  launched.destination = destination;
  launched.port = port;
  launched.callbacks = std::move(callbacks);
  launched_.push_back(std::move(launched));
  return std::make_unique<FakeProcess>(weak_factory_.GetWeakPtr(),
                                       launched_.size() - 1);
}

void FakeSshTunnelPlatform::PickPort(
    std::optional<uint16_t> preferred,
    base::OnceCallback<void(std::optional<uint16_t>)> callback) {
  preferred_ports_.push_back(preferred);
  if (preferred && preferred_free_) {
    std::move(callback).Run(preferred);
    return;
  }
  uint16_t port = next_port_++;
  if (!free_ports_.empty()) {
    port = free_ports_.front();
    free_ports_.pop_front();
  }
  std::move(callback).Run(port);
}

void FakeSshTunnelPlatform::ProbeSocks(
    uint16_t port,
    base::OnceCallback<void(bool)> callback) {
  ++probes_;
  std::move(callback).Run(socks_ready_);
}

FakeSshTunnelPlatform::Launched& FakeSshTunnelPlatform::current() {
  CHECK(!launched_.empty());
  CHECK(launched_.back().alive);
  return launched_.back();
}

void FakeSshTunnelPlatform::EmitStderr(std::string line) {
  current().callbacks.on_stderr_line.Run(std::move(line));
}

void FakeSshTunnelPlatform::EmitPrompt(SshPrompt prompt) {
  SshPromptReplyCallback reply;
  if (prompt.kind != SshPromptKind::kNotice) {
    reply = base::BindOnce(&FakeSshTunnelPlatform::OnReply,
                           weak_factory_.GetWeakPtr());
  }
  current().callbacks.on_prompt.Run(std::move(prompt), std::move(reply));
}

void FakeSshTunnelPlatform::Exit(int exit_code) {
  Launched& launched = current();
  launched.alive = false;
  std::move(launched.callbacks.on_exit).Run(exit_code);
}

void FakeSshTunnelPlatform::OnReply(SshPromptReply reply) {
  replies_.push_back(std::move(reply));
}

}  // namespace koan::remote
