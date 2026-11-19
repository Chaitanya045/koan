// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_SETTLE_H_
#define KOAN_CLI_KOAN_CLI_SETTLE_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "koan/cli/koan_cli_protocol.h"

// When `koan act` may say "ok" (.agent/notes/agent-control.md section 12,
// "Settle and return"): after an action, the tab's page has settled, so the
// snapshot `--snapshot` prints with it shows what the action did, and an
// agent needs no `sleep` and no second command. Pure: fed the tab's CDP
// events (Page and Network, which the facade passes scrubbed) and the times
// they came, it says what to wait for next. koan_cli_main.cc moves the bytes.
//
// The page has settled when all of these hold, or when the cap says stop:
//  - navigation: one the action started (the main frame began loading within
//    `navigation_grace`, or the action is a navigate) has committed and the
//    main frame's load event has fired; none started within the grace;
//  - network: no request of the tab is in flight (long-lived ones, a
//    WebSocket or an EventSource, do not count) and none ended in the last
//    `network_quiet`;
//  - DOM: the page's own MutationObserver saw no change for `dom_quiet`
//    (DomQuietExpression()), since after the network went quiet.
// A navigation that has not committed within `navigation_timeout` is a
// timeout; anything else that is still busy `cap` after the action (or after
// the commit) is given up on, and "ok" says the page was still busy.
namespace koan::cli {

struct SettleTimings {
  base::TimeDelta navigation_grace = base::Milliseconds(300);
  base::TimeDelta network_quiet = base::Milliseconds(250);
  base::TimeDelta dom_quiet = base::Milliseconds(250);
  base::TimeDelta cap = base::Seconds(3);
  // `koan act --timeout`.
  base::TimeDelta navigation_timeout = base::Seconds(15);
};

// Runs in the page (Runtime.evaluate, awaitPromise, returnByValue): resolves
// once no DOM change was seen for `quietMs` ms, or after `capMs` ms, with
// {quiet: bool}. A timer, not requestAnimationFrame, so that it also ends in
// a tab the OS does not paint. Formatted with the two numbers:
// DomQuietExpression().
std::string DomQuietExpression(base::TimeDelta quiet, base::TimeDelta cap);

enum class SettleState {
  // Keep waiting (until NextCheck()).
  kWaiting,
  // Everything above holds.
  kSettled,
  // The cap ran out with the page still busy (see still_loading()).
  kCapped,
  // A navigation did not commit in time.
  kTimedOut,
  // The page waits for the user (a dialog, a pause), or the tab went:
  // blocked_reason() says which.
  kBlocked,
};

class Settle {
 public:
  // `start`: when the action was sent. `navigating`: the action is a
  // navigate, so a navigation is expected (and timed) from `start`.
  // `target`: the tab (Koan.* events name it). `main_frame`: its main
  // frame's id (Page.getFrameTree), so that a subframe's load is not taken
  // for the page's.
  Settle(const SettleTimings& timings,
         base::TimeTicks start,
         bool navigating,
         std::string target,
         std::string main_frame);
  Settle(const Settle&) = delete;
  Settle& operator=(const Settle&) = delete;
  ~Settle();

  // A CDP event of the tab's session (or a Koan.* event of the client).
  void OnEvent(const Message& event, base::TimeTicks now);
  // The navigate's own answer came: it committed (or failed).
  void OnNavigateAnswered(base::TimeTicks now);
  // A DOM probe (DomQuietExpression()) was sent at `now`, and its answer
  // came at `now`.
  enum class DomProbe {
    kQuiet,
    // Its cap ran out with the DOM still changing.
    kBusy,
    // Its document went (the page navigated): another is sent soon.
    kPageChanged,
    // The page does not run it: the DOM is not waited for.
    kFailed,
  };
  void OnDomProbeSent(base::TimeTicks now);
  void OnDomProbe(DomProbe result, base::TimeTicks now);
  // The page waits for the user, as an answer said (Koan.paused, ...).
  void Block(std::string reason) { blocked_reason_ = std::move(reason); }

  SettleState Check(base::TimeTicks now) const;
  // When Check() may say something else without an event: the earliest of
  // the deadlines still ahead.
  base::TimeTicks NextCheck(base::TimeTicks now) const;
  // Whether to start a DOM probe now: nothing else is pending (no navigation
  // under way, nothing in flight), and the last probe is older than the last
  // network activity or commit.
  bool WantsDomProbe(base::TimeTicks now) const;
  // How long a probe started now may run (until the cap).
  base::TimeDelta ProbeBudget(base::TimeTicks now) const;

  // The main frame's HTTP status, if a navigation got a response.
  std::optional<int> http_status() const { return http_status_; }
  const std::string& status_text() const { return status_text_; }
  bool navigated() const { return committed_; }
  // The main frame had not fired its load event when the cap ran out.
  bool still_loading() const;
  const std::string& blocked_reason() const { return blocked_reason_; }
  size_t in_flight() const { return in_flight_.size(); }

 private:
  bool NavigationPending() const;
  base::TimeTicks SettleStart() const;

  const SettleTimings timings_;
  const base::TimeTicks start_;
  const std::string target_;
  const std::string main_frame_;
  // A navigation of the main frame began (and when), committed, loaded.
  std::optional<base::TimeTicks> navigation_started_;
  bool committed_ = false;
  base::TimeTicks committed_at_;
  bool loaded_ = false;
  std::optional<int> http_status_;
  std::string status_text_;
  std::set<std::string> in_flight_;
  base::TimeTicks last_network_;
  // When the probe that last said the DOM was quiet was sent: the DOM has
  // been quiet since then.
  std::optional<base::TimeTicks> dom_quiet_since_;
  base::TimeTicks probe_sent_;
  bool probing_ = false;
  // After a probe whose document went, the next one waits a little.
  base::TimeTicks probe_retry_at_;
  bool dom_unavailable_ = false;
  std::string blocked_reason_;
};

// What `koan act` prints for a settled action: "ok", "ok (HTTP 504 Gateway
// Timeout)" for a navigation whose main frame answered 4xx or 5xx (the tab
// shows the server's page, which the snapshot reads; the exit code is 0),
// and "ok (still loading)" when the cap ran out before the load event.
// `done` is what the action itself says ("ok", "ok: selected ...").
std::string SettledLine(const Settle& settle,
                        SettleState state,
                        std::string_view done = "ok");

// The error for a navigation that did not commit in time.
std::string NavigationTimeoutText(std::string_view url, base::TimeDelta wait);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_SETTLE_H_
