// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_REGISTRY_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_REGISTRY_H_

#include <stddef.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

// Which tabs each agent client may see and drive
// (.agent/notes/agent-control.md sections 4.2, 7 and 9.1): the facade lists a
// client's tabs from here and nothing else, so an agent that attaches to every
// target (agent-browser does) only ever finds its own.
//
// Two kinds of tab:
//  - **agent tabs**, which the agent opened (in an agent container, whatever
//    opens it): always kDriven, unless the user pauses them;
//  - **user tabs**, which the user handed over or approved a take-over of:
//    kReadable (screenshot and snapshot, no input) or kDriven, and kPaused when
//    the user types or clicks in them. At most one client holds a user tab.
//
// Tabs are named by their DevTools target id. Pure bookkeeping: whether a
// frame may be reached at all is koan_agent_reach.h's, and the browser side
// removes a tab from here when it closes.
namespace koan::agent {

enum class TabState {
  kReadable,
  kDriven,
  kPaused,
};

struct TargetEntry {
  std::string client_id;
  // The task (agent container) an agent tab belongs to; empty for user tabs.
  std::string task;
  bool agent_owned = false;
  TabState state = TabState::kDriven;
  // For a user tab the agent drives: the site (scheme and registrable domain)
  // the user allowed. Leaving it ends write access (SetReadable()).
  std::string site;
};

class TargetRegistry {
 public:
  // The open-tab budgets (design section 8): fixed for now.
  static constexpr size_t kMaxTabsPerTask = 8;
  static constexpr size_t kMaxTabsPerClient = 12;
  static constexpr size_t kMaxTabsTotal = 16;

  enum class AddResult {
    kOk,
    kTaskFull,
    kClientFull,
    kBrowserFull,
    // The id is already in the registry.
    kExists,
    kInvalid,
  };

  TargetRegistry();
  TargetRegistry(const TargetRegistry&) = delete;
  TargetRegistry& operator=(const TargetRegistry&) = delete;
  ~TargetRegistry();

  // Can `client_id` open another tab in `task`? The caller asks before it opens
  // the tab, then adds it.
  AddResult CanAddAgentTab(std::string_view client_id,
                           std::string_view task) const;
  AddResult AddAgentTab(std::string target_id,
                        std::string client_id,
                        std::string task);

  // The user handed over, or approved a take-over of, one of their tabs.
  // kExists if another client holds it (or it is an agent tab). A user tab does
  // not count against the open-tab budgets: the agent did not open it.
  AddResult GrantUserTab(std::string target_id,
                         std::string client_id,
                         TabState state,
                         std::string site = std::string());

  // The user typed or clicked in the tab. False if it is not in the registry.
  bool Pause(std::string_view target_id);
  // The user handed a paused tab back (the agent cannot do this itself).
  bool Resume(std::string_view target_id);
  // A user tab left the site the user allowed: the agent may only read it
  // now. False if it is not a user tab, or was readable already.
  bool SetReadable(std::string_view target_id);

  // The tab closed, or was released or handed back to the user.
  bool Remove(std::string_view target_id);
  // The client was revoked or disconnected for good: returns its tabs, which
  // are gone from the registry.
  std::vector<std::string> RemoveClient(std::string_view client_id);
  // The task ended: returns its tabs, which are gone from the registry.
  std::vector<std::string> RemoveTask(std::string_view client_id,
                                      std::string_view task);

  const TargetEntry* Find(std::string_view target_id) const;
  // The tabs `client_id` may see, in a stable order.
  std::vector<std::string> TargetsFor(std::string_view client_id) const;
  size_t size() const { return entries_.size(); }

  // Whether the client may send `method` to the tab now: a tab of another
  // client, or one not in the registry, never; a kDriven tab, anything the
  // method policy allows; a kReadable or kPaused tab, only the methods that
  // change nothing (IsReadOnlyMethod()), and on a user's tab not
  // Network.enable (the user's own traffic is not part of reading a tab).
  bool MaySend(std::string_view client_id,
               std::string_view target_id,
               std::string_view method) const;

 private:
  // Agent tabs of the client, in `task` or (nullopt) in all its tasks.
  size_t CountFor(std::string_view client_id,
                  std::optional<std::string_view> task) const;

  std::map<std::string, TargetEntry, std::less<>> entries_;
};

// The methods that only read (design section 9.2: "reads still work" on a
// paused tab). Unlisted means it may change the page, and is refused on a
// readable or paused tab: Input.*, navigation, Runtime.evaluate and
// callFunctionOn (arbitrary script can change anything), DOM and Emulation
// writes.
bool IsReadOnlyMethod(std::string_view method);
// The list, sorted. For the tests.
base::span<const std::string_view> ReadOnlyMethods();

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_REGISTRY_H_
