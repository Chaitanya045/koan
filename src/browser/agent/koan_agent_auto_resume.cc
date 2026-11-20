// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_auto_resume.h"

namespace koan::agent {

AutoResumeSettings ReadAutoResumeSettings(const base::Value* enabled,
                                          const base::Value* seconds) {
  AutoResumeSettings settings;
  settings.enabled = ReadSwitch(enabled, true);
  settings.delay = base::Seconds(ReadLimit(seconds, kAutoResumeSecondsLimit));
  return settings;
}

std::string_view PauseHoldName(PauseHold hold) {
  switch (hold) {
    case PauseHold::kNone:
      return "";
    case PauseHold::kTakeOver:
      return "take-over";
    case PauseHold::kSecretField:
      return "secret-field";
    case PauseHold::kUserNeeded:
      return "user-needed";
    case PauseHold::kInterstitial:
      return "interstitial";
    case PauseHold::kDialog:
      return "dialog";
    case PauseHold::kKeepPaused:
      return "keep-paused";
    case PauseHold::kSetting:
      return "setting";
  }
  return "";
}

AutoResumePolicy::AutoResumePolicy(AutoResumeSettings settings)
    : settings_(settings) {}

AutoResumePolicy::~AutoResumePolicy() = default;

void AutoResumePolicy::Pause(std::string_view reason,
                             bool user_on_tab,
                             base::TimeTicks now) {
  paused_ = true;
  user_on_tab_ = user_on_tab;
  hold_ = reason == "user-input" ? PauseHold::kNone : PauseHold::kTakeOver;
  paused_since_ = now;
  active_at_ = now;
  countdown_ends_.reset();
}

void AutoResumePolicy::Hold(PauseHold hold) {
  if (!paused_ || hold == PauseHold::kNone || hold == PauseHold::kSetting ||
      hold_ != PauseHold::kNone) {
    return;
  }
  hold_ = hold;
  countdown_ends_.reset();
}

void AutoResumePolicy::OnInput(base::TimeTicks now) {
  if (!paused_) {
    return;
  }
  active_at_ = now;
  countdown_ends_.reset();
}

void AutoResumePolicy::OnMouseMove(base::TimeTicks now) {
  // The same as input for the wait; that it never pauses is the caller's
  // (only input pauses a driven tab).
  OnInput(now);
}

void AutoResumePolicy::SetUserOnTab(bool user_on_tab, base::TimeTicks now) {
  if (!paused_ || user_on_tab == user_on_tab_) {
    return;
  }
  user_on_tab_ = user_on_tab;
  countdown_ends_.reset();
  if (user_on_tab) {
    // Back on the tab: they may be about to go on with it.
    active_at_ = now;
  }
}

void AutoResumePolicy::SetSettings(AutoResumeSettings settings) {
  settings_ = settings;
  if (!settings_.enabled) {
    countdown_ends_.reset();
  }
}

bool AutoResumePolicy::MayResume() const {
  return paused_ && hold() == PauseHold::kNone;
}

bool AutoResumePolicy::Advance(base::TimeTicks now) {
  if (!MayResume()) {
    countdown_ends_.reset();
    return false;
  }
  const base::TimeTicks idle_at = active_at_ + settings_.delay;
  if (!user_on_tab_) {
    return now >= idle_at;
  }
  if (!countdown_ends_) {
    if (now < idle_at) {
      return false;
    }
    // From now, not from `idle_at`: the user sees the whole countdown even if
    // this runs late.
    countdown_ends_ = now + kAutoResumeCountdown;
  }
  return now >= *countdown_ends_;
}

void AutoResumePolicy::End() {
  paused_ = false;
  hold_ = PauseHold::kNone;
  countdown_ends_.reset();
}

AutoResumePolicy::Phase AutoResumePolicy::phase() const {
  if (!paused_) {
    return Phase::kNotPaused;
  }
  if (hold() != PauseHold::kNone) {
    return Phase::kHeld;
  }
  return countdown_ends_ ? Phase::kCountdown : Phase::kWaiting;
}

PauseHold AutoResumePolicy::hold() const {
  if (!paused_) {
    return PauseHold::kNone;
  }
  if (hold_ != PauseHold::kNone) {
    return hold_;
  }
  return settings_.enabled ? PauseHold::kNone : PauseHold::kSetting;
}

std::optional<base::TimeTicks> AutoResumePolicy::NextCheck() const {
  if (!MayResume()) {
    return std::nullopt;
  }
  return countdown_ends_ ? *countdown_ends_ : active_at_ + settings_.delay;
}

std::optional<base::TimeTicks> AutoResumePolicy::ResumesAt() const {
  if (!MayResume()) {
    return std::nullopt;
  }
  if (countdown_ends_) {
    return *countdown_ends_;
  }
  const base::TimeTicks idle_at = active_at_ + settings_.delay;
  return user_on_tab_ ? idle_at + kAutoResumeCountdown : idle_at;
}

std::optional<base::TimeTicks> AutoResumePolicy::CountdownEnds() const {
  return MayResume() ? countdown_ends_ : std::nullopt;
}

}  // namespace koan::agent
