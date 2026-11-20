// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_keepalive.h"

#include <utility>

#include "base/values.h"

namespace koan::cli {

namespace {

// A renewal or an end needs no long wait: the browser answers at once.
constexpr base::TimeDelta kAnswer = base::Seconds(5);

}  // namespace

TabKeepAlive::TabKeepAlive(
    Link& link,
    std::string target_id,
    base::RepeatingCallback<void(const Message&)> on_event)
    : link_(link),
      target_id_(std::move(target_id)),
      on_event_(std::move(on_event)) {
  Ask();
}

TabKeepAlive::~TabKeepAlive() {
  if (hold_id_.empty() || link_->closed()) {
    return;
  }
  base::DictValue params;
  params.Set("holdId", hold_id_);
  params.Set("seconds", 0);
  link_->Call("Koan.keepAlive", std::move(params), {}, kAnswer, on_event_);
}

void TabKeepAlive::RenewIfDue() {
  if (base::TimeTicks::Now() >= next_ && !link_->closed()) {
    Ask();
  }
}

void TabKeepAlive::Ask() {
  base::DictValue params;
  if (hold_id_.empty()) {
    params.Set("targetId", target_id_);
  } else {
    params.Set("holdId", hold_id_);
  }
  params.Set("seconds", static_cast<int>(kRequest.InSeconds()));
  next_ = base::TimeTicks::Now() + kRenewEvery;
  std::optional<Message> answer =
      link_->Call("Koan.keepAlive", std::move(params), {}, kAnswer, on_event_);
  if (!answer || answer->is_error() || !answer->result) {
    // Lapsed (the tab went, or the browser restarted): a renewal asks anew.
    hold_id_.clear();
    return;
  }
  if (const std::string* id = answer->result->FindString("holdId")) {
    hold_id_ = *id;
  }
}

}  // namespace koan::cli
