// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_tunnel.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"

namespace koan::remote {

SshTunnelPlatform::ProcessCallbacks::ProcessCallbacks() = default;
SshTunnelPlatform::ProcessCallbacks::ProcessCallbacks(ProcessCallbacks&&) =
    default;
SshTunnelPlatform::ProcessCallbacks&
SshTunnelPlatform::ProcessCallbacks::operator=(ProcessCallbacks&&) = default;
SshTunnelPlatform::ProcessCallbacks::~ProcessCallbacks() = default;

SshTunnelStatus::SshTunnelStatus() = default;
SshTunnelStatus::SshTunnelStatus(const SshTunnelStatus&) = default;
SshTunnelStatus& SshTunnelStatus::operator=(const SshTunnelStatus&) = default;
SshTunnelStatus::~SshTunnelStatus() = default;

SshTunnel::SshTunnel(SshDestination destination,
                     SshTunnelPlatform* platform,
                     SshPromptDelegate* prompts)
    : destination_(std::move(destination)),
      destination_text_(SshDestinationToString(destination_)),
      platform_(platform),
      prompts_(prompts) {
  CHECK(IsValidSshHost(destination_.host));
  CHECK(platform_);
  CHECK(prompts_);
}

SshTunnel::~SshTunnel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CancelPrompts();
}

void SshTunnel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SshTunnel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SshTunnel::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (status_.state != SshTunnelState::kStopped &&
      status_.state != SshTunnelState::kFailed) {
    return;
  }
  ResetBackoff();
  BeginAttempt();
}

void SshTunnel::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++attempt_;
  process_.reset();
  retry_timer_.Stop();
  probe_timer_.Stop();
  CancelPrompts();
  ResetBackoff();
  status_ = SshTunnelStatus();
  Notify();
}

void SshTunnel::Reconnect() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (status_.state == SshTunnelState::kStopped) {
    return;
  }
  ResetBackoff();
  BeginAttempt();
}

void SshTunnel::OnSuspend() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  suspended_ = true;
  // A retry waits for the resume.
  retry_timer_.Stop();
}

void SshTunnel::OnResume() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  suspended_ = false;
  if (status_.state == SshTunnelState::kRetrying) {
    ResetBackoff();
    BeginAttempt();
    return;
  }
  RestartIfStale();
}

void SshTunnel::OnNetworkChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (suspended_) {
    return;
  }
  if (status_.state == SshTunnelState::kRetrying) {
    ResetBackoff();
    BeginAttempt();
    return;
  }
  RestartIfStale();
}

void SshTunnel::ResetBackoff() {
  backoff_failures_ = 0;
  immediate_port_retries_ = 0;
  status_.failed_attempts = 0;
}

void SshTunnel::RestartIfStale() {
  if (status_.state == SshTunnelState::kConnected && !needed_input_) {
    BeginAttempt();
  }
}

void SshTunnel::SetState(SshTunnelState state) {
  status_.state = state;
  if (state != SshTunnelState::kConnected) {
    status_.port = 0;
  }
  if (state != SshTunnelState::kRetrying) {
    status_.next_attempt = base::TimeTicks();
  }
  if (state != SshTunnelState::kRetrying && state != SshTunnelState::kFailed) {
    status_.failure = SshFailure::kNone;
    status_.detail.clear();
  }
  if (state != SshTunnelState::kNeedsInput) {
    status_.prompt.reset();
  }
}

void SshTunnel::Notify() {
  // A copy: an observer may change the tunnel.
  const SshTunnelStatus status = status_;
  for (Observer& observer : observers_) {
    observer.OnSshTunnelStatusChanged(status);
  }
}

void SshTunnel::BeginAttempt() {
  ++attempt_;
  process_.reset();
  retry_timer_.Stop();
  probe_timer_.Stop();
  CancelPrompts();
  stderr_lines_.clear();
  forward_ = ForwardState::kWaiting;
  needed_input_ = false;
  cancelled_ = false;
  connected_at_ = base::TimeTicks();
  port_ = 0;
  if (suspended_) {
    // Wait for OnResume().
    SetState(SshTunnelState::kRetrying);
    Notify();
    return;
  }
  const int attempt = attempt_;
  SetState(SshTunnelState::kConnecting);
  Notify();
  if (attempt != attempt_) {
    return;  // An observer stopped or restarted the tunnel.
  }
  platform_->PickPort(last_port_,
                      base::BindOnce(&SshTunnel::OnPortPicked,
                                     weak_factory_.GetWeakPtr(), attempt));
}

void SshTunnel::OnPortPicked(int attempt, std::optional<uint16_t> port) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attempt != attempt_) {
    return;
  }
  if (!port) {
    EndAttempt(SshFailure::kPortInUse, std::string());
    return;
  }
  port_ = *port;
  SshTunnelPlatform::ProcessCallbacks callbacks;
  callbacks.on_stderr_line = base::BindRepeating(
      &SshTunnel::OnStderrLine, weak_factory_.GetWeakPtr(), attempt);
  callbacks.on_prompt = base::BindRepeating(
      &SshTunnel::OnPrompt, weak_factory_.GetWeakPtr(), attempt);
  callbacks.on_exit =
      base::BindOnce(&SshTunnel::OnExit, weak_factory_.GetWeakPtr(), attempt);
  process_ = platform_->Launch(destination_, port_, std::move(callbacks));
}

void SshTunnel::OnStderrLine(int attempt, std::string line) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attempt != attempt_) {
    return;
  }
  AddSshStderrLine(stderr_lines_, line);
  OnForwardLine(ReadSshForwardLine(line, port_));
}

void SshTunnel::OnForwardLine(SshForwardLine line) {
  if (!process_ || forward_ == ForwardState::kListening) {
    return;
  }
  switch (line) {
    case SshForwardLine::kOther:
      return;
    case SshForwardLine::kBinding:
      forward_ = ForwardState::kBinding;
      return;
    case SshForwardLine::kFailed:
      // ssh would carry on without the forward (ExitOnForwardFailure=no, so
      // that the user's own forwards can fail harmlessly).
      EndAttempt(SshFailure::kPortInUse, SshFailureDetail(stderr_lines_));
      return;
    case SshForwardLine::kSessionStarted:
      // Without kBinding first, this is another ssh's (a ProxyJump's at the
      // user's LogLevel), or Koan's -D wasn't set up: wait.
      if (forward_ == ForwardState::kBinding) {
        forward_ = ForwardState::kListening;
        Probe();
      }
      return;
  }
}

void SshTunnel::OnPrompt(int attempt,
                         SshPrompt prompt,
                         SshPromptReplyCallback reply) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attempt != attempt_ || !process_) {
    return;  // Dropping `reply` answers nothing; the helper is killed.
  }
  // ssh asks one question at a time: a new one ends any notice.
  CancelPrompts();
  needed_input_ = true;
  const int id = next_prompt_id_++;
  shown_prompts_.push_back(id);
  const SshPromptKind kind = prompt.kind;
  SetState(SshTunnelState::kNeedsInput);
  status_.prompt = prompt;
  Notify();
  if (attempt != attempt_) {
    return;  // An observer stopped or restarted the tunnel.
  }
  if (kind == SshPromptKind::kNotice) {
    prompts_->ShowSshPrompt(id, destination_text_, prompt,
                            SshPromptReplyCallback());
    return;
  }
  prompts_->ShowSshPrompt(
      id, destination_text_, prompt,
      base::BindOnce(&SshTunnel::OnPromptReply, weak_factory_.GetWeakPtr(),
                     attempt, id, kind, std::move(reply)));
}

void SshTunnel::OnPromptReply(int attempt,
                              int prompt_id,
                              SshPromptKind kind,
                              SshPromptReplyCallback reply,
                              SshPromptReply answer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::erase(shown_prompts_, prompt_id);
  if (attempt != attempt_ || !process_) {
    return;  // `answer` is wiped when dropped.
  }
  if (kind == SshPromptKind::kSecret && !answer.accepted) {
    // ssh would carry on with an empty password; the user meant stop.
    cancelled_ = true;
    std::move(reply).Run(SshPromptReply::Cancel());
    EndAttempt(SshFailure::kCancelled, std::string());
    return;
  }
  // A rejected host key makes ssh exit with "Host key verification failed."
  std::move(reply).Run(std::move(answer));
  if (status_.state == SshTunnelState::kNeedsInput) {
    SetState(SshTunnelState::kConnecting);
    Notify();
  }
}

void SshTunnel::OnExit(int attempt, int exit_code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attempt != attempt_) {
    return;
  }
  const bool was_connected = status_.state == SshTunnelState::kConnected;
  const SshFailure failure =
      cancelled_ ? SshFailure::kCancelled
                 : ClassifySshExit(stderr_lines_, exit_code, was_connected);
  EndAttempt(failure, SshFailureDetail(stderr_lines_));
}

void SshTunnel::ScheduleProbe() {
  probe_timer_.Start(FROM_HERE, kProbeInterval,
                     base::BindOnce(&SshTunnel::Probe, base::Unretained(this)));
}

void SshTunnel::Probe() {
  if (!process_ || forward_ != ForwardState::kListening ||
      status_.state == SshTunnelState::kConnected) {
    return;
  }
  platform_->ProbeSocks(port_,
                        base::BindOnce(&SshTunnel::OnProbeResult,
                                       weak_factory_.GetWeakPtr(), attempt_));
}

void SshTunnel::OnProbeResult(int attempt, bool ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (attempt != attempt_ || !process_) {
    return;
  }
  if (!ready) {
    ScheduleProbe();
    return;
  }
  CancelPrompts();  // A notice ("touch your key") is over.
  last_port_ = port_;
  connected_at_ = base::TimeTicks::Now();
  immediate_port_retries_ = 0;
  // backoff_failures_ stays until the connection proves stable (EndAttempt()):
  // a tunnel that drops right after connecting keeps backing off.
  SetState(SshTunnelState::kConnected);
  status_.port = port_;
  status_.failed_attempts = 0;
  Notify();
}

void SshTunnel::EndAttempt(SshFailure failure, std::string detail) {
  ++attempt_;
  process_.reset();
  probe_timer_.Stop();
  CancelPrompts();
  if (!connected_at_.is_null() &&
      base::TimeTicks::Now() - connected_at_ >= kStableConnection) {
    backoff_failures_ = 0;
  }
  connected_at_ = base::TimeTicks();
  if (!IsRetryableSshFailure(failure)) {
    SetState(SshTunnelState::kFailed);
    status_.failure = failure;
    status_.detail = std::move(detail);
    Notify();
    return;
  }
  ++backoff_failures_;
  ++status_.failed_attempts;
  base::TimeDelta delay = RetryDelay();
  if (failure == SshFailure::kPortInUse) {
    last_port_.reset();  // Someone else has it now.
    if (immediate_port_retries_ < kMaxImmediatePortRetries) {
      ++immediate_port_retries_;
      delay = base::TimeDelta();
    }
  }
  SetState(SshTunnelState::kRetrying);
  status_.failure = failure;
  status_.detail = std::move(detail);
  status_.next_attempt = base::TimeTicks::Now() + delay;
  // Before Notify(): an observer may Stop() the tunnel, which stops the timer.
  if (!suspended_) {
    retry_timer_.Start(
        FROM_HERE, delay,
        base::BindOnce(&SshTunnel::BeginAttempt, base::Unretained(this)));
  }
  Notify();
}

void SshTunnel::CancelPrompts() {
  std::vector<int> shown = std::move(shown_prompts_);
  shown_prompts_.clear();
  for (int id : shown) {
    prompts_->CancelSshPrompt(id);
  }
}

base::TimeDelta SshTunnel::RetryDelay() const {
  base::TimeDelta delay = kFirstRetryDelay;
  for (int i = 1; i < backoff_failures_ && delay < kMaxRetryDelay; ++i) {
    delay *= 2;
  }
  return std::min(delay, kMaxRetryDelay);
}

}  // namespace koan::remote
