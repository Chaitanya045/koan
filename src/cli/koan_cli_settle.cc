// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_settle.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"

namespace koan::cli {

namespace {

// Requests that stay open for as long as the page lives: never "in flight".
bool IsLongLived(std::string_view type) {
  return type == "WebSocket" || type == "EventSource" || type == "Media";
}

std::string Str(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

std::string Seconds(base::TimeDelta wait) {
  const int64_t ms = wait.InMilliseconds();
  return ms % 1000 == 0 ? base::NumberToString(ms / 1000)
                        : base::StringPrintf("%.1f", ms / 1000.0);
}

}  // namespace

std::string DomQuietExpression(base::TimeDelta quiet, base::TimeDelta cap) {
  // Any change of the document (nodes, attributes, text), in the light DOM;
  // the timer starts again on each.
  return base::StringPrintf(
      R"((() => new Promise(resolve => {
  const quietMs = %d, capMs = %d;
  let timer = 0;
  const root = document.documentElement || document;
  const observer = new MutationObserver(() => {
    clearTimeout(timer);
    timer = setTimeout(done, quietMs, true);
  });
  const cap = setTimeout(done, capMs, false);
  function done(quiet) {
    observer.disconnect();
    clearTimeout(timer);
    clearTimeout(cap);
    resolve({quiet});
  }
  observer.observe(root, {subtree: true, childList: true, attributes: true,
                          characterData: true});
  timer = setTimeout(done, quietMs, true);
}))())",
      static_cast<int>(quiet.InMilliseconds()),
      static_cast<int>(std::max<int64_t>(cap.InMilliseconds(), 1)));
}

Settle::Settle(const SettleTimings& timings,
               base::TimeTicks start,
               bool navigating,
               std::string target,
               std::string main_frame)
    : timings_(timings),
      start_(start),
      target_(std::move(target)),
      main_frame_(std::move(main_frame)),
      last_network_(start) {
  if (navigating) {
    navigation_started_ = start;
  }
}

Settle::~Settle() = default;

void Settle::OnEvent(const Message& event, base::TimeTicks now) {
  const std::string& method = event.method;
  const base::DictValue& params = event.params;
  auto is_main = [&](std::string_view frame) {
    return !frame.empty() && frame == main_frame_;
  };
  auto start_navigation = [&] {
    // A new one (a redirect by script, a second submit) starts again.
    if (!navigation_started_ || committed_) {
      navigation_started_ = now;
      committed_ = false;
      loaded_ = false;
      http_status_.reset();
      status_text_.clear();
    }
  };
  if (method == "Network.requestWillBeSent") {
    const std::string id = Str(params, "requestId");
    const std::string type = Str(params, "type");
    if (type == "Document" && is_main(Str(params, "frameId")) &&
        !params.FindDict("redirectResponse")) {
      start_navigation();
    }
    if (!id.empty() && !IsLongLived(type)) {
      in_flight_.insert(id);
    }
    last_network_ = now;
  } else if (method == "Network.loadingFinished" ||
             method == "Network.loadingFailed") {
    in_flight_.erase(Str(params, "requestId"));
    last_network_ = now;
  } else if (method == "Network.responseReceived") {
    if (Str(params, "type") == "Document" && is_main(Str(params, "frameId"))) {
      if (const base::DictValue* response = params.FindDict("response")) {
        // A number, which FindDouble() reads whether it is sent as an int
        // or a double.
        if (std::optional<double> status = response->FindDouble("status")) {
          http_status_ = static_cast<int>(*status);
        }
        status_text_ = Str(*response, "statusText");
      }
    }
    last_network_ = now;
  } else if (method == "Page.frameStartedLoading") {
    if (is_main(Str(params, "frameId"))) {
      start_navigation();
    }
  } else if (method == "Page.frameNavigated") {
    const base::DictValue* frame = params.FindDict("frame");
    if (frame && is_main(Str(*frame, "id"))) {
      if (!navigation_started_) {
        navigation_started_ = now;
      }
      committed_ = true;
      committed_at_ = now;
      loaded_ = false;
    }
  } else if (method == "Page.navigatedWithinDocument") {
    // pushState, a fragment: no load to wait for.
    if (is_main(Str(params, "frameId"))) {
      if (!navigation_started_ || committed_) {
        navigation_started_ = now;
      }
      committed_ = true;
      committed_at_ = now;
      loaded_ = true;
    }
  } else if (method == "Page.loadEventFired") {
    if (committed_ || !navigation_started_) {
      loaded_ = true;
    }
  } else if (method == "Page.lifecycleEvent") {
    if (Str(params, "name") == "load" && is_main(Str(params, "frameId")) &&
        (committed_ || !navigation_started_)) {
      loaded_ = true;
    }
  } else if (method == "Page.frameStoppedLoading") {
    if (is_main(Str(params, "frameId"))) {
      if (navigation_started_ && !committed_) {
        // It ended without a new document (a 204, a download, a stop).
        navigation_started_.reset();
      }
      loaded_ = true;
    }
  } else if (method == "Page.javascriptDialogOpening") {
    blocked_reason_ =
        "the page opened a dialog (" + Str(params, "type") +
        ") and waits for the user: Koan.userNeeded; tell the user";
  } else if (method == "Koan.userNeeded" || method == "Koan.paused") {
    if (Str(params, "targetId") == target_) {
      const std::string reason = Str(params, "reason");
      blocked_reason_ =
          method == "Koan.paused"
              ? "the user took this tab (" + reason +
                    "): Koan.paused; wait for them to hand it back"
              : "the page waits for the user (" + reason +
                    "): Koan.userNeeded; tell the user what it asks";
    }
  } else if (method == "Target.detachedFromTarget" ||
             method == "Target.targetDestroyed") {
    if (Str(params, "targetId") == target_) {
      blocked_reason_ = "the tab closed: Koan.tabGone";
    }
  }
}

void Settle::OnNavigateAnswered(base::TimeTicks now) {
  if (!committed_) {
    committed_ = true;
    committed_at_ = now;
  }
}

void Settle::OnDomProbeSent(base::TimeTicks now) {
  probing_ = true;
  probe_sent_ = now;
}

void Settle::OnDomProbe(DomProbe result, base::TimeTicks now) {
  probing_ = false;
  switch (result) {
    case DomProbe::kQuiet:
      dom_quiet_since_ = probe_sent_;
      break;
    case DomProbe::kBusy:
      break;
    case DomProbe::kPageChanged:
      probe_retry_at_ = now + base::Milliseconds(100);
      break;
    case DomProbe::kFailed:
      dom_unavailable_ = true;
      break;
  }
}

bool Settle::NavigationPending() const {
  return navigation_started_ && !committed_;
}

base::TimeTicks Settle::SettleStart() const {
  return committed_ ? std::max(start_, committed_at_) : start_;
}

bool Settle::still_loading() const {
  return NavigationPending() || (committed_ && !loaded_);
}

SettleState Settle::Check(base::TimeTicks now) const {
  if (!blocked_reason_.empty()) {
    return SettleState::kBlocked;
  }
  if (NavigationPending()) {
    return now - *navigation_started_ >= timings_.navigation_timeout
               ? SettleState::kTimedOut
               : SettleState::kWaiting;
  }
  // A page that keeps navigating itself stops somewhere.
  if (now - start_ >= timings_.navigation_timeout + timings_.cap) {
    return SettleState::kCapped;
  }
  // A click's navigation may begin a little after the click.
  if (!navigation_started_ && now - start_ < timings_.navigation_grace) {
    return SettleState::kWaiting;
  }
  const base::TimeTicks quiet_from =
      committed_ ? std::max(last_network_, committed_at_) : last_network_;
  const bool dom_quiet =
      dom_unavailable_ || (dom_quiet_since_ && *dom_quiet_since_ >= quiet_from);
  const bool quiet = (!committed_ || loaded_) && in_flight_.empty() &&
                     now - last_network_ >= timings_.network_quiet && dom_quiet;
  if (quiet) {
    return SettleState::kSettled;
  }
  return now - SettleStart() >= timings_.cap ? SettleState::kCapped
                                             : SettleState::kWaiting;
}

base::TimeTicks Settle::NextCheck(base::TimeTicks now) const {
  std::vector<base::TimeTicks> ahead;
  if (NavigationPending()) {
    ahead.push_back(*navigation_started_ + timings_.navigation_timeout);
  } else {
    ahead.push_back(SettleStart() + timings_.cap);
    ahead.push_back(start_ + timings_.navigation_timeout + timings_.cap);
    if (!navigation_started_) {
      ahead.push_back(start_ + timings_.navigation_grace);
    }
    ahead.push_back(last_network_ + timings_.network_quiet);
    ahead.push_back(probe_retry_at_);
  }
  base::TimeTicks next = base::TimeTicks::Max();
  for (base::TimeTicks t : ahead) {
    if (t > now) {
      next = std::min(next, t);
    }
  }
  return next;
}

bool Settle::WantsDomProbe(base::TimeTicks now) const {
  if (probing_ || dom_unavailable_ || now < probe_retry_at_ ||
      NavigationPending() || !in_flight_.empty() || !blocked_reason_.empty()) {
    return false;
  }
  const base::TimeTicks quiet_from =
      committed_ ? std::max(last_network_, committed_at_) : last_network_;
  return !dom_quiet_since_ || *dom_quiet_since_ < quiet_from;
}

base::TimeDelta Settle::ProbeBudget(base::TimeTicks now) const {
  return std::max(SettleStart() + timings_.cap - now, base::Milliseconds(1));
}

std::string SettledLine(const Settle& settle,
                        SettleState state,
                        std::string_view done) {
  std::string line(done);
  std::vector<std::string> notes;
  if (std::optional<int> status = settle.http_status();
      status && *status >= 400) {
    std::string http = "HTTP " + base::NumberToString(*status);
    if (!settle.status_text().empty()) {
      http += " " + settle.status_text();
    }
    notes.push_back(http);
  }
  if ((state == SettleState::kCapped || state == SettleState::kTimedOut) &&
      settle.still_loading()) {
    notes.push_back("still loading");
  }
  for (size_t i = 0; i < notes.size(); ++i) {
    line += (i == 0 ? " (" : ", ") + notes[i];
  }
  return notes.empty() ? line : line + ")";
}

std::string NavigationTimeoutText(std::string_view url, base::TimeDelta wait) {
  return "Koan.timeout: " + std::string(url) + " did not answer within " +
         Seconds(wait) +
         " s (the server is slow or down; the tab keeps waiting for it). "
         "Take a snapshot later, navigate elsewhere, or allow longer with "
         "--timeout <seconds>";
}

}  // namespace koan::cli
