// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "koan/browser/agent/koan_agent_audit.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "koan/browser/agent/koan_agent_session.h"

namespace content {
class WebContents;
}

// The browser side of CDP forwarding (.agent/notes/agent-control.md sections
// 2, 4 and 7), on the UI thread: a CdpFacade per authenticated connection, the
// registry of which tabs each client may reach, the untrusted DevTools clients
// the facades talk to tabs through, the keep-alive lease of each agent tab, and
// the audit log every command is written to.
//
// Tabs enter the registry only through Koan.openTab / Target.createTarget
// (opened by the AgentTabOpener the agent containers install) or because a page
// in an agent tab opened them; nothing else, so a client never sees a tab of
// the user's it was not given.
namespace koan::agent {

class AgentForwarder {
 public:
  // Sends one JSON message to a connection (posted to the gateway).
  using SendCallback =
      base::RepeatingCallback<void(uint64_t connection_id, std::string json)>;

  // `log_dir` is where the audit log is written.
  AgentForwarder(SendCallback send, base::FilePath log_dir);
  AgentForwarder(const AgentForwarder&) = delete;
  AgentForwarder& operator=(const AgentForwarder&) = delete;
  ~AgentForwarder();

  void OnClientConnected(uint64_t connection_id,
                         const std::string& client_id,
                         const std::string& label);
  void OnClientMessage(uint64_t connection_id, AgentMessage message);
  void OnClientDisconnected(uint64_t connection_id);

  // Stop all agents or Agent ready off (`end_tasks`), or the app quits: every
  // connection's facade goes, every tab leaves the registry and every lease is
  // released. With `end_tasks` every task's container ends too
  // (containers::EndAgentTask(): its tabs close and a throwaway container is
  // cleared).
  void Reset(bool end_tasks);

  const TargetRegistry& registry() const { return registry_; }

 private:
  class Connection;
  class TabWatcher;

  Connection* ConnectionFor(uint64_t connection_id,
                            const std::string& client_id);

  // Puts an agent tab of the client into the registry, holds its lease and
  // tells the client's facades. Returns its target id, or empty if it could
  // not (the budgets, a tab of another profile).
  std::string AddAgentTab(content::WebContents* contents,
                          const std::string& client_id,
                          const std::string& task);
  void RemoveTab(const std::string& target_id, bool destroyed);
  // The tasks a client opened tabs for, so that each gets
  // containers::EndAgentTask() when it ends (Koan.endTask), when the client's
  // last connection goes, on Stop and on Agent ready off.
  void NoteTask(const std::string& client_id, const std::string& task);
  void EndTask(const std::string& client_id, const std::string& task);
  void EndClient(const std::string& client_id);
  // From the watcher: the tab closed, or its title or URL changed.
  void OnTabDestroyed(const std::string& target_id);
  void OnTabChanged(const std::string& target_id);
  // A page in an agent tab opened `opened` (AgentLeaseManager).
  void OnOpenedFromAgentTab(content::WebContents* source,
                            content::WebContents* opened);

  content::WebContents* ContentsFor(std::string_view target_id) const;
  void Audit(AuditRecord record);

  const SendCallback send_;
  TargetRegistry registry_;
  std::map<uint64_t, std::unique_ptr<Connection>> connections_;
  std::map<std::string, std::unique_ptr<TabWatcher>, std::less<>> tabs_;
  std::map<std::string, std::string, std::less<>> labels_;
  std::map<std::string, std::set<std::string>, std::less<>> tasks_;
  base::SequenceBound<AuditLog> audit_;
  base::WeakPtrFactory<AgentForwarder> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_
