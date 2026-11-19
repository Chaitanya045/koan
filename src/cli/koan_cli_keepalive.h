// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_KEEPALIVE_H_
#define KOAN_CLI_KOAN_CLI_KEEPALIVE_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/time/time.h"
#include "koan/cli/koan_cli_client.h"

// Keeps an agent tab rendering while the CLI waits on it
// (.agent/notes/agent-control.md section 8): Koan.keepAlive, renewed as the
// wait goes on and ended when it does. Without it the tab's capturer goes 3 s
// after the last command and a page that animates or polls stalls while the
// user's window is covered. A keep-alive lapses by itself in the browser, so
// one this process never ends (it was killed) costs at most kRenewEvery more.
namespace koan::cli {

class TabKeepAlive {
 public:
  // How long each request asks for, and how often it is renewed. Short, so
  // that a lost one lapses soon; within Low Power Mode's 60 s.
  static constexpr base::TimeDelta kRequest = base::Seconds(30);
  static constexpr base::TimeDelta kRenewEvery = base::Seconds(10);

  // Asks at once. `on_event` gets the events that come while a request waits
  // for its answer (see Link::Call()).
  TabKeepAlive(Link& link,
               std::string target_id,
               base::RepeatingCallback<void(const Message&)> on_event = {});
  TabKeepAlive(const TabKeepAlive&) = delete;
  TabKeepAlive& operator=(const TabKeepAlive&) = delete;
  // Ends it (seconds 0), if the connection is still open.
  ~TabKeepAlive();

  // Renews it if kRenewEvery has passed (or it was not granted: asks again).
  void RenewIfDue();
  // When RenewIfDue() next has something to do.
  base::TimeTicks next_renewal() const { return next_; }
  // The browser granted it (an old Koan without Koan.keepAlive does not; the
  // wait then goes on as before, held by its own commands).
  bool held() const { return !hold_id_.empty(); }

 private:
  void Ask();

  const raw_ref<Link> link_;
  const std::string target_id_;
  const base::RepeatingCallback<void(const Message&)> on_event_;
  std::string hold_id_;
  base::TimeTicks next_;
};

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_KEEPALIVE_H_
